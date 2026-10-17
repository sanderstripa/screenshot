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

Both shells keep only the wordmark, minimize and close, with native DWM rounding and no maximize button. Theme and language are accessed inside settings. The application persists RU/EN and System/Light/Dark; System follows AppsUseLightTheme and WM_SETTINGCHANGE. The installer keeps these choices throughout its wizard. Text settings actions are used pending the approved icon.

## Approved gear #8 unavailable

The original approved ten-variant sheet / gear #8 graphic is not present in the supplied project references, Screenshot assets or the CONNECT reference tree. No arbitrary gear or newly invented SVG is used. The missing original is the sole icon integration limitation; settings remain fully accessible through text actions.

## Verification scope

Windows tests exercise real native windows, language/theme selection, unchanged dimensions and palette, keyboard focus, minimize, Alt+F4, hotkey conflicts, Print Screen cancellation, installer update and disk/clipboard PNG. Test scripts restore the user's hotkey, language and theme preferences. Screenshots remain local.
