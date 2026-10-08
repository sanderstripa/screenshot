<p align="center">
  <img src="assets/Screenshot.png" width="128" height="128" alt="Screenshot icon">
</p>

<h1 align="center">Screenshot</h1>

<p align="center"><strong>Beautiful screenshots. One shortcut.</strong></p>

<p align="center">
  Windows 11 · Native app · PNG to clipboard and disk
</p>

<p align="center">
  <a href="https://github.com/sanderstripa/screenshot/releases/latest/download/Screenshot-Setup.exe"><strong>Download for Windows</strong></a>
  · <a href="https://github.com/sanderstripa/screenshot/releases">Release notes</a>
</p>

Screenshot captures a visible window or any screen region, adds rounded corners and a soft shadow, and saves the same PNG to your clipboard and `%USERPROFILE%\Pictures\Screenshot`.

## Features

- **Click a window or drag a region.** Open the capture overlay with your chosen shortcut.
- **Save and paste.** Every successful capture creates a PNG file and puts the identical PNG in the clipboard.
- **Ready at login.** A small native background app with automatic startup and no tray icon.
- **A native Windows 11 installer.** Dark UI, real rounded window corners, crisp text and high-resolution artwork.
- **Your shortcut.** Choose a hotkey after installation; replacing Windows' Print Screen action requires explicit confirmation.
- **Safe updates.** The installer replaces a running previous version and keeps your shortcut settings.

## Install

Download [**Screenshot-Setup.exe**](https://github.com/sanderstripa/screenshot/releases/latest/download/Screenshot-Setup.exe), run it and click **Install**. The current release is [**v0.4.1**](https://github.com/sanderstripa/screenshot/releases/tag/v0.4.1), with the supplied high-resolution Screenshot icon throughout the app and installer.

Installation runs under your Windows account without administrator privileges. It installs to `%LOCALAPPDATA%\Programs\Screenshot` and registers automatic startup at login. After installation, choose a hotkey. Print Screen is the default; Screenshot asks before changing its built-in Windows action. Windows may require signing out for that change to apply.

## Use

- Assigned key — open the capture overlay.
- Click an application window — capture the visible window.
- Drag with the mouse — capture any region.
- Escape or top **×** — cancel.
- Ctrl+V — paste the styled PNG with rounded corners and shadow.
- Every capture also saves the identical PNG to `%USERPROFILE%\Pictures\Screenshot`.

## Requirements and verification

Windows 11, 64-bit. The downloadable installer is self-contained; Python and developer tools are not required to use the app.

Tested on Windows 11 build 26100, including installation and updates, capture output, the native installer at 100–250% scaling, and the embedded icon resources. See the [Windows test report](WINDOWS_TEST_REPORT.md) for checks and their scope.

## Native build

Requires Visual Studio 2022 / Windows SDK. The supplied approved artwork is embedded directly, with nine ICO resolutions and a full-resolution PNG for the installer. Builds never redraw it. **No Inno Setup.**

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
python scripts/verify_icon.py assets/Screenshot.ico build/Release/Screenshot-Setup.exe build/Release/Screenshot.exe
```

The UI test opens every native setup stage at 100%, 125%, 150%, 200% and 250% scaling. The capture test opens a known-color native window, exercises click and drag capture, decodes the saved PNG and compares its bytes to the PNG in the Windows clipboard.

## Notes

Screenshots are captured from visible desktop pixels, so occluded windows, HDR and protected media may not render perfectly. Other applications' keyboard shortcuts cannot be forcibly overridden. The installer is unsigned and may be flagged by Windows SmartScreen.

Source code MIT licensed. Original icon and branding © Sander Stripa.

Made by [Sander Stripa](https://github.com/sanderstripa). [About Screenshot](ABOUT.md).
