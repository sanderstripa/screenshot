# Screenshot v0.3.1

**Installer repair and visual polish.** The previous install wrapper could stop at 55% with no explanation.

- Replaced the nested silent installer with direct native per-user installation. No stock setup wizard, second executable or Inno Setup dependency.
- Safe in-place upgrade: requests graceful Screenshot shutdown, stops only a process at the known installation path when running an older version, and atomically replaces Screenshot.exe.
- Accurate progress; installation failures show the Windows error and affected stage rather than a fake percentage.
- Rounded window silhouette without black background corners. DPI-aware layout with crisp Segoe UI Variable text.
- Rebuilt Screenshot artwork in a reproducible vector-like process, including 16/24/32/48/64/128/256px icon frames.
- Shortcut selection after installation, with explicit confirmation when replacing Windows Print Screen handling.
- Correct Windows login autostart, installed program entry and native uninstaller.

Automated Windows CI verifies PNG processing, embedded executable integrity, first install, reinstall, upgrade while the previous Screenshot process is running, installed executable hash, autostart, and clean uninstall.

**Note:** The Windows setting for Print Screen can require signing out. Shortcuts reserved by other applications cannot be forcibly stolen. The capture experience on the user's physical monitor still requires hands-on testing. Binaries are currently unsigned, so SmartScreen may warn.
