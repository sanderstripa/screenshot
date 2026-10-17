# Screenshot — CONNECT UI System v1.0

Reference: https://github.com/sanderstripa/connect/blob/connect-ui-system-v1-reference/docs/CONNECT-UI-SYSTEM-v1.md

The implementation extends existing PR #1. No replacement framework, screenshot encoding/capture, shortcut registration, startup, single-instance IPC, output directory or installation mechanism was introduced.

## Preserved tokens

| Token | Light | Dark |
| --- | --- | --- |
| Window | #F5F5F5 | #0A0A0A |
| Text / primary fill | #111111 | #F4F4F4 |
| Muted text | #75797F | #8B8E94 |
| Border | #D9DADC | #333639 |
| Secondary surface | #F9F9F9 | #141414 |

The embedded Montserrat font file is unchanged (SHA-256 `0f7b311b2f3279e4eef9b2f968bcdbab6e28f4daeb1f049f4f278a902bcd82f7`). Settings remain 520×382 logical pixels, setup 640×400. The original Screenshot artwork is retained.

## Shared components

Primary actions invert existing monochrome text/background colors. Secondary controls keep neutral surfaces and a 1px outline. Buttons and shortcut fields use 36px height and 8px corners. Keyboard focus uses a 2px ring, hover/press use the existing foreground with reduced opacity; there is no animation to conflict with reduced motion. Progress uses a 4px monochrome track.

Both shells keep only the wordmark, minimize and close, with native DWM rounding and no maximize button. Theme and language are accessed inside settings. The application persists RU/EN and System/Light/Dark; System follows AppsUseLightTheme and WM_SETTINGCHANGE. The installer keeps these choices throughout its wizard. Settings use the approved CONNECT SVG #8, rendered as a flat vector icon with no background.

## Approved settings icon #8

Original source: `sanderstripa/connect`, branch `connect-ui-system-v1-reference`, `docs/assets/CONNECT-settings-08.svg`. The copied asset has the same Git blob ID `c7a6eedf689fdb759133b35e487c35e4505af236` (SHA-256 `3f14c9094a96c13584a5514ef648a25cfbcb95371668036279ef4cb8051baf18`). No replacement gear is used.

`scripts/generate_settings_icon.ps1` translates the approved SVG path commands and circle into native Direct2D geometry, retaining the original coordinates, arcs, Bezier curves, even-odd fill and viewBox. CI regenerates the header and rejects any difference. Icon color uses the existing palette; there is no tile, mouse-focus outline or hover/press background. A focus indicator appears only during keyboard navigation.

## Verification scope

Windows tests exercise real native windows, language/theme selection, unchanged dimensions and palette, keyboard focus, minimize, Alt+F4, hotkey conflicts, Print Screen cancellation, installer update and disk/clipboard PNG. Test scripts restore the user's hotkey, language and theme preferences. Screenshots remain local.
