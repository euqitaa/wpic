# Minimal Image Viewer for Windows

A lightweight, native Windows image viewer built with pure Win32 API and GDI+. No Qt, no heavy frameworks, no bloat.

## Features

- **Minimal footprint**: ~50KB executable, ~5-10MB RAM usage
- **Fast loading**: Native Win32 + GDI+ rendering
- **Folder navigation**: Auto-scans folder for images
- **Keyboard shortcuts**:
  - `←` / `→` or `Space` / `Backspace` - Navigate images
  - `↑` / `↓` - Zoom in/out
  - `F` - Toggle fit-to-window
  - `R` - Reset view
  - `D` - Toggle dark/light mode
  - `Esc` - Exit
- **Mouse controls**:
  - Scroll wheel - Zoom
  - Drag - Pan when zoomed
  - Drag & drop - Open image
- **Supported formats**: JPG, PNG, GIF, BMP, TIFF, WebP

## Building

### Option 1: Visual Studio (Recommended)
```cmd
cl.exe minimal_viewer.cpp /O2 /EHsc /Fe:minimal_viewer.exe /link gdiplus.lib user32.lib gdi32.lib shell32.lib
```

### Option 2: MinGW
```cmd
g++ -O2 -s minimal_viewer.cpp -o minimal_viewer.exe -lgdiplus -luser32 -lgdi32 -lshell32 -mwindows
```

### Option 3: Run build.bat
Double-click `build.bat` - it auto-detects your compiler.

## Usage

```cmd
minimal_viewer.exe [image_path]
```

Or drag and drop an image onto the window.

## Why This?

| Feature | Microsoft Photos | This Viewer |
|---------|-----------------|-------------|
| Startup Time | Slow (UWP) | Instant |
| RAM Usage | ~100-200MB | ~5-10MB |
| File Size | ~50MB+ | ~50KB |
| Dependencies | Windows Store, UWP | Windows only |
| Network Calls | Yes (OneDrive) | No |
| Telemetry | Yes | No |

## Architecture

- **No frameworks**: Pure Win32 API
- **No runtime dependencies**: Uses only Windows system libraries (gdiplus.dll, user32.dll, gdi32.dll)
- **Static linking**: All code in single executable
- **Memory efficient**: Loads only current image, streams from disk

## License

Public Domain - Do whatever you want.
