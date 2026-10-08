# Windows 11 verification — 2026-10-09

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

The approved icon attachment from the earlier chat was inaccessible. The checked-in icon was preserved and build-time icon regeneration removed.
