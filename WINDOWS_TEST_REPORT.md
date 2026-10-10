# Windows 11 verification — 2026-10-11

## v0.5.0 — Start menu settings and shared UI

Tested downloaded candidate v0.5.0-rc.2 on Windows 11 build 26100. Final release publishes these exact tested bytes: SHA-256 `4a0e4caaca351728231e14d0988021868ebe50e9e5ba1959c70f086606ca75cf`.

- PASS upgrade over the installed v0.5.0-rc.1.
- PASS Start menu shortcut target, arguments and icon; launch forwards to the existing background process.
- PASS hidden autostart, no settings on login; closing settings leaves capture running.
- PASS F24 to F23 applies live, old key released, new key registered by the same process.
- PASS occupied F22 refused while current working key and configuration remain intact.
- PASS Print Screen shows confirmation; cancellation leaves Windows binding and current shortcut unchanged.
- PASS theme and shortcut persist across a complete process restart.
- PASS both settings themes at 100/125/150/200/250%, with a clickable title-row toggle and exact reference background colors.
- PASS all six installer stages at the same five scales (30 combinations), correct window dimensions and #0A0A0A background; actual screenshots visually reviewed for layout and Montserrat rendering.
- PASS PNG disk/clipboard self-test after settings changes.

Regression scripts: `scripts/test_settings.ps1`, `scripts/test_installer.ps1`. Settings tests restore the user's original shortcut, theme and Print Screen configuration. Screenshots stay local.

## v0.4.1 — supplied icon

The user's original PNG is retained. The approved design is used with transparent exterior edges, nine ICO sizes and a 1254×1254 installer master. The original and prepared assets are separate files.

On Windows 11 build 26100: live upgrade from v0.4.0 and final installation PASS; all nine actual icon resources in both EXEs match the packaged ICO byte for byte; embedded installer PNG matches its master byte for byte; all 30 native stage/DPI combinations PASS; PNG disk/clipboard self-test PASS. Icon cache refresh is issued during successful installation.

Final installer SHA-256: `dc1a1f19770e9a30b2f0569c136a408df0a8afcdc35b90db2cb842c9e0631122`.

The UI screenshot test uses direct native-window capture when an unrelated desktop popup obscures the window. Additional mouse-driven capture reruns on this active desktop were interrupted by unrelated windows; those reruns are not counted as passes. Capture code is unchanged from the verified v0.4.0 tests below.

## v0.4.0 — application and installer behavior

System: Windows 11, build 26100, x64. Tests ran against the downloaded executable.

Installer SHA-256: `4f933e491b357cec05786071e2519470c4b0ad4b12eb2f7d099e33363fed7c07`.

| Check | Result |
| --- | --- |
| Install published v0.3.1, then upgrade | PASS, exit code 0 |
| Upgrade while installed v0.3.1 holds its executable | PASS, old process exits and installer returns 0 |
| Six native UI stages at 100/125/150/200/250% | PASS, 30 combinations, correct dimensions and painted surface; screenshots visually inspected |
| Actual wizard | PASS: install → complete → hotkey → Print Screen confirmation → cancel → F24 → ready |
| Print Screen consent | PASS, Windows binding unchanged before consent and after cancellation |
| Hotkey activation | PASS, test hotkey saved, app launched, ready screen displayed; original settings restored afterward |
| Window capture | PASS, 550×380 PNG, actual fixture bounds plus 24px padding per edge |
| Region capture | PASS, 200×120 crop produces 248×168 PNG |
| Captured pixels | PASS, fixture color #1680e8 and opaque center |
| Clipboard and disk | PASS, PNG clipboard bytes exactly match the saved PNG |
| Output directory | PASS, `%USERPROFILE%\Pictures\Screenshot` |
| Windows CI | PASS, PNG encode/decode, payload, reinstall, live update, installed hash, autostart and uninstall |

Desktop checks: `scripts/test_ui.py`, `scripts/test_capture.py`, `scripts/test_wizard.py`. Python with Pillow is only needed for testing. Screenshots stay local and are not published.

At v0.4.0 the approved attachment was unavailable. The user has since supplied it and v0.4.1 installs that design.

