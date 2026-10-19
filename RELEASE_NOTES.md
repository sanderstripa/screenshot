# Screenshot v0.6.9 — VPN installer settings and stable path editing

- Matched installer settings to CONNECT installer CSS: 12px semibold Language/Theme labels, 11px regular choices, 13px shortcut labels/Back, correct heading alignment, selection borders and measured RU/EN control bounds.
- Confirmed the Montserrat font is the exact VPN installer asset (Git blob c97aca18592834d8706549c279cb8d5ac5d85f69). Existing installer palette already matches the reference CSS and is retained.
- Removed the extra language/appearance explanation and installer System choice. The application's existing System theme remains available.
- Fixed path field flicker/jumping: no repeated move/show/erase operations on mouse movement. Refresh happens only on actual DPI/theme/visibility changes; the parent clips child controls.
- Preserved folder selection, shortcuts, update/uninstall and first-launch theme/language inheritance.

Windows validation: settings in both themes and languages, 100 mouse-move messages per case with unchanged editor bounds/selection; native screenshots inspected. Actual custom-path install/update, launch via Unicode shortcut, appearance inheritance, custom uninstall, unrelated-file preservation and default clean reinstall. Windows CI validates installation/update/autostart/uninstall and PNG/clipboard.

Exact tested v0.6.9-rc.2 installer SHA-256: d3b1acd89a3383418955ec6e650320301de5f9bf7be52cbb369589d967175be0. Stable reuses these bytes.