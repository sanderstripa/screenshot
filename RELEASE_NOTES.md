# Screenshot v0.2.0

Native Windows 11 screenshot app with a rebuilt custom dark installer.

- Custom C++/Direct2D setup, no stock Inno Setup wizard.
- One-click installation to a fixed per-user folder.
- Simple progress, completion, first-run shortcut picker and explicit Windows Print Screen replacement confirmation.
- Native background capture of windows or selected rectangles, with rounded corners and soft shadow.
- Silent login autostart; no tray icon, cloud, telemetry, or Chromium.

**Limitations:** Other applications' hotkeys cannot be forcibly overridden. Windows may require signing out to release Print Screen from Snipping Tool. Visible pixels only, no protected video/HDR guarantee. Unsigned binary may trigger Windows SmartScreen. CI builds and runs native self-tests; physical Windows 11 desktop/monitor testing is still needed.
