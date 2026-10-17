# Screenshot v0.6.1 — restore application opening after installation

- Fixed the confirmed Start menu failure when Screenshot was running elevated: ordinary launches received Windows error 5 when sending the open-settings request. The elevated controller now permits only that argument-free message across UIPI.
- An administrator-run installer delegates app startup to the existing desktop Shell so Screenshot runs with ordinary user rights.
- The final wizard button opens the installed application instead of only closing the installer.
- Removed the filled hover/press background from the installer's settings action. The original approved gear #8 is pending the user's source image; no substitute icon is used.
- Montserrat, both palettes, window dimensions, capture behavior and existing hotkey settings are retained.

Verified on Windows 11 build 26100: update over an elevated previous installation, ordinary Start menu → elevated worker, elevated wizard → ordinary worker, and finish → visible app settings. Existing settings, hotkey conflicts, Print Screen cancellation, five DPI scales, wizard notices, keyboard/window controls and PNG disk/clipboard regressions passed.

Tested installer SHA-256: `43f9ca69c5adbca1650e2089348bf02f9820fba15419438891dc33e4fc9c0e4a`.
