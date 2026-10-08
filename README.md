# Screenshot

**Beautiful screenshots. Nothing else.**

Screenshot is a tiny C++/Win32 capture app for Windows 11. No tray, editor, OCR, recording, telemetry, or browser engine.

## Install

Download the latest **Screenshot-Setup.exe** from [Releases](https://github.com/sanderstripa/screenshot/releases).

The custom dark installer requires one click, no destination choice, and no administrative privileges. It installs to `%LOCALAPPDATA%\Programs\Screenshot` and configures Windows login autostart. After installation, assign your desired hotkey (Print Screen by default). Screenshot asks for confirmation before changing Windows' built-in Print Screen Snipping Tool behavior. Windows may require signing out for that change to apply.

## Use

- Assigned key — open the capture overlay.
- Click an application window — capture the visible window.
- Drag with the mouse — capture any region.
- Escape or top **×** — cancel.
- Ctrl+V — paste the styled PNG with rounded corners and shadow.
- Every capture also saves the identical PNG to `%USERPROFILE%\Pictures\Screenshot`.

## Native build

Requires Visual Studio 2022 / Windows SDK. The checked-in icon is embedded directly; builds never redraw or replace it. **No Inno Setup.**

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target ScreenshotSetup
.\build\Release\Screenshot-Setup.exe
```

The setup binary embeds the native Screenshot executable. It directly performs per-user installation, safe updates of running older versions, registry registration, and uninstallation. Windows Actions additionally verify a repeat install, an update over a live process, installed hash, and uninstall behavior.

Real desktop regression checks (Python with Pillow):

```powershell
python scripts/test_ui.py build/Release/Screenshot-Setup.exe test-results/ui
python scripts/test_capture.py "$env:LOCALAPPDATA/Programs/Screenshot/Screenshot.exe" test-results/capture
```

The UI test opens every native setup stage at 100%, 125%, 150%, 200% and 250% scaling. The capture test opens a known-color native window, exercises click and drag capture, decodes the saved PNG and compares its bytes to the PNG in the Windows clipboard.

## Notes

Screenshots are captured from visible desktop pixels, so occluded windows, HDR and protected media may not render perfectly. Other applications' keyboard shortcuts cannot be forcibly overridden. The installer is unsigned and may be flagged by Windows SmartScreen.

Source code MIT licensed. Original icon and branding © Sander Stripa.
