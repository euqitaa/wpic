// wpic - Minimal Image Viewer for Windows
// Build: g++ -O2 -s wpic.cpp wpic_res.o -o wpic.exe -mwindows -static -lgdiplus -luser32 -lgdi32 -lshell32 -lcomctl32 -lshlwapi -lole32 -fexceptions

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600  // Windows Vista minimum
#endif

#ifndef _WIN32_IE
#define _WIN32_IE 0x0600     // IE6/Windows XP SP2
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>     // Must be before shellapi.h for REFIID
#include <shellapi.h>
#include <commctrl.h>

#ifndef _WIN32_WCE
#include <wtypes.h>
#endif

#include <gdiplus.h>
#include <windowsx.h>
#include <math.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

using namespace Gdiplus;

// Compatibility defines for older MinGW/Windows SDK
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef TBS_TOOLTIPS
#define TBS_TOOLTIPS 0x0100
#endif

#ifndef DPI_AWARENESS_CONTEXT
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_UNAWARE ((DPI_AWARENESS_CONTEXT)-1)
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE ((DPI_AWARENESS_CONTEXT)-2)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((DPI_AWARENESS_CONTEXT)-3)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

// Function pointer typedefs for DPI awareness
typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFunc)(DPI_AWARENESS_CONTEXT);
typedef int (WINAPI *GetDpiForSystemFunc)(void);
typedef UINT (WINAPI *GetDpiForWindowFunc)(HWND);
typedef BOOL (WINAPI *SetProcessDPIAwareFunc)(void);

// Menu IDs
#define IDM_OPTIONS_DARKMODE 2001
#define IDM_OPTIONS_ABOUT 2002

// DPI Scale aware constants - INCREASED for better usability
#define BASE_TOOLBAR_HEIGHT 72
#define BASE_BUTTON_HEIGHT 56
#define TRACKBAR_WIDTH 160
#define TRACKBAR_HEIGHT 22
#define STATUS_BAR_HEIGHT 26
#define ZOOM_LABEL_WIDTH 52

// Trackbar uses 0..100 internal units with log scale:
//   pos=0  → 10% zoom,  pos=50 → 100% zoom,  pos=100 → 1000% zoom
#define TRACKBAR_UNITS 100

static inline int ZoomToTrackPos(float zoom) {
    // zoom range: 0.1 .. 10.0  →  pos range: 0 .. 100
    // log10(zoom) range: -1 .. 1  →  map to 0..100
    float logZ = log10f((std::max)(zoom, 0.01f));
    int pos = (int)((logZ + 1.0f) * 50.0f + 0.5f);
    if (pos < 0) pos = 0;
    if (pos > TRACKBAR_UNITS) pos = TRACKBAR_UNITS;
    return pos;
}

static inline float TrackPosToZoom(int pos) {
    // inverse: pos 0..100 → zoom 0.1..10
    float logZ = pos / 50.0f - 1.0f;
    return powf(10.0f, logZ);
}
#define ID_BTN_PREV 1001
#define ID_BTN_NEXT 1002
#define ID_BTN_ZOOM_OUT 1003
#define ID_BTN_ZOOM_IN 1004
#define ID_BTN_FIT 1005
#define ID_BTN_ACTUAL 1006
#define ID_BTN_ROTATE_LEFT 1007
#define ID_BTN_ROTATE_RIGHT 1008
#define ID_BTN_DELETE 1009

// Cache constants
#define CACHE_SIZE 3  // Number of images to cache ahead and behind

// Button layout
struct ButtonDef {
    int id;
    const wchar_t* symbol;
    const wchar_t* label;
    int x;
    int width;
};

// Cached image entry
struct CachedImage {
    std::wstring path;
    Image* image;
    volatile bool loading;
    volatile bool ready;
    
    CachedImage() : image(nullptr), loading(false), ready(false) {}
    
    CachedImage(const CachedImage&) = delete;
    CachedImage& operator=(const CachedImage&) = delete;
    
    CachedImage(CachedImage&& other) noexcept 
        : path(std::move(other.path)), 
          image(other.image),
          loading(other.loading),
          ready(other.ready) {
        other.image = nullptr;
        other.loading = false;
        other.ready = false;
    }
    
    CachedImage& operator=(CachedImage&& other) noexcept {
        if (this != &other) {
            if (image) delete image;
            path = std::move(other.path);
            image = other.image;
            loading = other.loading;
            ready = other.ready;
            other.image = nullptr;
            other.loading = false;
            other.ready = false;
        }
        return *this;
    }
    
    ~CachedImage() { if (image) delete image; }
    
    void clear() {
        if (image) {
            delete image;
            image = nullptr;
        }
        loading = false;
        ready = false;
        path.clear();
    }
};

// Global state
HWND g_hwnd = nullptr;
HWND g_hToolbar = nullptr;
HWND g_hTrackbar = nullptr;
HWND g_hZoomLabel = nullptr;
HWND g_hStatusBar = nullptr;
HMENU g_hMenu = nullptr;

// Trackbar subclass - forwards mousewheel to main window for zoom-to-cursor
static WNDPROC g_origTrackbarProc = nullptr;
LRESULT CALLBACK TrackbarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_MOUSEWHEEL) {
        return SendMessageW(g_hwnd, WM_MOUSEWHEEL, wParam, lParam);
    }
    return CallWindowProcW(g_origTrackbarProc, hwnd, msg, wParam, lParam);
}
std::vector<std::wstring> g_imageFiles;
std::wstring g_currentFolder;
size_t g_currentIndex = 0;
Image* g_currentImage = nullptr;
float g_zoom = 1.0f;
bool g_fitToWindow = true;
POINT g_panOffset = {0, 0};
bool g_isPanning = false;
POINT g_lastMousePos = {0, 0};
int g_rotation = 0;
bool g_darkMode = false;

// DPI globals
int g_dpi = 96;
float g_dpiScale = 1.0f;

// Cache state
std::vector<CachedImage> g_imageCache;
CRITICAL_SECTION g_cacheLock;
HANDLE g_cacheThread = nullptr;
volatile bool g_cacheRunning = false;
volatile size_t g_cacheTargetIndex = 0;
volatile bool g_cacheReady = false;

// Supported extensions
const wchar_t* g_supportedExts[] = {L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".tiff", L".tif", L".webp", nullptr};

int Scale(int value) {
    return (int)(value * g_dpiScale);
}

bool IsImageFile(const std::wstring& filename) {
    size_t dotPos = filename.find_last_of(L'.');
    if (dotPos == std::wstring::npos) return false;
    std::wstring ext = filename.substr(dotPos);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c){ return std::towlower(c); });
    for (int i = 0; g_supportedExts[i]; ++i) {
        if (ext == g_supportedExts[i]) return true;
    }
    return false;
}

void ScanFolder(const std::wstring& targetFile) {
    g_imageFiles.clear();
    size_t lastSlash = targetFile.find_last_of(L'\\');
    if (lastSlash == std::wstring::npos) {
        lastSlash = targetFile.find_last_of(L'/');
    }
    g_currentFolder = (lastSlash != std::wstring::npos) ? targetFile.substr(0, lastSlash) : L".";

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW((g_currentFolder + L"\\*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring filename = findData.cFileName;
            if (IsImageFile(filename)) {
                g_imageFiles.push_back(g_currentFolder + L"\\" + filename);
            }
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);

    std::sort(g_imageFiles.begin(), g_imageFiles.end());

    g_currentIndex = 0;
    std::wstring targetLower = targetFile;
    std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(), [](wchar_t c){ return std::towlower(c); });
    
    for (size_t i = 0; i < g_imageFiles.size(); ++i) {
        std::wstring fileLower = g_imageFiles[i];
        std::transform(fileLower.begin(), fileLower.end(), fileLower.begin(), [](wchar_t c){ return std::towlower(c); });
        if (fileLower == targetLower) {
            g_currentIndex = i;
            break;
        }
    }
}

void StopCacheThread() {
    g_cacheRunning = false;
    g_cacheReady = false;
    if (g_cacheThread) {
        WaitForSingleObject(g_cacheThread, 2000);
        CloseHandle(g_cacheThread);
        g_cacheThread = nullptr;
    }
}

DWORD WINAPI CacheWorker(LPVOID lpParam);

void StartCacheThread() {
    StopCacheThread();
    g_cacheRunning = true;
    g_cacheReady = false;
    g_cacheThread = CreateThread(nullptr, 0, CacheWorker, nullptr, 0, nullptr);
}

void WakeCacheThread() {
    g_cacheReady = true;
}

Image* GetCachedImage(const std::wstring& path) {
    EnterCriticalSection(&g_cacheLock);
    for (auto& entry : g_imageCache) {
        if (entry.ready && entry.path == path && entry.image) {
            Image* img = entry.image;
            entry.image = nullptr;
            entry.ready = false;
            LeaveCriticalSection(&g_cacheLock);
            return img;
        }
    }
    LeaveCriticalSection(&g_cacheLock);
    return nullptr;
}

size_t GetCacheTarget() {
    return g_cacheTargetIndex;
}

size_t GetImageCount() {
    EnterCriticalSection(&g_cacheLock);
    size_t count = g_imageFiles.size();
    LeaveCriticalSection(&g_cacheLock);
    return count;
}

std::wstring GetImagePath(size_t index) {
    EnterCriticalSection(&g_cacheLock);
    std::wstring path;
    if (index < g_imageFiles.size()) {
        path = g_imageFiles[index];
    }
    LeaveCriticalSection(&g_cacheLock);
    return path;
}

DWORD WINAPI CacheWorker(LPVOID lpParam) {
    while (g_cacheRunning) {
        if (!g_cacheReady) {
            Sleep(100);
            continue;
        }
        
        size_t target = GetCacheTarget();
        size_t imgCount = GetImageCount();
        
        if (imgCount == 0) {
            Sleep(100);
            continue;
        }
        
        std::vector<size_t> toCache;
        for (int offset = -CACHE_SIZE; offset <= CACHE_SIZE; ++offset) {
            if (offset == 0) continue;
            size_t idx = (target + offset + imgCount) % imgCount;
            toCache.push_back(idx);
        }
        
        for (size_t idx : toCache) {
            if (!g_cacheRunning) break;
            
            std::wstring path = GetImagePath(idx);
            if (path.empty()) continue;
            
            bool alreadyCached = false;
            EnterCriticalSection(&g_cacheLock);
            for (auto& entry : g_imageCache) {
                if (entry.path == path && (entry.ready || entry.loading)) {
                    alreadyCached = true;
                    break;
                }
            }
            LeaveCriticalSection(&g_cacheLock);
            
            if (alreadyCached) continue;
            
            CachedImage* slot = nullptr;
            EnterCriticalSection(&g_cacheLock);
            
            for (auto& entry : g_imageCache) {
                if (!entry.ready && !entry.loading) {
                    slot = &entry;
                    break;
                }
            }
            
            if (!slot && g_imageCache.size() < (CACHE_SIZE * 2 + 2)) {
                g_imageCache.emplace_back();
                slot = &g_imageCache.back();
            }
            
            if (!slot && !g_imageCache.empty()) {
                for (auto& entry : g_imageCache) {
                    slot = &entry;
                    entry.clear();
                    break;
                }
            }
            
            if (slot) {
                slot->path = path;
                slot->loading = true;
            }
            LeaveCriticalSection(&g_cacheLock);
            
            if (slot) {
                Image* img = nullptr;
                try {
                    img = new Image(path.c_str());
                } catch (...) {
                    img = nullptr;
                }
                
                EnterCriticalSection(&g_cacheLock);
                if (g_cacheRunning) {
                    slot->image = img;
                    slot->loading = false;
                    slot->ready = (img && img->GetLastStatus() == Ok);
                    if (!slot->ready && img) {
                        delete img;
                        slot->image = nullptr;
                    }
                } else {
                    if (img) delete img;
                    slot->clear();
                }
                LeaveCriticalSection(&g_cacheLock);
            }
        }
        
        Sleep(50);
    }
    return 0;
}

void InitCache() {
    InitializeCriticalSection(&g_cacheLock);
    g_imageCache.clear();
    g_imageCache.reserve(CACHE_SIZE * 2 + 2);
}

void ClearCache() {
    EnterCriticalSection(&g_cacheLock);
    for (auto& entry : g_imageCache) {
        entry.clear();
    }
    LeaveCriticalSection(&g_cacheLock);
}

void UpdateTitle() {
    if (g_imageFiles.empty()) {
        SetWindowTextW(g_hwnd, L"wpic");
        return;
    }
    std::wstring filename = g_imageFiles[g_currentIndex].substr(g_imageFiles[g_currentIndex].find_last_of(L'\\') + 1);
    std::wstring title = filename + L" - wpic";
    SetWindowTextW(g_hwnd, title.c_str());
}

void UpdateTrackbarPos() {
    if (g_hTrackbar) {
        int pos = ZoomToTrackPos(g_zoom);
        SendMessageW(g_hTrackbar, TBM_SETPOS, TRUE, pos);
    }
    if (g_hZoomLabel) {
        wchar_t buf[16];
        int pct = (int)(g_zoom * 100 + 0.5f);
        wsprintfW(buf, L"%d%%", pct);
        SetWindowTextW(g_hZoomLabel, buf);
    }
}

void UpdateStatusBar() {
    if (!g_hStatusBar) return;
    wchar_t buf[256] = {};
    if (!g_imageFiles.empty() && g_currentImage && g_currentImage->GetLastStatus() == Ok) {
        std::wstring fname = g_imageFiles[g_currentIndex];
        size_t sl = fname.find_last_of(L'\\');
        if (sl != std::wstring::npos) fname = fname.substr(sl + 1);
        int w = g_currentImage->GetWidth();
        int h = g_currentImage->GetHeight();
        int pct = (int)(g_zoom * 100 + 0.5f);
        wsprintfW(buf, L"  %s    %d × %d px    %d of %d",
            fname.c_str(), w, h,
            (int)(g_currentIndex + 1), (int)g_imageFiles.size());
    } else if (g_imageFiles.empty()) {
        wcscpy(buf, L"  Drop an image to open");
    }
    SetWindowTextW(g_hStatusBar, buf);
}

void LoadImage(size_t index) {
    if (g_currentImage) {
        delete g_currentImage;
        g_currentImage = nullptr;
    }
    
    EnterCriticalSection(&g_cacheLock);
    size_t fileCount = g_imageFiles.size();
    LeaveCriticalSection(&g_cacheLock);
    
    if (index >= fileCount) return;

    g_currentIndex = index;
    std::wstring path = GetImagePath(index);
    if (path.empty()) return;
    
    g_currentImage = GetCachedImage(path);
    
    if (!g_currentImage) {
        try {
            g_currentImage = new Image(path.c_str());
        } catch (...) {
            g_currentImage = nullptr;
        }
    }
    
    g_zoom = 1.0f;
    g_panOffset = {0, 0};
    g_fitToWindow = true;
    g_rotation = 0;
    UpdateTrackbarPos();
    UpdateTitle();
    UpdateStatusBar();
    InvalidateRect(g_hwnd, nullptr, FALSE);
    
    g_cacheTargetIndex = index;
}

void NextImage() {
    if (g_imageFiles.empty()) return;
    LoadImage((g_currentIndex + 1) % g_imageFiles.size());
}

void PrevImage() {
    if (g_imageFiles.empty()) return;
    LoadImage((g_currentIndex + g_imageFiles.size() - 1) % g_imageFiles.size());
}

void ToggleZoom() {
    g_fitToWindow = !g_fitToWindow;
    if (g_fitToWindow) {
        g_zoom = 1.0f;
        g_panOffset = {0, 0};
    }
    UpdateTrackbarPos();
    UpdateStatusBar();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ZoomIn() {
    g_fitToWindow = false;
    g_zoom = (std::min)(g_zoom * 1.25f, 10.0f);
    UpdateTrackbarPos();
    UpdateStatusBar();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ZoomOut() {
    g_fitToWindow = false;
    g_zoom = (std::max)(g_zoom / 1.25f, 0.05f);
    UpdateTrackbarPos();
    UpdateStatusBar();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ActualSize() {
    g_fitToWindow = false;
    g_zoom = 1.0f;
    g_panOffset = {0, 0};
    UpdateTrackbarPos();
    UpdateStatusBar();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void RotateLeft() {
    g_rotation = (g_rotation - 90) % 360;
    if (g_rotation < 0) g_rotation += 360;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void RotateRight() {
    g_rotation = (g_rotation + 90) % 360;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ToggleDarkMode() {
    g_darkMode = !g_darkMode;

    if (g_hMenu) {
        HMENU hOptionsMenu = GetSubMenu(g_hMenu, 0);
        if (hOptionsMenu) {
            CheckMenuItem(hOptionsMenu, IDM_OPTIONS_DARKMODE, 
                g_darkMode ? MF_CHECKED : MF_UNCHECKED);
        }
    }

    InvalidateRect(g_hwnd, nullptr, FALSE);
    if (g_hToolbar) InvalidateRect(g_hToolbar, nullptr, FALSE);
    if (g_hStatusBar) InvalidateRect(g_hStatusBar, nullptr, FALSE);
    if (g_hZoomLabel) InvalidateRect(g_hZoomLabel, nullptr, FALSE);
}

void DeleteToRecycleBin(const std::wstring& filePath) {
    SHFILEOPSTRUCTW fileOp = {};
    fileOp.wFunc = FO_DELETE;

    wchar_t* from = new wchar_t[filePath.length() + 2];
    wcscpy(from, filePath.c_str());
    from[filePath.length() + 1] = L'\0';

    fileOp.pFrom = from;
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;

    SHFileOperationW(&fileOp);
    delete[] from;
}

void DeleteCurrentImage() {
    if (g_imageFiles.empty()) return;

    std::wstring currentFile = g_imageFiles[g_currentIndex];
    std::wstring filename = currentFile.substr(currentFile.find_last_of(L'\\') + 1);

    std::wstring msg = L"Are you sure you want to move this file to the Recycle Bin?\n\n" + filename;
    int result = MessageBoxW(g_hwnd, msg.c_str(), L"wpic - Confirm Delete", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);

    if (result != IDYES) return;

    size_t nextIndex = (g_currentIndex + 1) % g_imageFiles.size();
    if (nextIndex == g_currentIndex && g_imageFiles.size() == 1) {
        if (g_currentImage) {
            delete g_currentImage;
            g_currentImage = nullptr;
        }
        DeleteToRecycleBin(currentFile);
        g_imageFiles.clear();
        ClearCache();
        SetWindowTextW(g_hwnd, L"wpic");
        InvalidateRect(g_hwnd, nullptr, FALSE);
        return;
    }

    LoadImage(nextIndex);
    DeleteToRecycleBin(currentFile);
    g_imageFiles.erase(g_imageFiles.begin() + g_currentIndex);
    if (g_currentIndex >= g_imageFiles.size()) g_currentIndex = 0;
    UpdateTitle();
}

LRESULT CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            if (g_darkMode) {
                SetWindowLongW(hwnd, GWL_EXSTYLE, GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_COMPOSITED);
            }

            HWND hTitle = CreateWindowExW(0, L"STATIC", L"wpic",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, Scale(20), Scale(400), Scale(40), hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)CreateFontW(Scale(24), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"), TRUE);

            HWND hVersion = CreateWindowExW(0, L"STATIC", L"Version 0.5.3",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, Scale(65), Scale(400), Scale(25), hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hVersion, WM_SETFONT, (WPARAM)CreateFontW(Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"), TRUE);

            HWND hAuthor = CreateWindowExW(0, L"STATIC", L"by euqitaa",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, Scale(95), Scale(400), Scale(25), hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hAuthor, WM_SETFONT, (WPARAM)CreateFontW(Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"), TRUE);

            HWND hLink = CreateWindowExW(0, L"STATIC", L"https://github.com/euqitaa/wpic",
                WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY,
                Scale(50), Scale(140), Scale(300), Scale(30), hwnd, (HMENU)1001, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hLink, WM_SETFONT, (WPARAM)CreateFontW(Scale(12), 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"), TRUE);

            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            EnableWindow(g_hwnd, TRUE);
            SetForegroundWindow(g_hwnd);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == 1001) {
                ShellExecuteW(nullptr, L"open", L"https://github.com/euqitaa/wpic", nullptr, nullptr, SW_SHOWNORMAL);
            }
            return 0;

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
            if (g_darkMode) {
                HDC hdc = (HDC)wParam;
                SetBkColor(hdc, RGB(45, 45, 45));
                SetTextColor(hdc, RGB(255, 255, 255));
                return (LRESULT)CreateSolidBrush(RGB(45, 45, 45));
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowAboutDialog() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = AboutDlgProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"wpicAboutDlg";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"wpicAboutDlg", L"About wpic",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, Scale(400), Scale(220),
        g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!hDlg) return;

    RECT rcDlg, rcOwner;
    GetWindowRect(hDlg, &rcDlg);
    GetWindowRect(g_hwnd, &rcOwner);
    int x = rcOwner.left + (rcOwner.right - rcOwner.left - (rcDlg.right - rcDlg.left)) / 2;
    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - (rcDlg.bottom - rcDlg.top)) / 2;
    SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

    EnableWindow(g_hwnd, FALSE);
}

void GetButtonLayout(int windowWidth, int trackbarWidth, ButtonDef* buttons, int& count) {
    count = 9;

    // Compact but readable button widths
    buttons[0] = {ID_BTN_PREV, L"◄", L"Prev", 0, Scale(55)};
    buttons[1] = {ID_BTN_NEXT, L"►", L"Next", 0, Scale(55)};
    buttons[2] = {ID_BTN_ZOOM_OUT, L"−", L"Out", 0, Scale(50)};
    buttons[3] = {ID_BTN_ZOOM_IN, L"+", L"In", 0, Scale(50)};
    buttons[4] = {ID_BTN_FIT, L"□", L"Fit", 0, Scale(55)};
    buttons[5] = {ID_BTN_ACTUAL, L"1:1", L"Actual", 0, Scale(60)};
    buttons[6] = {ID_BTN_ROTATE_LEFT, L"↺", L"Left", 0, Scale(60)};
    buttons[7] = {ID_BTN_ROTATE_RIGHT, L"↻", L"Right", 0, Scale(60)};
    buttons[8] = {ID_BTN_DELETE, L"✕", L"Del", 0, Scale(55)};

    int totalWidth = Scale(10);
    for (int i = 0; i < count; i++) {
        totalWidth += buttons[i].width + Scale(3);
        if (i == 1 || i == 5 || i == 7) totalWidth += Scale(12);
    }

    int availableWidth = windowWidth - trackbarWidth - Scale(ZOOM_LABEL_WIDTH) - Scale(30);
    int startX = (availableWidth - totalWidth) / 2;
    if (startX < Scale(10)) startX = Scale(10);

    int x = startX;
    for (int i = 0; i < count; i++) {
        buttons[i].x = x;
        x += buttons[i].width + Scale(3);
        if (i == 1 || i == 5 || i == 7) x += Scale(12);
    }
}

// Forward declarations for toolbar sub-windows
LRESULT CALLBACK ToolbarParentWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK ToolbarParentWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static ButtonDef buttons[9];
    static int btnCount = 0;
    static int hoveredBtn = -1;
    static int pressedBtn = -1;
    
    switch (msg) {
        case WM_CREATE: {
            RECT rc;
            GetClientRect(GetParent(hwnd), &rc);
            GetButtonLayout(rc.right, Scale(TRACKBAR_WIDTH), buttons, btnCount);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);

            COLORREF bgColor = g_darkMode ? RGB(38, 38, 42) : RGB(248, 248, 250);
            HBRUSH bgBrush = CreateSolidBrush(bgColor);
            FillRect(hdc, &rc, bgBrush);
            DeleteObject(bgBrush);

            // Top border line - subtle
            COLORREF borderColor = g_darkMode ? RGB(65, 65, 72) : RGB(210, 210, 215);
            HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, 0, 0, nullptr);
            LineTo(hdc, rc.right, 0);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);

            // Get current layout
            RECT parentRc;
            GetClientRect(GetParent(hwnd), &parentRc);
            GetButtonLayout(parentRc.right, Scale(TRACKBAR_WIDTH), buttons, btnCount);

            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            COLORREF textColor = g_darkMode ? RGB(225, 225, 230) : RGB(45, 45, 55);
            COLORREF labelColor = g_darkMode ? RGB(140, 140, 150) : RGB(110, 110, 120);

            int btnHeight = Scale(BASE_BUTTON_HEIGHT - 6);
            int btnTop = (rc.bottom - btnHeight) / 2;

            for (int i = 0; i < btnCount; i++) {
                RECT btnRc = {buttons[i].x, btnTop, buttons[i].x + buttons[i].width, btnTop + btnHeight};
                bool hovered = (pt.x >= btnRc.left && pt.x < btnRc.right && pt.y >= btnRc.top && pt.y < btnRc.bottom);
                bool pressed = (pressedBtn == buttons[i].id);
                bool isDelete = (buttons[i].id == ID_BTN_DELETE);

                // Button background with rounded feel via layered rects
                COLORREF btnBgColor;
                if (pressed) {
                    btnBgColor = isDelete ? (g_darkMode ? RGB(120, 40, 40) : RGB(210, 80, 80))
                                          : (g_darkMode ? RGB(65, 65, 75) : RGB(195, 195, 205));
                } else if (hovered) {
                    btnBgColor = isDelete ? (g_darkMode ? RGB(90, 35, 35) : RGB(255, 220, 220))
                                          : (g_darkMode ? RGB(55, 55, 65) : RGB(225, 225, 235));
                } else {
                    btnBgColor = bgColor;
                }

                // Draw a rounded rectangle for the button
                if (pressed || hovered) {
                    HBRUSH brush = CreateSolidBrush(btnBgColor);
                    // Simple rounded rect: draw normal rect then paint corners
                    RECT inflated = {btnRc.left - 1, btnRc.top - 1, btnRc.right + 1, btnRc.bottom + 1};
                    FillRect(hdc, &btnRc, brush);
                    DeleteObject(brush);

                    // Rounded border
                    COLORREF btnBorderColor = isDelete 
                        ? (g_darkMode ? RGB(160, 60, 60) : RGB(220, 100, 100))
                        : (g_darkMode ? RGB(85, 85, 100) : RGB(190, 190, 205));
                    HPEN borderPen = CreatePen(PS_SOLID, 1, btnBorderColor);
                    HPEN oldBorderPen = (HPEN)SelectObject(hdc, borderPen);
                    HBRUSH oldBrush2 = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    RoundRect(hdc, btnRc.left, btnRc.top, btnRc.right, btnRc.bottom, Scale(5), Scale(5));
                    SelectObject(hdc, oldBorderPen);
                    SelectObject(hdc, oldBrush2);
                    DeleteObject(borderPen);
                }

                // Separator lines between groups
                if (i == 1 || i == 5 || i == 7) {
                    int sepX = btnRc.right + Scale(6);
                    COLORREF sepPenColor = g_darkMode ? RGB(65, 65, 75) : RGB(210, 210, 218);
                    HPEN sepPen = CreatePen(PS_SOLID, 1, sepPenColor);
                    HPEN oldSepPen = (HPEN)SelectObject(hdc, sepPen);
                    MoveToEx(hdc, sepX, btnTop + Scale(6), nullptr);
                    LineTo(hdc, sepX, btnTop + btnHeight - Scale(6));
                    SelectObject(hdc, oldSepPen);
                    DeleteObject(sepPen);
                }

                // Symbol
                SetBkMode(hdc, TRANSPARENT);
                COLORREF symColor = (isDelete && hovered && !pressed) 
                    ? (g_darkMode ? RGB(255, 100, 100) : RGB(200, 50, 50))
                    : textColor;
                SetTextColor(hdc, symColor);
                HFONT symbolFont = CreateFontW(-Scale(20), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI Symbol");
                HFONT oldFont = (HFONT)SelectObject(hdc, symbolFont);

                RECT symbolRect = {btnRc.left, btnRc.top + Scale(4), btnRc.right, btnRc.top + Scale(28)};
                DrawTextW(hdc, buttons[i].symbol, -1, &symbolRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdc, oldFont);
                DeleteObject(symbolFont);

                // Label
                SetTextColor(hdc, labelColor);
                HFONT labelFont = CreateFontW(-Scale(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
                oldFont = (HFONT)SelectObject(hdc, labelFont);

                RECT labelRect = {btnRc.left, btnRc.top + Scale(26), btnRc.right, btnRc.bottom - Scale(2)};
                DrawTextW(hdc, buttons[i].label, -1, &labelRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdc, oldFont);
                DeleteObject(labelFont);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);

            int btnHeight = Scale(BASE_BUTTON_HEIGHT - 6);
            int btnTop = (Scale(BASE_TOOLBAR_HEIGHT) - btnHeight) / 2;

            int newHover = -1;
            for (int i = 0; i < btnCount; i++) {
                if (x >= buttons[i].x && x < buttons[i].x + buttons[i].width && 
                    y >= btnTop && y < btnTop + btnHeight) {
                    newHover = buttons[i].id;
                    break;
                }
            }

            if (newHover != hoveredBtn) {
                hoveredBtn = newHover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);

            int btnHeight = Scale(BASE_BUTTON_HEIGHT - 6);
            int btnTop = (Scale(BASE_TOOLBAR_HEIGHT) - btnHeight) / 2;

            for (int i = 0; i < btnCount; i++) {
                if (x >= buttons[i].x && x < buttons[i].x + buttons[i].width && 
                    y >= btnTop && y < btnTop + btnHeight) {
                    pressedBtn = buttons[i].id;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (pressedBtn != -1) {
                SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(pressedBtn, 0), 0);
                pressedBtn = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            hoveredBtn = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_SIZE:
            RECT parentRc;
            GetClientRect(GetParent(hwnd), &parentRc);
            GetButtonLayout(parentRc.right, Scale(TRACKBAR_WIDTH), buttons, btnCount);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
            
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void Render(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right;
    int height = clientRect.bottom - Scale(BASE_TOOLBAR_HEIGHT) - Scale(STATUS_BAR_HEIGHT);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    Color bgColor = g_darkMode ? Color(28, 28, 32) : Color(235, 235, 240);
    Graphics graphics(memDC);
    graphics.Clear(bgColor);

    // Draw status bar background area
    {
        int sbH = Scale(STATUS_BAR_HEIGHT);
        RECT sbRect = {0, clientRect.bottom - sbH, clientRect.right, clientRect.bottom};
        HBRUSH sbBrush = CreateSolidBrush(g_darkMode ? RGB(30, 30, 33) : RGB(225, 225, 230));
        FillRect(memDC, &sbRect, sbBrush);
        DeleteObject(sbBrush);
        // Thin top border
        HPEN sbPen = CreatePen(PS_SOLID, 1, g_darkMode ? RGB(55, 55, 62) : RGB(205, 205, 210));
        HPEN oldSbPen = (HPEN)SelectObject(memDC, sbPen);
        MoveToEx(memDC, 0, sbRect.top, nullptr);
        LineTo(memDC, clientRect.right, sbRect.top);
        SelectObject(memDC, oldSbPen);
        DeleteObject(sbPen);
    }

    if (g_currentImage && g_currentImage->GetLastStatus() == Ok) {
        int imgWidth = g_currentImage->GetWidth();
        int imgHeight = g_currentImage->GetHeight();

        bool rotated90 = (g_rotation == 90 || g_rotation == 270);
        int renderWidth = rotated90 ? imgHeight : imgWidth;
        int renderHeight = rotated90 ? imgWidth : imgHeight;

        float drawWidth, drawHeight, x, y;

        if (g_fitToWindow) {
            float scaleX = (float)(width - Scale(20)) / renderWidth;
            float scaleY = (float)(height - Scale(20)) / renderHeight;
            float scale = (std::min)(scaleX, scaleY);
            drawWidth = renderWidth * scale;
            drawHeight = renderHeight * scale;
            x = (width - drawWidth) / 2 + g_panOffset.x;
            y = (height - drawHeight) / 2 + g_panOffset.y;
        } else {
            drawWidth = renderWidth * g_zoom;
            drawHeight = renderHeight * g_zoom;
            x = (width - drawWidth) / 2 + g_panOffset.x;
            y = (height - drawHeight) / 2 + g_panOffset.y;
        }

        graphics.SetSmoothingMode(SmoothingModeHighQuality);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);

        REAL centerX = x + drawWidth / 2;
        REAL centerY = y + drawHeight / 2;
        graphics.TranslateTransform(centerX, centerY);
        graphics.RotateTransform((REAL)g_rotation);
        graphics.TranslateTransform(-centerX, -centerY);

        RectF destRect(centerX - (imgWidth * (drawWidth / renderWidth)) / 2, 
                       centerY - (imgHeight * (drawHeight / renderHeight)) / 2,
                       imgWidth * (drawWidth / renderWidth), 
                       imgHeight * (drawHeight / renderHeight));
        graphics.DrawImage(g_currentImage, destRect);
    } else {
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, g_darkMode ? RGB(200, 200, 200) : RGB(100, 100, 100));
        HFONT font = CreateFontW(Scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(memDC, font);
        std::wstring msg = g_imageFiles.empty() ? L"Drag and drop an image here" : L"Failed to load image";
        RECT textRect = {0, 0, width, height};
        DrawTextW(memDC, msg.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(memDC, oldFont);
        DeleteObject(font);
    }

    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            DragAcceptFiles(hwnd, TRUE);

            g_hMenu = CreateMenu();
            HMENU hOptionsMenu = CreatePopupMenu();

            AppendMenuW(hOptionsMenu, MF_STRING | MF_UNCHECKED, IDM_OPTIONS_DARKMODE, L"Dark Mode\tCtrl+D");
            AppendMenuW(hOptionsMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hOptionsMenu, MF_STRING, IDM_OPTIONS_ABOUT, L"About");

            AppendMenuW(g_hMenu, MF_POPUP, (UINT_PTR)hOptionsMenu, L"Options");
            SetMenu(hwnd, g_hMenu);

            // Create toolbar container window (for buttons only)
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.lpfnWndProc = ToolbarParentWndProc;
            wc.hInstance = ((LPCREATESTRUCT)lParam)->hInstance;
            wc.lpszClassName = L"wpicToolbar";
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            RegisterClassExW(&wc);

            RECT rc;
            GetClientRect(hwnd, &rc);
            int toolbarHeight = Scale(BASE_TOOLBAR_HEIGHT);
            int statusBarHeight = Scale(STATUS_BAR_HEIGHT);
            
            // Status bar at very bottom
            g_hStatusBar = CreateWindowExW(0, L"STATIC", L"  Drop an image to open",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                0, rc.bottom - statusBarHeight, rc.right, statusBarHeight,
                hwnd, nullptr, ((LPCREATESTRUCT)lParam)->hInstance, nullptr);

            g_hToolbar = CreateWindowExW(0, L"wpicToolbar", nullptr,
                WS_CHILD | WS_VISIBLE,
                0, rc.bottom - toolbarHeight - statusBarHeight, rc.right, toolbarHeight,
                hwnd, nullptr, ((LPCREATESTRUCT)lParam)->hInstance, nullptr);

            // Trackbar (zoom slider) — positioned right side of toolbar
            int trackY = rc.bottom - toolbarHeight - statusBarHeight + (toolbarHeight - Scale(TRACKBAR_HEIGHT)) / 2;
            g_hTrackbar = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
                WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                rc.right - Scale(TRACKBAR_WIDTH) - Scale(ZOOM_LABEL_WIDTH) - Scale(15),
                trackY,
                Scale(TRACKBAR_WIDTH),
                Scale(TRACKBAR_HEIGHT),
                hwnd, (HMENU)1010, ((LPCREATESTRUCT)lParam)->hInstance, nullptr);
            
            SendMessageW(g_hTrackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, TRACKBAR_UNITS));
            SendMessageW(g_hTrackbar, TBM_SETPOS, TRUE, ZoomToTrackPos(1.0f));  // 50 = 100%
            // Subclass trackbar to forward mousewheel to main window
            g_origTrackbarProc = (WNDPROC)SetWindowLongPtrW(g_hTrackbar, GWLP_WNDPROC, (LONG_PTR)TrackbarSubclassProc);

            // Zoom % label to the right of trackbar
            g_hZoomLabel = CreateWindowExW(0, L"STATIC", L"100%",
                WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
                rc.right - Scale(ZOOM_LABEL_WIDTH) - Scale(8),
                rc.bottom - toolbarHeight - statusBarHeight + (toolbarHeight - Scale(20)) / 2,
                Scale(ZOOM_LABEL_WIDTH), Scale(20),
                hwnd, nullptr, ((LPCREATESTRUCT)lParam)->hInstance, nullptr);

            InitCache();

            // Set fonts on static controls — use larger readable sizes
            HFONT hStatusFont = CreateFontW(-Scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            if (g_hStatusBar) SendMessageW(g_hStatusBar, WM_SETFONT, (WPARAM)hStatusFont, TRUE);
            HFONT hZoomFont = CreateFontW(-Scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            if (g_hZoomLabel) SendMessageW(g_hZoomLabel, WM_SETFONT, (WPARAM)hZoomFont, TRUE);

            return 0;
        }

        case WM_DPICHANGED: {
            g_dpi = HIWORD(wParam);
            g_dpiScale = g_dpi / 96.0f;
            RECT* prcNew = (RECT*)lParam;
            SetWindowPos(hwnd, NULL, prcNew->left, prcNew->top,
                prcNew->right - prcNew->left,
                prcNew->bottom - prcNew->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            
            SendMessage(hwnd, WM_SIZE, 0, 0);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            wchar_t filePath[MAX_PATH];
            DragQueryFileW(hDrop, 0, filePath, MAX_PATH);
            DragFinish(hDrop);

            StopCacheThread();
            ScanFolder(filePath);
            ClearCache();
            WakeCacheThread();
            StartCacheThread();
            LoadImage(g_currentIndex);
            return 0;
        }

        case WM_PAINT:
            Render(hwnd);
            return 0;

        case WM_SIZE: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int toolbarHeight = Scale(BASE_TOOLBAR_HEIGHT);
            int statusBarHeight = Scale(STATUS_BAR_HEIGHT);
            int trackbarTop = rc.bottom - toolbarHeight - statusBarHeight + (toolbarHeight - Scale(TRACKBAR_HEIGHT)) / 2;
            
            if (g_hStatusBar) {
                SetWindowPos(g_hStatusBar, nullptr, 0, rc.bottom - statusBarHeight,
                    rc.right, statusBarHeight, SWP_NOZORDER);
            }
            if (g_hToolbar) {
                SetWindowPos(g_hToolbar, nullptr, 0, rc.bottom - toolbarHeight - statusBarHeight,
                    rc.right, toolbarHeight, SWP_NOZORDER);
            }
            if (g_hTrackbar) {
                SetWindowPos(g_hTrackbar, nullptr, 
                    rc.right - Scale(TRACKBAR_WIDTH) - Scale(ZOOM_LABEL_WIDTH) - Scale(15),
                    trackbarTop,
                    Scale(TRACKBAR_WIDTH), Scale(TRACKBAR_HEIGHT), SWP_NOZORDER);
            }
            if (g_hZoomLabel) {
                SetWindowPos(g_hZoomLabel, nullptr,
                    rc.right - Scale(ZOOM_LABEL_WIDTH) - Scale(8),
                    rc.bottom - toolbarHeight - statusBarHeight + (toolbarHeight - Scale(20)) / 2,
                    Scale(ZOOM_LABEL_WIDTH), Scale(20), SWP_NOZORDER);
            }
            
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_HSCROLL: {
            if ((HWND)lParam == g_hTrackbar) {
                int pos = (int)SendMessageW(g_hTrackbar, TBM_GETPOS, 0, 0);
                g_zoom = TrackPosToZoom(pos);
                if (g_zoom < 0.01f) g_zoom = 0.01f;
                if (g_zoom > 10.0f) g_zoom = 10.0f;
                g_fitToWindow = false;
                // Update zoom label
                if (g_hZoomLabel) {
                    wchar_t buf[16];
                    int pct = (int)(g_zoom * 100 + 0.5f);
                    wsprintfW(buf, L"%d%%", pct);
                    SetWindowTextW(g_hZoomLabel, buf);
                }
                UpdateStatusBar();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_BTN_PREV: PrevImage(); break;
                case ID_BTN_NEXT: NextImage(); break;
                case ID_BTN_ZOOM_IN: ZoomIn(); break;
                case ID_BTN_ZOOM_OUT: ZoomOut(); break;
                case ID_BTN_FIT: ToggleZoom(); break;
                case ID_BTN_ACTUAL: ActualSize(); break;
                case ID_BTN_ROTATE_LEFT: RotateLeft(); break;
                case ID_BTN_ROTATE_RIGHT: RotateRight(); break;
                case ID_BTN_DELETE: DeleteCurrentImage(); break;
                case IDM_OPTIONS_DARKMODE: ToggleDarkMode(); break;
                case IDM_OPTIONS_ABOUT: ShowAboutDialog(); break;
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            if (hCtrl == g_hZoomLabel) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, g_darkMode ? RGB(180, 180, 180) : RGB(80, 80, 80));
                // Return toolbar background brush
                static HBRUSH hBrLight = nullptr, hBrDark = nullptr;
                if (g_darkMode) {
                    if (!hBrDark) hBrDark = CreateSolidBrush(RGB(45, 45, 45));
                    return (LRESULT)hBrDark;
                } else {
                    if (!hBrLight) hBrLight = CreateSolidBrush(RGB(245, 245, 245));
                    return (LRESULT)hBrLight;
                }
            }
            if (hCtrl == g_hStatusBar) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, g_darkMode ? RGB(160, 160, 160) : RGB(100, 100, 100));
                static HBRUSH hBrStatusLight = nullptr, hBrStatusDark = nullptr;
                if (g_darkMode) {
                    if (!hBrStatusDark) hBrStatusDark = CreateSolidBrush(RGB(35, 35, 35));
                    return (LRESULT)hBrStatusDark;
                } else {
                    if (!hBrStatusLight) hBrStatusLight = CreateSolidBrush(RGB(230, 230, 230));
                    return (LRESULT)hBrStatusLight;
                }
            }
            break;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            // Get mouse position relative to client window
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hwnd, &pt);

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int viewHeight = clientRect.bottom - Scale(BASE_TOOLBAR_HEIGHT) - Scale(STATUS_BAR_HEIGHT);

            // Only zoom-to-cursor when not in fit mode
            float oldZoom = g_zoom;
            float newZoom;
            if (delta > 0) {
                newZoom = (std::min)(oldZoom * 1.15f, 10.0f);
            } else {
                newZoom = (std::max)(oldZoom / 1.15f, 0.05f);
            }

            if (g_fitToWindow) {
                // Exit fit mode, compute the actual rendered scale first
                if (g_currentImage) {
                    bool rotated90 = (g_rotation == 90 || g_rotation == 270);
                    int imgW = g_currentImage->GetWidth();
                    int imgH = g_currentImage->GetHeight();
                    int rW = rotated90 ? imgH : imgW;
                    int rH = rotated90 ? imgW : imgH;
                    int viewW = clientRect.right;
                    float scaleX = (float)(viewW - Scale(20)) / rW;
                    float scaleY = (float)(viewHeight - Scale(20)) / rH;
                    oldZoom = (std::min)(scaleX, scaleY);
                }
                g_fitToWindow = false;
                newZoom = (delta > 0) ? (std::min)(oldZoom * 1.15f, 10.0f)
                                      : (std::max)(oldZoom / 1.15f, 0.05f);
            }

            // Zoom toward cursor: adjust pan so point under cursor stays fixed
            float zoomRatio = newZoom / oldZoom;
            // Current image center offset from view center
            int cx = clientRect.right / 2;
            int cy = viewHeight / 2;
            g_panOffset.x = (int)((g_panOffset.x - (pt.x - cx)) * zoomRatio + (pt.x - cx));
            g_panOffset.y = (int)((g_panOffset.y - (pt.y - cy)) * zoomRatio + (pt.y - cy));

            g_zoom = newZoom;
            UpdateTrackbarPos();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            g_isPanning = true;
            g_lastMousePos.x = LOWORD(lParam);
            g_lastMousePos.y = HIWORD(lParam);
            SetCapture(hwnd);
            return 0;
        }

        case WM_LBUTTONUP:
            g_isPanning = false;
            ReleaseCapture();
            return 0;

        case WM_MOUSEMOVE: {
            if (g_isPanning) {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                g_panOffset.x += x - g_lastMousePos.x;
                g_panOffset.y += y - g_lastMousePos.y;
                g_lastMousePos.x = x;
                g_lastMousePos.y = y;
                g_fitToWindow = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_KEYDOWN:
            switch (wParam) {
                case VK_RIGHT: case VK_SPACE: NextImage(); break;
                case VK_LEFT: case VK_BACK: PrevImage(); break;
                case VK_UP: ZoomIn(); break;
                case VK_DOWN: ZoomOut(); break;
                case 'F': ToggleZoom(); break;
                case 'R': g_rotation = 0; InvalidateRect(hwnd, nullptr, FALSE); break;
                case 'D': 
                    if (GetAsyncKeyState(VK_CONTROL) < 0) {
                        ToggleDarkMode();
                    }
                    break;
                case VK_DELETE: DeleteCurrentImage(); break;
                case VK_ESCAPE: 
                    StopCacheThread();
                    PostQuitMessage(0); 
                    break;
            }
            return 0;

        case WM_DESTROY:
            StopCacheThread();
            DeleteCriticalSection(&g_cacheLock);
            if (g_hMenu) DestroyMenu(g_hMenu);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLineAnsi, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    // Set DPI awareness
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        SetProcessDpiAwarenessContextFunc SetProcessDpiAwarenessContext = 
            (SetProcessDpiAwarenessContextFunc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        
        if (SetProcessDpiAwarenessContext) {
            SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDPIAwareFunc SetProcessDPIAware = 
                (SetProcessDPIAwareFunc)GetProcAddress(hUser32, "SetProcessDPIAware");
            if (SetProcessDPIAware) {
                SetProcessDPIAware();
            }
        }
        
        GetDpiForSystemFunc GetDpiForSystem = 
            (GetDpiForSystemFunc)GetProcAddress(hUser32, "GetDpiForSystem");
        if (GetDpiForSystem) {
            g_dpi = GetDpiForSystem();
            g_dpiScale = g_dpi / 96.0f;
        }
    }

    // Initialize common controls (required for trackbar)
    INITCOMMONCONTROLSEX iccex;
    iccex.dwSize = sizeof(iccex);
    iccex.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&iccex);

    int wideLen = MultiByteToWideChar(CP_ACP, 0, lpCmdLineAnsi, -1, nullptr, 0);
    std::vector<wchar_t> wideBuffer(wideLen);
    MultiByteToWideChar(CP_ACP, 0, lpCmdLineAnsi, -1, wideBuffer.data(), wideLen);
    LPWSTR lpCmdLine = wideBuffer.data();

    HICON hIcon = LoadIconW(hInstance, L"IDI_APPLICATION");
    if (!hIcon) hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!hIcon) hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"wpicWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = hIcon;
    wc.hIconSm = hIcon;
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"wpicWindowClass",
        L"wpic",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, Scale(1000), Scale(700),
        nullptr, nullptr, hInstance, nullptr
    );

    if (lpCmdLine && wcslen(lpCmdLine) > 0) {
        std::wstring cmdLine = lpCmdLine;
        if (cmdLine.front() == L'"' && cmdLine.back() == L'"') {
            cmdLine = cmdLine.substr(1, cmdLine.length() - 2);
        }
        ScanFolder(cmdLine);
        WakeCacheThread();
        StartCacheThread();
        LoadImage(g_currentIndex);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_currentImage) delete g_currentImage;
    GdiplusShutdown(gdiplusToken);
    return 0;
}