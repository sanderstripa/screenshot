# Screenshot v0.5.0

- Open Screenshot from the Windows Start menu to change your shortcut.
- Shortcut changes apply immediately in the running background process; occupied shortcuts preserve the current working key.
- Persistent light and dark themes, with one unlabeled toggle beside the close button.
- Hotkey text centered within its field.
- Settings and native installer share Montserrat, the #0A0A0A background and neutral outlines and buttons.
- Supplied icon retained; Windows owns rounded corners and shadows.
- Login startup remains invisible, without a tray icon. Screenshots still save the same PNG to the clipboard and %USERPROFILE%\Pictures\Screenshot.
- Explicit confirmation before replacing Windows Print Screen behavior.

Tested on Windows 11 build 26100: update over the previous installation, Start menu launch into the existing process, live reassignment, occupied-key rejection, Print Screen cancellation, theme persistence, both settings themes at 100–250%, all six installer stages at 100–250%, and PNG disk/clipboard output. Windows CI verifies installation, live upgrade, autostart, shortcut creation and removal, and uninstall.

SHA-256 of the exact tested installer: `4a0e4caaca351728231e14d0988021868ebe50e9e5ba1959c70f086606ca75cf`.

The installer is self-contained and unsigned.
