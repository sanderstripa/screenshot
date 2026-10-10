# Screenshot v0.5.1

- Removed arrows from Install, Continue and Next buttons.
- Added an unlabeled theme toggle at the top right of the welcome screen.
- Added one language control at the bottom left: RU switches to EN and EN switches back to RU.
- All installer screens, buttons, instructions and application error dialogs switch between Russian and English.
- The selected theme and language remain active throughout installation and shortcut configuration.
- Start menu settings, live hotkey reassignment, the approved icon, hidden startup and PNG saving remain available.

Tested on Windows 11 build 26100: actual clicks in both directions, independent theme/language switching, an actual update using the light English wizard, PNG disk/clipboard output, and all six screens in both themes and languages at 100/125/150/200/250% (120 combinations). Native screens were captured and visually inspected. Windows CI also passed installation, live upgrade, autostart, Start menu and uninstall checks.

SHA-256 of the exact tested installer: `f4d88b202390b925b153adadaa08b04b1c4e4081790ac49a982ad5c9d823dfae`.
