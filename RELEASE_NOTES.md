# Screenshot v0.6.7 — approved CONNECT installer

- Rebuilt the native installer visuals from the approved CONNECT installer reference: Montserrat, 840×540 layout, light/dark tones, supplied pink ribbon, shared footer and Screenshot version.
- Added independent Desktop and Start menu shortcut switches.
- The final Done action has a launch switch underneath; turning it off closes setup without opening the application window. The screenshot worker remains available for the configured capture shortcut.
- Kept installation/update, hotkey assignment and explicit Print Screen confirmation. No browser extension is included.
- Application UI and capture code are unchanged; app version metadata is 0.6.7.

Windows validation: six native stages × both themes × 100/150/200% DPI; RU/EN and theme switching; all four actual install/update shortcut combinations; shortcut targets; Print Screen consent cancellation; Done with launch off/on. Windows CI verifies clean install/reinstall/live upgrade, autostart, uninstall and PNG/clipboard.

The final release uses the exact locally tested v0.6.7-rc.2 installer, SHA-256: 4ddfab82fd07d25dfcb8a5e9078bc54a20a9cb307490d168e3abe2d95ed217de.