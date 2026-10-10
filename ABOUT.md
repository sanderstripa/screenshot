<p align="center">
  <img src="assets/Screenshot.png" width="112" height="112" alt="Screenshot icon">
</p>

# About Screenshot

**Screenshot** · v0.5.0 · Windows 11 x64

Capture a visible window or screen region with a single keyboard shortcut. The resulting PNG, including soft shadow and subtle corner rounding, is saved to `%USERPROFILE%\Pictures\Screenshot` and copied to the clipboard.

The app uses native C++17, Win32, Direct2D, DirectWrite and Windows Imaging Component. It is a small user-session background process without a tray icon, heavy frameworks, network access or telemetry.

The setup program has an original dark Windows UI. It directly installs the application under the current user's profile, supports safe upgrade over a running previous version, and lets the user assign a hotkey after installation.

Version 0.5.0 adds Start menu settings, live shortcut reassignment and persistent light/dark themes. Settings and the installer share Montserrat typography and a neutral palette.

Screenshot includes the supplied Screenshot artwork in the application, installer and uninstaller: nine Windows icon resolutions and a full-resolution PNG for the installer. Windows refreshes its icon cache after installation to show the updated artwork.

Windows 11 x64 is required. Installation runs without administrator privileges and registers startup at login. Replacing Windows' Print Screen action requires confirmation. The installer is self-contained and unsigned.

[Download Screenshot](https://github.com/sanderstripa/screenshot/releases/latest/download/Screenshot-Setup.exe) · [Release notes](https://github.com/sanderstripa/screenshot/releases) · [Windows test report](WINDOWS_TEST_REPORT.md)

Developer: **Sander Stripa**. Code MIT licensed. Original Screenshot name/visual artwork © 2026 Sander Stripa. Not affiliated with Apple or Microsoft.

