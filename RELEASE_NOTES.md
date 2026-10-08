# Screenshot v0.4.0

- Native dark Windows 11 installer with DWM rounding and shadow, without black corner masks.
- Correct DPI rendering and layout at 100%, 125%, 150%, 200% and 250% scaling.
- Minimal installation, progress, completion and hotkey screens; explicit Print Screen confirmation and cancellation.
- The checked-in Screenshot.ico is embedded directly in both executables and rendered through the same Direct2D surface as the text. Builds no longer generate a substitute icon.
- Click to capture a visible window; drag to capture a region. Mouse coordinates come from each input event, preventing a later cursor movement from changing the crop.
- Every successful capture saves the identical PNG to the Windows clipboard and `%USERPROFILE%\Pictures\Screenshot`, including rounded corners and soft shadow.
- Clipboard retry, unique filenames, checked disk writes and visible errors when saving fails.
- Per-user installation and login autostart, no tray icon, atomic replacement of the running previous version and preserved hotkey selection during updates.

The downloadable executable was tested on a real Windows 11 machine, build 26100: upgrade over running v0.3.1, all 30 stage/DPI combinations, the actual installation wizard, Print Screen consent/cancel, activation with a test hotkey, window/region capture, exact crop sizes, known pixel colors and byte equality of the disk and clipboard PNG. Windows CI also verifies embedded payload integrity, repeated installation, live replacement, autostart and uninstall.

SHA-256 of the tested installer: `4f933e491b357cec05786071e2519470c4b0ad4b12eb2f7d099e33363fed7c07`.

The separately approved icon from the earlier conversation was not available among the accessible files. This release preserves the repository's original ICO rather than claiming that a newly drawn substitute is the approved artwork. Windows may require signing out after changing its Print Screen binding. The installer is unsigned.
