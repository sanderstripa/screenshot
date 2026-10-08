# Screenshot

**Beautiful screenshots. Nothing else.**

Screenshot is a minimal native screenshot utility for Windows 11 by Sander Stripa. Press **Print Screen**, click a window or drag to capture an area, and paste the polished screenshot immediately with **Ctrl+V**.

The app runs quietly at login, without a tray icon, settings window, background web engine, OCR, or recording features.

> The first Windows build is in development. Downloads will be published under [Releases](https://github.com/sanderstripa/screenshot/releases) after the Windows build and smoke test pass.

## How it works

- **Print Screen** — activate the capture overlay.
- **Hover + click** — capture the highlighted window.
- **Click + drag** — capture a custom rectangular area.
- **Esc** or the on-screen **×** — cancel.
- Paste immediately with **Ctrl+V**. The clipboard receives a PNG with rounded edges and a soft shadow.

## Design

Native Win32/C++ with Direct2D for the overlay and WIC for PNG encoding. No Electron, Chromium, .NET runtime, browser processes, or telemetry.

The selected Screenshot artwork is copyright © Sander Stripa. The C++ source code uses the MIT License.

## Limitations

Screenshot captures the visible desktop pixels at activation. Hidden or occluded portions of windows are not reconstructed. Protected video surfaces and HDR content may not capture as expected. Windows may reserve Print Screen for Snipping Tool; turn off **Settings → Accessibility → Keyboard → Use the Print Screen key to open screen capture** if the hotkey is taken.

## Build

Use Visual Studio 2022 with the Desktop development with C++ workload and CMake:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\Screenshot.exe --self-test
```

## About

Screenshot © 2026 Sander Stripa. Windows 11 · native · lightweight · no subscriptions.
