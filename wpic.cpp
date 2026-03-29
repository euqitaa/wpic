
// Minimal Image Viewer for Windows
// Build: cl.exe minimal_viewer.cpp /O2 /link gdiplus.lib user32.lib gdi32.lib shell32.lib comctl32.lib
// Or use MinGW: g++ -O2 -s minimal_viewer.cpp -o minimal_viewer.exe -municode -lgdiplus -luser32 -lgdi32 -lshell32 -lcomctl32 -mwindows

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

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
#pragma comment(lib, "comctl32.lib")

using namespace Gdiplus;

// Custom title bar height
#define TITLE_BAR_HEIGHT 32
#define BUTTON_WIDTH 45

// Global state
HWND g_hwnd = nullptr;
HWND g_btnPrev = nullptr;
HWND g_btnNext = nullptr;
HWND g_btnZoomIn = nullptr;
HWND g_btnZoomOut = nullptr;
HWND g_btnFit = nullptr;
HWND g_btnClose = nullptr;
std::vector<std::wstring> g_imageFiles;
std::wstring g_currentFolder;
size_t g_currentIndex = 0;
Image* g_currentImage = nullptr;
float g_zoom = 1.0f;
bool g_fitToWindow = true;
POINT g_panOffset = {0, 0};
bool g_isDragging = false;
bool g_isPanning = false;
POINT g_lastMousePos = {0, 0};
bool g_darkMode = true;
bool g_isMaximized = false;
RECT g_normalRect = {0};

// Button IDs
#define ID_BTN_PREV 1001
#define ID_BTN_NEXT 1002
#define ID_BTN_ZOOM_IN 1003
#define ID_BTN_ZOOM_OUT 1004
#define ID_BTN_FIT 1005
#define ID_BTN_CLOSE 1006
#define ID_BTN_MINIMIZE 1007
#define ID_BTN_MAXIMIZE 1008

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
        SetWindowTextW(g_hwnd, L"Minimal Image Viewer");
        return;
    }
    std::wstring filename = g_imageFiles[g_currentIndex].substr(g_imageFiles[g_currentIndex].find_last_of(L'\\') + 1);
    std::wstring title = filename + L" - " + std::to_wstring(g_currentIndex + 1) + L"/" + std::to_wstring(g_imageFiles.size());
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

void ResetView() {
    g_fitToWindow = true;
    g_zoom = 1.0f;
    g_panOffset = {0, 0};
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

// Create toolbar buttons with Windows system icons
void CreateToolbarButtons(HWND hwnd, HINSTANCE hInstance) {
    // Get system metrics for high DPI
    int btnHeight = TITLE_BAR_HEIGHT - 4;
    int btnY = 2;

    // Navigation buttons (left side)
    g_btnPrev = CreateWindowW(L"BUTTON", L"", 
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_FLAT,
        5, btnY, BUTTON_WIDTH, btnHeight,
        hwnd, (HMENU)ID_BTN_PREV, hInstance, nullptr);

    g_btnNext = CreateWindowW(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_FLAT,
        5 + BUTTON_WIDTH + 2, btnY, BUTTON_WIDTH, btnHeight,
        hwnd, (HMENU)ID_BTN_NEXT, hInstance, nullptr);

    // Zoom buttons (left of center)
    int zoomStart = 5 + (BUTTON_WIDTH + 2) * 2 + 20;
    g_btnZoomOut = CreateWindowW(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_FLAT,
        zoomStart, btnY, BUTTON_WIDTH, btnHeight,
        hwnd, (HMENU)ID_BTN_ZOOM_OUT, hInstance, nullptr);

    g_btnFit = CreateWindowW(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_FLAT,
        zoomStart + BUTTON_WIDTH + 2, btnY, BUTTON_WIDTH, btnHeight,
        hwnd, (HMENU)ID_BTN_FIT, hInstance, nullptr);

    g_btnZoomIn = CreateWindowW(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_FLAT,
        zoomStart + (BUTTON_WIDTH + 2) * 2, btnY, BUTTON_WIDTH, btnHeight,
        hwnd, (HMENU)ID_BTN_ZOOM_IN, hInstance, nullptr);
}

// Draw button with system icon
void DrawToolbarButton(LPDRAWITEMSTRUCT dis, int iconId) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED);
    bool hovered = (dis->itemState & ODS_HOTLIGHT) || (GetAsyncKeyState(VK_LBUTTON) < 0 && pressed);

    // Background
    COLORREF bgColor = g_darkMode ? RGB(45, 45, 45) : RGB(240, 240, 240);
    COLORREF hoverColor = g_darkMode ? RGB(60, 60, 60) : RGB(220, 220, 220);
    COLORREF pressedColor = g_darkMode ? RGB(80, 80, 80) : RGB(200, 200, 200);

    HBRUSH brush = CreateSolidBrush(pressed ? pressedColor : (hovered ? hoverColor : bgColor));
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);

    // Draw icon from shell32.dll or user32.dll
    HICON hIcon = nullptr;
    int iconSize = 16;

    // Load system icons
    switch (iconId) {
        case ID_BTN_PREV:
            hIcon = (HICON)LoadImage(nullptr, MAKEINTRESOURCE(32512), IMAGE_ICON, iconSize, iconSize, LR_SHARED); // IDI_APPLICATION as fallback
            // Use arrow left from shell32
            hIcon = (HICON)LoadImageW(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEW(16754), IMAGE_ICON, iconSize, iconSize, LR_DEFAULTCOLOR);
            break;
        case ID_BTN_NEXT:
            hIcon = (HICON)LoadImageW(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEW(16755), IMAGE_ICON, iconSize, iconSize, LR_DEFAULTCOLOR);
            break;
        case ID_BTN_ZOOM_IN:
            hIcon = (HICON)LoadImageW(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEW(16762), IMAGE_ICON, iconSize, iconSize, LR_DEFAULTCOLOR);
            break;
        case ID_BTN_ZOOM_OUT:
            hIcon = (HICON)LoadImageW(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEW(16763), IMAGE_ICON, iconSize, iconSize, LR_DEFAULTCOLOR);
            break;
        case ID_BTN_FIT:
            hIcon = (HICON)LoadImageW(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEW(16739), IMAGE_ICON, iconSize, iconSize, LR_DEFAULTCOLOR);
            break;
    }

    // Fallback: draw text if icon not found
    if (hIcon) {
        DrawIconEx(hdc, (rc.right - rc.left - iconSize) / 2, (rc.bottom - rc.top - iconSize) / 2, hIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    } else {
        // Draw text fallback
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_darkMode ? RGB(200, 200, 200) : RGB(50, 50, 50));
        HFONT font = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(hdc, font);

        const wchar_t* text = L"?";
        switch (iconId) {
            case ID_BTN_PREV: text = L"<"; break;
            case ID_BTN_NEXT: text = L">"; break;
            case ID_BTN_ZOOM_IN: text = L"+"; break;
            case ID_BTN_ZOOM_OUT: text = L"-"; break;
            case ID_BTN_FIT: text = L"[]"; break;
        }
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(font);
    }
}

// Custom title bar drawing
void DrawCustomTitleBar(HDC hdc, HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    // Title bar background
    COLORREF titleBarColor = g_darkMode ? RGB(32, 32, 32) : RGB(240, 240, 240);
    RECT titleRect = {0, 0, rc.right, TITLE_BAR_HEIGHT};
    HBRUSH brush = CreateSolidBrush(titleBarColor);
    FillRect(hdc, &titleRect, brush);
    DeleteObject(brush);

    // Draw title centered
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, g_darkMode ? RGB(255, 255, 255) : RGB(0, 0, 0));

    HFONT font = CreateFontW(13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(hdc, font);

    wchar_t title[256];
    GetWindowTextW(hwnd, title, 256);

    // Calculate text position (centered)
    SIZE textSize;
    GetTextExtentPoint32W(hdc, title, wcslen(title), &textSize);
    int textX = (rc.right - textSize.cx) / 2;
    int textY = (TITLE_BAR_HEIGHT - textSize.cy) / 2;

    // Ensure text doesn't overlap with buttons
    int leftMargin = 200;
    int rightMargin = 150;
    if (textX < leftMargin) textX = leftMargin;
    if (textX + textSize.cx > rc.right - rightMargin) textX = rc.right - rightMargin - textSize.cx;

    RECT textRect = {textX, textY, textX + textSize.cx, textY + textSize.cy};
    DrawTextW(hdc, title, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldFont);
    DeleteObject(font);

    // Draw window control buttons (right side)
    int btnY = 2;
    int btnSize = TITLE_BAR_HEIGHT - 4;
    int closeX = rc.right - btnSize - 5;
    int maxX = closeX - btnSize - 2;
    int minX = maxX - btnSize - 2;

    // Minimize button
    RECT minRect = {minX, btnY, minX + btnSize, btnY + btnSize};
    FillRect(hdc, &minRect, CreateSolidBrush(g_darkMode ? RGB(45, 45, 45) : RGB(240, 240, 240)));
    SetTextColor(hdc, g_darkMode ? RGB(200, 200, 200) : RGB(50, 50, 50));
    DrawTextW(hdc, L"_", -1, &minRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Maximize/Restore button
    RECT maxRect = {maxX, btnY, maxX + btnSize, btnY + btnSize};
    FillRect(hdc, &maxRect, CreateSolidBrush(g_darkMode ? RGB(45, 45, 45) : RGB(240, 240, 240)));
    DrawTextW(hdc, g_isMaximized ? L"❐" : L"□", -1, &maxRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Close button
    RECT closeRect = {closeX, btnY, closeX + btnSize, btnY + btnSize};
    HBRUSH closeBrush = CreateSolidBrush(RGB(196, 43, 28));
    FillRect(hdc, &closeRect, closeBrush);
    DeleteObject(closeBrush);
    SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc, L"×", -1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void Render(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    int contentHeight = height - TITLE_BAR_HEIGHT;

    // Create memory DC for double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // Clear entire background first (prevents flickering)
    Color bgColor = g_darkMode ? Color(30, 30, 30) : Color(240, 240, 240);
    Graphics graphics(memDC);
    graphics.Clear(bgColor);

    // Draw title bar
    DrawCustomTitleBar(memDC, hwnd);

    // Content area background
    RECT contentRect = {0, TITLE_BAR_HEIGHT, width, height};
    HBRUSH contentBrush = CreateSolidBrush(g_darkMode ? RGB(30, 30, 30) : RGB(240, 240, 240));
    FillRect(memDC, &contentRect, contentBrush);
    DeleteObject(contentBrush);

    // Draw image
    if (g_currentImage && g_currentImage->GetLastStatus() == Ok) {
        int imgWidth = g_currentImage->GetWidth();
        int imgHeight = g_currentImage->GetHeight();

        float drawWidth, drawHeight, x, y;

        if (g_fitToWindow) {
            float scaleX = (float)(width - 40) / imgWidth;
            float scaleY = (float)(contentHeight - 40) / imgHeight;
            float scale = (std::min)(scaleX, scaleY);
            drawWidth = imgWidth * scale;
            drawHeight = imgHeight * scale;
            x = (width - drawWidth) / 2 + g_panOffset.x;
            y = TITLE_BAR_HEIGHT + (contentHeight - drawHeight) / 2 + g_panOffset.y;
        } else {
            drawWidth = imgWidth * g_zoom;
            drawHeight = imgHeight * g_zoom;
            x = (width - drawWidth) / 2 + g_panOffset.x;
            y = TITLE_BAR_HEIGHT + (contentHeight - drawHeight) / 2 + g_panOffset.y;
        }

        // Draw shadow
        if (g_darkMode) {
            RectF shadowRect(x - 5, y - 5, drawWidth + 10, drawHeight + 10);
            SolidBrush shadowBrush(Color(60, 0, 0, 0));
            graphics.FillRectangle(&shadowBrush, shadowRect);
        }

        // Set high quality rendering
        graphics.SetSmoothingMode(SmoothingModeHighQuality);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);

        RectF destRect(x, y, drawWidth, drawHeight);
        graphics.DrawImage(g_currentImage, destRect);
    } else {
        // No image message
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(hdc, g_darkMode ? RGB(200, 200, 200) : RGB(100, 100, 100));
        HFONT font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(memDC, font);
        std::wstring msg = g_imageFiles.empty() ? L"Drag and drop an image here" : L"Failed to load image";
        RECT textRect = {0, TITLE_BAR_HEIGHT, width, height};
        DrawTextW(memDC, msg.c_str(), -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(memDC, oldFont);
        DeleteObject(font);
    }

    // Copy to screen
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// Handle window controls click
bool HandleTitleBarClick(HWND hwnd, int x, int y) {
    if (y > TITLE_BAR_HEIGHT) return false;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int btnSize = TITLE_BAR_HEIGHT - 4;
    int closeX = rc.right - btnSize - 5;
    int maxX = closeX - btnSize - 2;
    int minX = maxX - btnSize - 2;

    if (x >= closeX && x <= closeX + btnSize) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        return true;
    } else if (x >= maxX && x <= maxX + btnSize) {
        if (g_isMaximized) {
            ShowWindow(hwnd, SW_RESTORE);
            g_isMaximized = false;
        } else {
            GetWindowRect(hwnd, &g_normalRect);
            ShowWindow(hwnd, SW_MAXIMIZE);
            g_isMaximized = true;
        }
        return true;
    } else if (x >= minX && x <= minX + btnSize) {
        ShowWindow(hwnd, SW_MINIMIZE);
        return true;
    }

    return false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            DragAcceptFiles(hwnd, TRUE);
            CreateToolbarButtons(hwnd, ((LPCREATESTRUCT)lParam)->hInstance);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1; // Prevent flickering

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
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            DrawToolbarButton(dis, dis->CtlID);
            return TRUE;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_BTN_PREV: PrevImage(); break;
                case ID_BTN_NEXT: NextImage(); break;
                case ID_BTN_ZOOM_IN: ZoomIn(); break;
                case ID_BTN_ZOOM_OUT: ZoomOut(); break;
                case ID_BTN_FIT: ToggleZoom(); break;
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
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);

            if (HandleTitleBarClick(hwnd, x, y)) {
                return 0;
            }

            if (y > TITLE_BAR_HEIGHT) {
                g_isPanning = true;
                g_lastMousePos.x = x;
                g_lastMousePos.y = y;
                SetCapture(hwnd);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            g_isPanning = false;
            ReleaseCapture();
            return 0;

        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);

            if (g_isPanning) {
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
                case 'R': ResetView(); break;
                case 'D': g_darkMode = !g_darkMode; InvalidateRect(hwnd, nullptr, FALSE); break;
                case VK_ESCAPE: PostQuitMessage(0); break;
            }
            return 0;

        case WM_NCHITTEST: {
            // Allow dragging window by title bar
            POINT pt;
            pt.x = LOWORD(lParam);
            pt.y = HIWORD(lParam);
            ScreenToClient(hwnd, &pt);

            if (pt.y < TITLE_BAR_HEIGHT) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                int btnSize = TITLE_BAR_HEIGHT - 4;
                int closeX = rc.right - btnSize - 5;
                int maxX = closeX - btnSize - 2;
                int minX = maxX - btnSize - 2;

                if (pt.x < minX) return HTCAPTION;
            }
            break;
        }

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

    // Initialize common controls
    INITCOMMONCONTROLSEX iccex = {sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&iccex);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MinimalImageViewer";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    // Create borderless window
    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"MinimalImageViewer",
        L"Minimal Image Viewer",
        WS_POPUP | WS_VISIBLE | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
        nullptr, nullptr, hInstance, nullptr
    );

    // Load file from command line if provided
    if (lpCmdLine && wcslen(lpCmdLine) > 0) {
        std::wstring cmdLine = lpCmdLine;
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