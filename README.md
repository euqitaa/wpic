# wpic

A minimal, lightweight image viewer for Windows. Built with pure Win32 API and GDI+ - no Qt, no .NET, no bloat.

![wpic Icon](wpic.png)

## Features

- **Minimal footprint**: ~50KB executable, ~5-10MB RAM usage
- **Fast loading**: Native Win32 + GDI+ rendering
- **Clean UI**: Standard Windows title bar with bottom toolbar
- **Folder navigation**: Auto-scans folder for images
- **Keyboard shortcuts**:
  - `←` / `→` or `Space` / `Backspace` - Navigate images
  - `↑` / `↓` - Zoom in/out
  - `F` - Toggle fit-to-window
  - `R` - Reset rotation
  - `Delete` - Move to Recycle Bin (with confirmation)
  - `Esc` - Exit
- **Mouse controls**:
  - Scroll wheel - Zoom
  - Drag - Pan when zoomed
  - Drag & drop - Open image
- **Supported formats**: JPG, PNG, GIF, BMP, TIFF, WebP
- **Safe delete**: Files moved to Recycle Bin with confirmation dialog
- **Image rotation**: Rotate left/right without distortion

## Screenshots

### Main Interface
Standard Windows title bar with centered toolbar at bottom:
```
[◄ Previous] [► Next] | [⊖ Zoom out] [⊕ Zoom in] [□ Fit] [1:1 Actual] | [↺ Rotate left] [↻ Rotate right] | [✕ Delete]
```

## Building

### Prerequisites

- **MinGW-w64** (recommended) or **Visual Studio Build Tools**
- Your icon file: `wpic.ico` (convert the provided PNG to ICO format)

### Build Steps

1. **Convert icon** (if you have PNG):
   - Use any online PNG to ICO converter
   - Recommended size: 256x256 with 32x32 and 16x16 versions included
   - Save as `wpic.ico`

2. **Compile resource file**:
   ```bash
   windres wpic.rc -o wpic_res.o
   ```

3. **Build executable**:
   ```bash
   g++ -O2 -s wpic.cpp wpic_res.o -o wpic.exe -municode -lgdiplus -luser32 -lgdi32 -lshell32 -mwindows
   ```

### MSVC Alternative

```cmd
rc.exe wpic.rc
cl.exe wpic.cpp /O2 /Fe:wpic.exe /link wpic.res gdiplus.lib user32.lib gdi32.lib shell32.lib
```

## Installation

Simply place `wpic.exe` anywhere you want. It's a portable single-file application.

Optional: Set as default image viewer:
1. Right-click an image file → Open with → Choose another app
2. Browse to `wpic.exe`
3. Check "Always use this app to open .jpg files" (repeat for other formats)

## Usage

```cmd
wpic.exe [image_path]
```

Or drag and drop an image onto the window.

## Why wpic?

| Feature | Microsoft Photos | wpic |
|---------|-----------------|------|
| Startup Time | Slow (UWP) | Instant |
| RAM Usage | ~100-200MB | ~5-10MB |
| File Size | ~50MB+ | ~50KB |
| Dependencies | Windows Store, UWP | Windows only |
| Network Calls | Yes (OneDrive) | No |
| Telemetry | Yes | No |
| Delete Safety | Permanent | Recycle Bin |

## Architecture

- **No frameworks**: Pure Win32 API + GDI+
- **No runtime dependencies**: Uses only Windows system libraries
- **Static linking**: Single executable file
- **Memory efficient**: Loads only current image

## Files

- `wpic.cpp` - Main source code
- `wpic.rc` - Resource file (icon + version info)
- `wpic.ico` - Application icon (you provide this)

## License

Public Domain - Do whatever you want.

---

**Icon**: Custom "W" logo with mountain landscape and crescent moon
