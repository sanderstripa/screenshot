# Screenshot v0.1.0

First native Windows 11 preview by Sander Stripa.

- Invisible user-session background process, with no tray icon; automatic login startup.
- **Print Screen** opens a smooth animated screen capture overlay.
- Hover and click a visible window, or drag to select a rectangle.
- Cancel via the small floating **×** button or **Esc**.
- Rounded corners, soft shadow and automatic PNG clipboard transfer.
- C++/Win32/Direct2D/WIC; no Electron, telemetry, editor, recording, or cloud service.
- Lightweight per-user installer; no administrator permission required.

**Limitations:** Captures the visible screen at activation, not obscured window contents. Protected media and HDR can be problematic. If Snipping Tool reserves Print Screen, turn it off in Settings → Accessibility → Keyboard, then restart Screenshot.

**Validation:** GitHub Actions compiles and runs synthetic image-processing/PNG smoke tests. Hands-on Windows 11 GPU/monitor testing is still required.
