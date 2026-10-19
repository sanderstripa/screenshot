# Screenshot v0.6.8 — installation folder and first-launch preferences

- Installation settings now show an editable destination and a native Browse folder picker. Updates default to the registered installation folder.
- Installation, launch, Desktop/Start menu shortcuts, autostart and uninstall use the selected location. Relocation removes only the old Screenshot executable/uninstaller; unrelated files are retained.
- The installer saves System/Light/Dark, its effective light state and RU/EN before the application starts. The first application window inherits these choices; they remain editable inside the app.
- The application UI/capture implementation is unchanged.

Windows verification: native folder picker/cancel, custom directory with spaces and Cyrillic, repeated update there, real Unicode Start menu shortcut launch, dark/English and light/Russian first launch, autostart/uninstall registration, custom-path uninstall, unrelated-file preservation, return to default directory and clean reinstall. Windows CI verifies install/update/autostart/uninstall and PNG/clipboard.

Exact locally tested v0.6.8-rc.3 installer SHA-256: 93484348cccbb12c6e3afab0f65511012dcfb26b85d02f1b9328e53b941c0340. Stable release reuses these bytes.