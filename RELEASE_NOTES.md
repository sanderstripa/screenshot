# Screenshot v0.4.1

- Installed the user's supplied approved icon. Its original PNG is retained in `assets/Screenshot-approved-original.png`.
- Removed the exterior white background for transparent rounded icon edges. The design is retained without the previous simplified substitute.
- Packaged nine Windows ICO frames: 16, 20, 24, 32, 40, 48, 64, 128 and 256 pixels.
- The installer renders the full-resolution 1254×1254 PNG through Direct2D/WIC, keeping the large logo clear at high DPI.
- The application, setup and uninstall executable use the same icon resource. Windows' installed program entry points to the new application icon.
- Installation notifies Explorer to refresh cached icons after replacing the executable at the same path.

Verified on Windows 11 build 26100: upgrade from running v0.4.0, successful installation, exact comparison of all nine embedded icon frames in setup and the installed application, full-resolution installer PNG resource, all six native setup stages at 100–250% scaling, and disk/clipboard PNG self-test. Windows CI checks installation, reinstall, live upgrade, autostart and uninstall. The capture behavior from v0.4.0 is retained.

SHA-256 of the tested downloadable installer: `dc1a1f19770e9a30b2f0569c136a408df0a8afcdc35b90db2cb842c9e0631122`.

Screenshots save to `%USERPROFILE%\Pictures\Screenshot` and the clipboard. No tray icon. The installer remains unsigned.
