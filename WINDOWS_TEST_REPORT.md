# Windows 11 verification — 2026-10-18

## v0.6.3 — VPN appearance and alignment

Final candidate v0.6.3-rc.2 SHA-256: `6e049eafd945b6eb6efb1efa4430439a0b730ec1cb75510fd53bc0dfa4ededa4`. Final release reuses these exact bytes.

The user explicitly requested the VPN tone palette, overriding earlier preservation instructions. Colors were taken from CONNECT frontend/src/style.css; appearance control metrics/selection from frontend/src/styles/connect-ui-v1.css. The same Lucide icon family used by CONNECT supplies Monitor/Sun/Moon/Palette/ArrowLeft; attribution is retained in assets/Lucide-LICENSE. Approved settings #8 remains unchanged.

PASS actual rendered gear vertical center = 341, matching Install center = 341. PASS selected preference pixel #EEEEEE (light) / #1A1B1E (dark), without inverted text or bright primary fills. Native screenshots of both preference pages were visually reviewed against the supplied VPN screenshots.

PASS app settings, RU/EN and System/Light/Dark behavior, live key reassignment, occupied-key rejection, Print Screen cancellation, persistence, custom window/keyboard controls and all five app DPI scales from 100–250%. PASS six installer stages × two themes × two languages at 100% (24 captures), plus actual wizard update/preferences/notice interaction. PASS PNG disk/clipboard output and Windows CI installation, update, startup, Start menu and uninstall. App remains 520×382 and setup 640×400 logical pixels; Montserrat is unchanged.

## v0.6.2 — approved SVG #8

Final candidate v0.6.2-rc.2 SHA-256: `06c0583cddc98402152ce8abd0b2d90e30bd07cc29bf96010ddfa7fa2652491b`. Final release reuses these exact bytes.

PASS original SVG Git blob equality with the CONNECT reference branch; PASS reproducible native geometry generation checked in CI. Actual native light/dark captures show approved #8 without a background tile or mouse-focus box in both installer and app. PASS settings icon clicks, persisted theme/language, live shortcut reassignment/conflict rejection, Print Screen cancellation, keyboard/window controls, update and PNG disk/clipboard regression. Settings passed all five scales from 100–250%; synthetic DPI tests anchor the fixture on the primary display to avoid crossing monitors. Test windows are placed behind the foreground application; native PrintWindow captures exercise the actual UI without requiring it to cover the user's desktop.

## v0.6.1 — confirmed launch-permissions regression

Observed the user's running v0.6.0 worker with TokenElevation=1. An ordinary launch exited without opening a window; PostMessage(WM_SCREENSHOT_SETTINGS) returned false and GetLastError=5 (access denied). This cross-integrity case was missing from the earlier tests.

Final candidate v0.6.1-rc.3 SHA-256: `43f9ca69c5adbca1650e2089348bf02f9820fba15419438891dc33e4fc9c0e4a`. The final release reuses these exact bytes.

PASS update over an elevated installation; PASS a real ordinary Start menu launch opens settings in a deliberately elevated worker; PASS the administrator-run wizard launches a worker with TokenElevation=0 via the desktop Shell; PASS its finish button opens the app window. The elevated test helper restores the user's hotkey and stops its fixtures. Reproducible tests: `scripts/test_launch_permissions.ps1` with `scripts/test_elevated_setup.ps1`; the driver runs normally and requests interactive UAC only for its elevated helper.

PASS existing settings regression, persisted language/theme, live hotkey replacement and conflict rejection, Print Screen cancellation, five DPI scales, keyboard controls and PNG disk/clipboard output. Native wizard interactions and notification checks passed. No broad capture implementation change was made. The approved gear #8 source is still awaiting the user's attachment, so no replacement icon is included; only the unwanted filled background was removed.

## v0.6.0 — CONNECT UI System integration

Final candidate v0.6.0-rc.3 tested on Windows 11 build 26100; final release reuses its exact installer bytes. SHA-256 `4b50e4452066a0ca752c8e971b9a8dcdda7ec1d5ff2e6d45663b90bcfb0800c9`.

- PASS actual update over installed previous version; setup preferences survive into installation complete, shortcut and confirmation stages.
- PASS original #F5F5F5 / #0A0A0A backgrounds and #111111 / #F4F4F4 primary button fills, sampled from actual rendered windows.
- PASS unchanged logical dimensions: app 520×382 and setup 640×400.
- PASS Montserrat asset unchanged byte for byte; original Screenshot branding retained.
- PASS live F24 → F23 assignment, old key released, new key registered by the same process; occupied F22 does not replace the working key.
- PASS explicit Print Screen consent cancellation preserves the Windows binding and app shortcut.
- PASS app RU/EN and System/Light/Dark preferences, persistence after restart, and System palette matched to Windows. WM_SETTINGCHANGE refresh tested against current Windows setting without changing the user's OS theme.
- PASS app and setup minimize; no maximize style; Alt+F4 closes the visible UI and leaves the background capture worker running for app settings.
- PASS titlebar drag and minimize client hit tests; keyboard Tab/Space/Enter navigation and visible 2px focus states.
- PASS native installer conflict notice, keyboard dismissal and confirmed complete occlusion of underlying controls.
- PASS both settings themes at 100/125/150/200/250%; setup six stages × two themes × RU/EN × five scales (120 captures), with correct dimensions and background colors. Representative real screenshots inspected for text, controls and dialogs.
- PASS PNG disk/clipboard output after update. Windows CI independently verifies build, payload, repeated installation, update over a running executable, autostart, Start menu link and uninstall.

Tests restore original app shortcut, language and theme values. Screenshots remain local. The approved gear #8 visual is missing, so its exact artwork is not tested or replaced; text settings actions remain available.

## v0.5.2 — brand heading

Tested candidate v0.5.2-rc.1 on Windows 11 build 26100. Exact final installer SHA-256: `20b6c486a7992e5ca775fff6e81f22cd5a214120549898af7edcddae40cd98a8`.

PASS update over v0.5.1; native light/dark screenshots show SCREENSHOT. above Settings. PASS existing settings regression checks, including Start menu launch, live shortcut changes, conflict rejection, consent cancellation, persistence and both themes at five scales. Physical heights use truncation for fractional DPI pixels, matching the native window size. Installer behavior is unchanged; Windows CI repeats installation/update/startup/shortcut/uninstall checks.

## v0.5.1 — installer language and theme

Downloaded v0.5.1-rc.1 tested on Windows 11 build 26100. SHA-256: `f4d88b202390b925b153adadaa08b04b1c4e4081790ac49a982ad5c9d823dfae`. The final release publishes these same bytes.

- PASS actual RU → EN → RU clicks; original UI restored byte for byte.
- PASS dark → light → dark clicks; current language retained and exact reference background colors used.
- PASS upgrade via the actual light English wizard; Installation complete → Keyboard shortcut remains light and English.
- PASS all six installer stages, both themes, both languages and five DPI scales: 120 combinations. Window sizes and background colors checked; captured screens visually reviewed.
- PASS PNG disk/clipboard after the update; user's existing shortcut retained and background process restarted.
- PASS Windows CI install, reinstall, live upgrade, shortcut/autostart registration and uninstall.

Scripts: `scripts/test_installer.ps1`, `scripts/test_installer_switches.ps1`. Window lookup is restricted to each test's process ID so a separately opened installer is not captured. Test screenshots remain local.

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

## v0.6.4
PASS exact SVG comparison against locked VPN npm package 0.468.0; 13px rendering. PASS Windows app at five DPI scales 100–250%, theme/language persistence, shortcut controls, actual installer update, preferences and PNG disk/clipboard. Light app/dark installer screenshots visually reviewed. Stable reuses tested candidate SHA-256 e182d7496391227a2014e1b3ab534a031096c09fea96d9e68bb5d147446410e5.

## v0.6.5 — window placement
PASS scripts/test_window_position.ps1 on installed Windows app: move persists immediately; close/reopen preserves position; full process restart after minimize preserves normal position; unavailable monitor coordinates recover onto desktop. Test restores original position. Physical PC reboot not performed. Tested candidate SHA-256 fc313115396dd9299aec4fac0a3560bc2a3acf9a391a9a96a03b376758933dda; stable reuses these bytes.

## v0.6.6 — caption buttons without fills
PASS actual native window pixel checks: app/setup x light/dark x minimize/close x normal/hover/pressed. Screenshots reviewed. Candidate SHA-256 9216d00e98d2054a2381f90ea114f3cc57ae90e08e9a62914a3dbb0ed767e7ff; stable reuses tested bytes.

## v0.6.7 — approved native CONNECT installer
Approved visual integration in src/setup.cpp. Six native wizard stages in light/dark at 100/150/200% DPI captured. Actual GUI update checks cover all four Desktop/Start menu shortcut combinations and their target/--settings arguments, RU/EN and System/Light/Dark, Print Screen confirmation cancellation, custom shortcut activation and Done launch off/on. User hotkey configuration restored by test. Application main.cpp/settings.cpp/connect_ui.h unchanged. Windows CI covers clean/reinstall/live upgrade/autostart/uninstall and PNG/clipboard. Final candidate v0.6.7-rc.2 SHA-256 4ddfab82fd07d25dfcb8a5e9078bc54a20a9cb307490d168e3abe2d95ed217de; stable publishes those exact bytes. Native screenshots reviewed against approved preview; no real reboot test.

## v0.6.8 — custom install location and preference inheritance
Native folder picker/cancel and editable folder tested with WM_SETTEXT/WM_GETTEXT, not cross-process caption cache APIs. Unicode .lnk targets verified through IShellLinkW rather than ANSI WScript target properties; real shortcut launch tested. Actual install/update in a path containing spaces/Cyrillic, registered autostart/uninstall/shortcuts, dark+English and light+Russian application rendering, custom-path uninstall and unrelated-file preservation, default-path restore and clean reinstall. Candidate SHA-256 93484348cccbb12c6e3afab0f65511012dcfb26b85d02f1b9328e53b941c0340; stable uses tested bytes. App source main.cpp/settings.cpp/connect_ui.h unchanged. Reboot and protected Program Files installation were not tested.
