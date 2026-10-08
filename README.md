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

## Native build

Requires Visual Studio 2022 / Windows SDK and Python 3 with Pillow for creating the full-resolution icon. **No Inno Setup.**

```powershell
python -m pip install Pillow
python scripts/make_icon.py assets
cmake -S . -B build -A x64
cmake --build build --config Release --target ScreenshotSetup
.\build\Release\Screenshot-Setup.exe
```

The setup binary embeds the native Screenshot executable. It directly performs per-user installation, safe updates of running older versions, registry registration, and uninstallation. Windows Actions additionally verify a repeat install, an update over a live process, installed hash, and uninstall behavior.

## Notes

Screenshots are captured from visible desktop pixels, so occluded windows, HDR and protected media may not render perfectly. Other applications' keyboard shortcuts cannot be forcibly overridden. The installer is unsigned and may be flagged by Windows SmartScreen.

Source code MIT licensed. Original icon and branding © Sander Stripa.
