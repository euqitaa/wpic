// wpic - Minimal Image Viewer for Windows
// Build: g++ -O2 -s wpic.cpp -o wpic.exe -mwindows -static -lgdiplus -luser32 -lgdi32 -lshell32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#ifndef _WIN32_WCE
#include <wtypes.h>
#endif

#include <gdiplus.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

using namespace Gdiplus;

// Menu IDs
#define IDM_OPTIONS_DARKMODE 2001
#define IDM_OPTIONS_ABOUT 2002

// Toolbar constants
#define TOOLBAR_HEIGHT 60
#define BUTTON_HEIGHT 46

// Button IDs
#define ID_BTN_PREV 1001
#define ID_BTN_NEXT 1002
#define ID_BTN_ZOOM_OUT 1003
#define ID_BTN_ZOOM_IN 1004
#define ID_BTN_FIT 1005
#define ID_BTN_ACTUAL 1006
#define ID_BTN_ROTATE_LEFT 1007
#define ID_BTN_ROTATE_RIGHT 1008
#define ID_BTN_DELETE 1009

// Button layout
struct ButtonDef {
    int id;
    const wchar_t* symbol;
    const wchar_t* label;
    int x;
    int width;
};

// Global state
HWND g_hwnd = nullptr;
HWND g_hToolbar = nullptr;
HMENU g_hMenu = nullptr;
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

    for (size_t i = 0; i < g_imageFiles.size(); ++i) {
        if (g_imageFiles[i] == targetFile) {
            g_currentIndex = i;
            break;
        }
    }
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
    g_rotation = 0;
    UpdateTitle();
    InvalidateRect(g_hwnd, nullptr, FALSE);
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
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ZoomIn() {
    g_fitToWindow = false;
    g_zoom = (std::min)(g_zoom * 1.25f, 10.0f);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ZoomOut() {
    g_fitToWindow = false;
    g_zoom = (std::max)(g_zoom / 1.25f, 0.1f);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ActualSize() {
    g_fitToWindow = false;
    g_zoom = 1.0f;
    g_panOffset = {0, 0};
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

    // Update menu checkmark
    if (g_hMenu) {
        HMENU hOptionsMenu = GetSubMenu(g_hMenu, 0);
        if (hOptionsMenu) {
            CheckMenuItem(hOptionsMenu, IDM_OPTIONS_DARKMODE, 
                g_darkMode ? MF_CHECKED : MF_UNCHECKED);
        }
    }

    InvalidateRect(g_hwnd, nullptr, FALSE);
    if (g_hToolbar) InvalidateRect(g_hToolbar, nullptr, FALSE);
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

// About dialog window procedure
LRESULT CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Set dark background if in dark mode
            if (g_darkMode) {
                SetWindowLongW(hwnd, GWL_EXSTYLE, GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_COMPOSITED);
            }

            // Title
            HWND hTitle = CreateWindowExW(0, L"STATIC", L"wpic",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 20, 400, 40, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"), TRUE);

            // Version
            HWND hVersion = CreateWindowExW(0, L"STATIC", L"Version 0.5",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 65, 400, 25, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hVersion, WM_SETFONT, (WPARAM)CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"), TRUE);

            // Author
            HWND hAuthor = CreateWindowExW(0, L"STATIC", L"by euqitaa",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 95, 400, 25, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hAuthor, WM_SETFONT, (WPARAM)CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"), TRUE);

            // GitHub link
            HWND hLink = CreateWindowExW(0, L"STATIC", L"https://github.com/euqitaa/wpic",
                WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY,
                50, 140, 300, 30, hwnd, (HMENU)1001, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hLink, WM_SETFONT, (WPARAM)CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"), TRUE);

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
            if (LOWORD(wParam) == 1001) { // Link clicked
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

// Custom About dialog
void ShowAboutDialog() {
    // Register dialog class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = AboutDlgProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"wpicAboutDlg";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    // Create dialog window
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"wpicAboutDlg", L"About wpic",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 220,
        g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!hDlg) return;

    // Center dialog
    RECT rcDlg, rcOwner;
    GetWindowRect(hDlg, &rcDlg);
    GetWindowRect(g_hwnd, &rcOwner);
    int x = rcOwner.left + (rcOwner.right - rcOwner.left - (rcDlg.right - rcDlg.left)) / 2;
    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - (rcDlg.bottom - rcDlg.top)) / 2;
    SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

    // Disable parent window
    EnableWindow(g_hwnd, FALSE);
}

// Calculate button layout
void GetButtonLayout(int windowWidth, ButtonDef* buttons, int& count) {
    count = 9;

    buttons[0] = {ID_BTN_PREV, L"◄", L"Previous", 0, 80};
    buttons[1] = {ID_BTN_NEXT, L"►", L"Next", 0, 60};
    buttons[2] = {ID_BTN_ZOOM_OUT, L"⊖", L"Zoom out", 0, 80};
    buttons[3] = {ID_BTN_ZOOM_IN, L"⊕", L"Zoom in", 0, 70};
    buttons[4] = {ID_BTN_FIT, L"□", L"Fit to window", 0, 100};
    buttons[5] = {ID_BTN_ACTUAL, L"1:1", L"Actual size", 0, 80};
    buttons[6] = {ID_BTN_ROTATE_LEFT, L"↺", L"Rotate left", 0, 90};
    buttons[7] = {ID_BTN_ROTATE_RIGHT, L"↻", L"Rotate right", 0, 95};
    buttons[8] = {ID_BTN_DELETE, L"✕", L"Delete", 0, 65};

    int totalWidth = 10;
    for (int i = 0; i < count; i++) {
        totalWidth += buttons[i].width + 4;
        if (i == 1 || i == 5 || i == 7) totalWidth += 20;
    }

    int startX = (windowWidth - totalWidth) / 2;
    if (startX < 10) startX = 10;

    int x = startX;
    for (int i = 0; i < count; i++) {
        buttons[i].x = x;
        x += buttons[i].width + 4;
        if (i == 1 || i == 5 || i == 7) x += 20;
    }
}

// Toolbar window procedure
LRESULT CALLBACK ToolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int hoveredBtn = -1;
    static int pressedBtn = -1;
    static ButtonDef buttons[9];
    static int btnCount = 0;

    switch (msg) {
        case WM_CREATE: {
            RECT rc;
            GetClientRect(GetParent(hwnd), &rc);
            GetButtonLayout(rc.right, buttons, btnCount);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);

            // Background - dark or light
            COLORREF bgColor = g_darkMode ? RGB(45, 45, 45) : RGB(245, 245, 245);
            HBRUSH bgBrush = CreateSolidBrush(bgColor);
            FillRect(hdc, &rc, bgBrush);
            DeleteObject(bgBrush);

            // Top border
            COLORREF borderColor = g_darkMode ? RGB(80, 80, 80) : RGB(200, 200, 200);
            HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, 0, 0, nullptr);
            LineTo(hdc, rc.right, 0);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);

            RECT parentRc;
            GetClientRect(GetParent(hwnd), &parentRc);
            GetButtonLayout(parentRc.right, buttons, btnCount);

            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            // Text colors
            COLORREF textColor = g_darkMode ? RGB(220, 220, 220) : RGB(60, 60, 60);
            COLORREF labelColor = g_darkMode ? RGB(180, 180, 180) : RGB(80, 80, 80);

            for (int i = 0; i < btnCount; i++) {
                RECT btnRc = {buttons[i].x, 8, buttons[i].x + buttons[i].width, 8 + BUTTON_HEIGHT};
                bool hovered = (pt.x >= btnRc.left && pt.x < btnRc.right && pt.y >= btnRc.top && pt.y < btnRc.bottom);
                bool pressed = (pressedBtn == buttons[i].id);

                // Button background
                COLORREF btnBgColor;
                if (pressed) btnBgColor = g_darkMode ? RGB(70, 70, 70) : RGB(200, 200, 200);
                else if (hovered) btnBgColor = g_darkMode ? RGB(60, 60, 60) : RGB(230, 230, 230);
                else btnBgColor = bgColor;

                HBRUSH brush = CreateSolidBrush(btnBgColor);
                FillRect(hdc, &btnRc, brush);
                DeleteObject(brush);

                // Border
                if (hovered || pressed) {
                    COLORREF btnBorderColor = g_darkMode ? RGB(100, 100, 100) : RGB(180, 180, 180);
                    HPEN borderPen = CreatePen(PS_SOLID, 1, btnBorderColor);
                    HPEN oldBorderPen = (HPEN)SelectObject(hdc, borderPen);
                    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    Rectangle(hdc, btnRc.left, btnRc.top, btnRc.right, btnRc.bottom);
                    SelectObject(hdc, oldBorderPen);
                    SelectObject(hdc, oldBrush);
                    DeleteObject(borderPen);
                }

                // Separator
                if (i == 1 || i == 5 || i == 7) {
                    int sepX = btnRc.right + 10;
                    COLORREF sepPenColor = g_darkMode ? RGB(80, 80, 80) : RGB(200, 200, 200);
                    HPEN sepPen = CreatePen(PS_SOLID, 1, sepPenColor);
                    HPEN oldSepPen = (HPEN)SelectObject(hdc, sepPen);
                    MoveToEx(hdc, sepX, 14, nullptr);
                    LineTo(hdc, sepX, TOOLBAR_HEIGHT - 14);
                    SelectObject(hdc, oldSepPen);
                    DeleteObject(sepPen);
                }

                // Symbol
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, textColor);
                HFONT symbolFont = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI Symbol");
                HFONT oldFont = (HFONT)SelectObject(hdc, symbolFont);

                RECT symbolRect = {btnRc.left, btnRc.top + 3, btnRc.right, btnRc.top + 28};
                DrawTextW(hdc, buttons[i].symbol, -1, &symbolRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdc, oldFont);
                DeleteObject(symbolFont);

                // Label
                SetTextColor(hdc, labelColor);
                HFONT labelFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
                oldFont = (HFONT)SelectObject(hdc, labelFont);

                RECT labelRect = {btnRc.left, btnRc.top + 26, btnRc.right, btnRc.bottom - 3};
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

            int newHover = -1;
            for (int i = 0; i < btnCount; i++) {
                if (x >= buttons[i].x && x < buttons[i].x + buttons[i].width && y >= 8 && y < 8 + BUTTON_HEIGHT) {
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

            for (int i = 0; i < btnCount; i++) {
                if (x >= buttons[i].x && x < buttons[i].x + buttons[i].width && y >= 8 && y < 8 + BUTTON_HEIGHT) {
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
            GetButtonLayout(parentRc.right, buttons, btnCount);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Render(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right;
    int height = clientRect.bottom - TOOLBAR_HEIGHT;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // Dark or light background
    Color bgColor = g_darkMode ? Color(30, 30, 30) : Color(240, 240, 240);
    Graphics graphics(memDC);
    graphics.Clear(bgColor);

    if (g_currentImage && g_currentImage->GetLastStatus() == Ok) {
        int imgWidth = g_currentImage->GetWidth();
        int imgHeight = g_currentImage->GetHeight();

        bool rotated90 = (g_rotation == 90 || g_rotation == 270);
        int renderWidth = rotated90 ? imgHeight : imgWidth;
        int renderHeight = rotated90 ? imgWidth : imgHeight;

        float drawWidth, drawHeight, x, y;

        if (g_fitToWindow) {
            float scaleX = (float)(width - 20) / renderWidth;
            float scaleY = (float)(height - 20) / renderHeight;
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
        HFONT font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
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

            // Create menu bar
            g_hMenu = CreateMenu();
            HMENU hOptionsMenu = CreatePopupMenu();

            AppendMenuW(hOptionsMenu, MF_STRING | MF_UNCHECKED, IDM_OPTIONS_DARKMODE, L"Dark Mode\tCtrl+D");
            AppendMenuW(hOptionsMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hOptionsMenu, MF_STRING, IDM_OPTIONS_ABOUT, L"About");

            AppendMenuW(g_hMenu, MF_POPUP, (UINT_PTR)hOptionsMenu, L"Options");
            SetMenu(hwnd, g_hMenu);

            // Register toolbar class
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.lpfnWndProc = ToolbarWndProc;
            wc.hInstance = ((LPCREATESTRUCT)lParam)->hInstance;
            wc.lpszClassName = L"wpicToolbar";
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            RegisterClassExW(&wc);

            // Create toolbar
            RECT rc;
            GetClientRect(hwnd, &rc);
            g_hToolbar = CreateWindowExW(0, L"wpicToolbar", nullptr,
                WS_CHILD | WS_VISIBLE,
                0, rc.bottom - TOOLBAR_HEIGHT, rc.right, TOOLBAR_HEIGHT,
                hwnd, nullptr, ((LPCREATESTRUCT)lParam)->hInstance, nullptr);

            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

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

        case WM_SIZE: {
            if (g_hToolbar) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                SetWindowPos(g_hToolbar, nullptr, 0, rc.bottom - TOOLBAR_HEIGHT, rc.right, TOOLBAR_HEIGHT, SWP_NOZORDER);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
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

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta > 0) ZoomIn();
            else ZoomOut();
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
                case VK_ESCAPE: PostQuitMessage(0); break;
            }
            return 0;

        case WM_DESTROY:
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

    // Convert ANSI command line to wide char to maintain compatibility with existing code
    int wideLen = MultiByteToWideChar(CP_ACP, 0, lpCmdLineAnsi, -1, nullptr, 0);
    std::vector<wchar_t> wideBuffer(wideLen);
    MultiByteToWideChar(CP_ACP, 0, lpCmdLineAnsi, -1, wideBuffer.data(), wideLen);
    LPWSTR lpCmdLine = wideBuffer.data();

    // Load icon
    HICON hIcon = LoadIconW(hInstance, L"IDI_APPLICATION");
    if (!hIcon) hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!hIcon) hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION = 32512

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
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
        nullptr, nullptr, hInstance, nullptr
    );

    if (lpCmdLine && wcslen(lpCmdLine) > 0) {
        std::wstring cmdLine = lpCmdLine;
        if (cmdLine.front() == L'"' && cmdLine.back() == L'"') {
            cmdLine = cmdLine.substr(1, cmdLine.length() - 2);
        }
        ScanFolder(cmdLine);
        LoadImage(0);
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