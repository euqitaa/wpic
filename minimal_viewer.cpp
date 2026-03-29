
// Minimal Image Viewer for Windows
// Build: cl.exe minimal_viewer.cpp /O2 /link gdiplus.lib user32.lib gdi32.lib shell32.lib
// Or use MinGW: g++ -O2 -s minimal_viewer.cpp -o minimal_viewer.exe -lgdiplus -luser32 -lgdi32 -lshell32 -mwindows

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// Fix for MinGW missing PROPID definition
#ifndef _WIN32_WCE
#include <wtypes.h>
#endif

#include <gdiplus.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

using namespace Gdiplus;

// Global state
HWND g_hwnd = nullptr;
std::vector<std::wstring> g_imageFiles;
std::wstring g_currentFolder;
size_t g_currentIndex = 0;
Image* g_currentImage = nullptr;
float g_zoom = 1.0f;
bool g_fitToWindow = true;
POINT g_panOffset = {0, 0};
bool g_isDragging = false;
POINT g_lastMousePos = {0, 0};
bool g_darkMode = true;

// Supported extensions
const wchar_t* g_supportedExts[] = {L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".tiff", L".tif", L".webp", nullptr};

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

    // Find current index
    for (size_t i = 0; i < g_imageFiles.size(); ++i) {
        if (g_imageFiles[i] == targetFile) {
            g_currentIndex = i;
            break;
        }
    }
}

void LoadImage(size_t index) {
    if (g_currentImage) {
        delete g_currentImage;
        g_currentImage = nullptr;
    }
    if (index >= g_imageFiles.size()) return;

    g_currentIndex = index;
    g_currentImage = new Image(g_imageFiles[index].c_str());
    g_zoom = 1.0f;
    g_panOffset = {0, 0};
    g_fitToWindow = true;
    InvalidateRect(g_hwnd, nullptr, TRUE);

    // Update title
    std::wstring title = L"Minimal Viewer - " + g_imageFiles[g_currentIndex].substr(g_imageFiles[g_currentIndex].find_last_of(L'\\') + 1);
    title += L" (" + std::to_wstring(g_currentIndex + 1) + L"/" + std::to_wstring(g_imageFiles.size()) + L")";
    SetWindowTextW(g_hwnd, title.c_str());
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
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void ZoomIn() {
    g_fitToWindow = false;
    g_zoom = (std::min)(g_zoom * 1.25f, 10.0f);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void ZoomOut() {
    g_fitToWindow = false;
    g_zoom = (std::max)(g_zoom / 1.25f, 0.1f);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void ResetView() {
    g_fitToWindow = true;
    g_zoom = 1.0f;
    g_panOffset = {0, 0};
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

void Render(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    // Double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // Background
    Graphics graphics(memDC);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);

    Color bgColor = g_darkMode ? Color(30, 30, 30) : Color(240, 240, 240);
    graphics.Clear(bgColor);

    if (g_currentImage && g_currentImage->GetLastStatus() == Ok) {
        int imgWidth = g_currentImage->GetWidth();
        int imgHeight = g_currentImage->GetHeight();

        float drawWidth, drawHeight, x, y;

        if (g_fitToWindow) {
            float scaleX = (float)(width - 40) / imgWidth;
            float scaleY = (float)(height - 40) / imgHeight;
            float scale = (std::min)(scaleX, scaleY);
            drawWidth = imgWidth * scale;
            drawHeight = imgHeight * scale;
            x = (width - drawWidth) / 2 + g_panOffset.x;
            y = (height - drawHeight) / 2 + g_panOffset.y;
        } else {
            drawWidth = imgWidth * g_zoom;
            drawHeight = imgHeight * g_zoom;
            x = (width - drawWidth) / 2 + g_panOffset.x;
            y = (height - drawHeight) / 2 + g_panOffset.y;
        }

        // Draw shadow
        if (g_darkMode) {
            RectF shadowRect(x - 5, y - 5, drawWidth + 10, drawHeight + 10);
            SolidBrush shadowBrush(Color(60, 0, 0, 0));
            graphics.FillRectangle(&shadowBrush, shadowRect);
        }

        RectF destRect(x, y, drawWidth, drawHeight);
        graphics.DrawImage(g_currentImage, destRect);
    } else {
        // No image or error
        SolidBrush textBrush(g_darkMode ? Color(200, 200, 200) : Color(100, 100, 100));
        Font font(L"Segoe UI", 14);
        std::wstring msg = g_imageFiles.empty() ? L"Drag and drop an image here" : L"Failed to load image";
        RectF layoutRect(0, 0, (REAL)width, (REAL)height);
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        graphics.DrawString(msg.c_str(), -1, &font, layoutRect, &format, &textBrush);
    }

    // Copy to screen
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            DragAcceptFiles(hwnd, TRUE);
            return 0;

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            wchar_t filePath[MAX_PATH];
            DragQueryFileW(hDrop, 0, filePath, MAX_PATH);
            DragFinish(hDrop);

            ScanFolder(filePath);
            LoadImage(0);
            return 0;
        }

        case WM_PAINT:
            Render(hwnd);
            return 0;

        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta > 0) ZoomIn();
            else ZoomOut();
            return 0;
        }

        case WM_LBUTTONDOWN:
            g_isDragging = true;
            g_lastMousePos.x = LOWORD(lParam);
            g_lastMousePos.y = HIWORD(lParam);
            SetCapture(hwnd);
            return 0;

        case WM_LBUTTONUP:
            g_isDragging = false;
            ReleaseCapture();
            return 0;

        case WM_MOUSEMOVE:
            if (g_isDragging) {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                g_panOffset.x += x - g_lastMousePos.x;
                g_panOffset.y += y - g_lastMousePos.y;
                g_lastMousePos.x = x;
                g_lastMousePos.y = y;
                g_fitToWindow = false;
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;

        case WM_KEYDOWN:
            switch (wParam) {
                case VK_RIGHT: case VK_SPACE: NextImage(); break;
                case VK_LEFT: case VK_BACK: PrevImage(); break;
                case VK_UP: ZoomIn(); break;
                case VK_DOWN: ZoomOut(); break;
                case 'F': ToggleZoom(); break;
                case 'R': ResetView(); break;
                case 'D': g_darkMode = !g_darkMode; InvalidateRect(hwnd, nullptr, TRUE); break;
                case VK_ESCAPE: PostQuitMessage(0); break;
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MinimalImageViewer";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    // Create window
    g_hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE | WS_EX_ACCEPTFILES,
        L"MinimalImageViewer",
        L"Minimal Image Viewer",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        nullptr, nullptr, hInstance, nullptr
    );

    // Load file from command line if provided
    if (lpCmdLine && wcslen(lpCmdLine) > 0) {
        std::wstring cmdLine = lpCmdLine;
        // Remove quotes if present
        if (cmdLine.front() == L'"' && cmdLine.back() == L'"') {
            cmdLine = cmdLine.substr(1, cmdLine.length() - 2);
        }
        ScanFolder(cmdLine);
        LoadImage(0);
    }

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    if (g_currentImage) delete g_currentImage;
    GdiplusShutdown(gdiplusToken);
    return 0;
}