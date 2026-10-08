# Screenshot

**Beautiful screenshots. Nothing else.**

Native Windows 11 screenshot capture by Sander Stripa.

## Install
Download Screenshot-Setup.exe from [Releases](https://github.com/sanderstripa/screenshot/releases). A custom dark setup installs automatically under %LOCALAPPDATA%\Programs\Screenshot without asking for a folder. After installation, choose a screenshot shortcut (Print Screen is the default), confirm replacing the Windows Snipping Tool action when necessary, and close the wizard.

## Controls
- Assigned shortcut: start capture
- Hover and click: capture a visible window
- Drag: capture an area
- Esc or ×: cancel
- Ctrl+V: paste polished PNG

No tray icon, no settings panel, no OCR, no recording, no cloud, and no telemetry.

## Native build
Windows 11, Visual Studio 2022 C++/CMake and Inno Setup 6.
1. cmake -S . -B build -A x64
2. cmake --build build --config Release --target Screenshot
3. Compile installer.iss with ISCC to dist/ScreenshotCore-Setup.exe
4. cmake --build build --config Release --target ScreenshotSetup
5. The custom installer is build/Release/Screenshot-Setup.exe

The native custom C++ launcher embeds an Inno silent installation engine for correct uninstall and registry integration. Only custom Screenshot UI is shown.

## Known limitations
Print Screen may require signing out after its Windows Snipping Tool binding is disabled. Third-party occupied shortcuts must be changed in the conflicting application. Capture reads the visible desktop pixels; obscured windows, protected media and HDR may not capture perfectly. The installer is unsigned and may trigger SmartScreen.

Code MIT; branding and approved logo copyright Sander Stripa.
