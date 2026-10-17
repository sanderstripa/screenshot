# Screenshot v0.6.0 — CONNECT UI System v1.0

- Extended existing PR #1 rather than duplicating its button hierarchy.
- Custom wordmark/minimize/close shells; no maximize control or redundant titlebar icon.
- Contrasting primary buttons, neutral secondary controls, 36px inputs/buttons, 8px corners, hover/press feedback and 2px keyboard focus rings.
- Theme and language are inside settings, with working System/Light/Dark and RU/EN choices. App preferences persist; system theme responds to Windows changes.
- Native confirmation, error notices and 4px monochrome progress follow the same components.
- Montserrat, the original light/dark palette, 520×382 settings window, 640×400 setup window, approved Screenshot artwork, capture output, shortcut registration, startup and installation mechanism are preserved.

The approved gear #8 source image was unavailable in the supplied materials and reference repository. No replacement gear was invented: settings use text actions until the original can be integrated.

Verified on Windows 11 build 26100: update over the previous version, actual wizard interactions, conflict notice, Print Screen cancellation, shortcut reassignment in the existing process, persisted language/theme, System mode, keyboard focus, minimize, Alt+F4, titlebar drag hit testing and PNG disk/clipboard. All six setup screens in both themes and languages at 100/125/150/200/250% were captured (120 combinations); settings also passed all five scales. See WINDOWS_TEST_REPORT.md.

SHA-256 of the exact tested installer: `4b50e4452066a0ca752c8e971b9a8dcdda7ec1d5ff2e6d45663b90bcfb0800c9`.
