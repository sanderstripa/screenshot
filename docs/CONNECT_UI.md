# Screenshot — CONNECT UI System v1.0

Reference: https://github.com/sanderstripa/connect/blob/connect-ui-system-v1-reference/docs/CONNECT-UI-SYSTEM-v1.md

The implementation extends existing PR #1. On 2026-10-18 the user explicitly requested alignment of Screenshot tones with CONNECT VPN, superseding the earlier palette-preservation constraint. Active colors come from CONNECT frontend/src/style.css; preference dimensions and selection behavior follow frontend/src/styles/connect-ui-v1.css. No replacement framework, screenshot encoding/capture, shortcut registration, startup, single-instance IPC, output directory or installation mechanism was introduced.

## Active CONNECT VPN palette

| Token | Light | Dark |
| --- | --- | --- |
| Window | #F7F7F5 | #0C0C0D |
| Text | #171A1D | #EDEDEE |
| Primary fill | #171A1D | #E6E7E9 |
| Muted text | #80858B | #81858C |
| Border | #DADBDC | #303236 |
| Quiet selected surface | #EEEEEE | #1A1B1E |

The embedded Montserrat font file is unchanged (SHA-256 `0f7b311b2f3279e4eef9b2f968bcdbab6e28f4daeb1f049f4f278a902bcd82f7`). Settings remain 520×382 logical pixels, setup 640×400. The original Screenshot artwork is retained.

## Shared components

Primary actions invert existing monochrome text/background colors. Secondary controls keep neutral surfaces and a 1px outline. Buttons and shortcut fields use 36px height and 8px corners. Keyboard focus uses a 2px ring, hover/press use the existing foreground with reduced opacity; there is no animation to conflict with reduced motion. Progress uses a 4px monochrome track.

Appearance settings use the same Lucide Monitor/Sun/Moon/Palette/ArrowLeft shapes as CONNECT. RU/EN are compact 32×28 controls at the right of the appearance heading; theme choices are 32px high with subdued selected fill and a thin muted border. Installer gear and Install share the same vertical center (341 logical pixels).

Both shells keep only the wordmark, minimize and close, with native DWM rounding and no maximize button. Theme and language are accessed inside settings. The application persists RU/EN and System/Light/Dark; System follows AppsUseLightTheme and WM_SETTINGCHANGE. The installer keeps these choices throughout its wizard. Settings use the approved CONNECT SVG #8, rendered as a flat vector icon with no background.

## Approved settings icon #8

Original source: `sanderstripa/connect`, branch `connect-ui-system-v1-reference`, `docs/assets/CONNECT-settings-08.svg`. The copied asset has the same Git blob ID `c7a6eedf689fdb759133b35e487c35e4505af236` (SHA-256 `3f14c9094a96c13584a5514ef648a25cfbcb95371668036279ef4cb8051baf18`). No replacement gear is used.

`scripts/generate_settings_icon.ps1` translates the approved SVG path commands and circle into native Direct2D geometry, retaining the original coordinates, arcs, Bezier curves, even-odd fill and viewBox. CI regenerates the header and rejects any difference. Icon color uses the existing palette; there is no tile, mouse-focus outline or hover/press background. A focus indicator appears only during keyboard navigation.

## Verification scope

Windows tests exercise real native windows, language/theme selection, unchanged dimensions and palette, keyboard focus, minimize, Alt+F4, hotkey conflicts, Print Screen cancellation, installer update and disk/clipboard PNG. Test scripts restore the user's hotkey, language and theme preferences. Screenshots remain local.

Theme SVGs match the actual locked lucide-vue-next 0.468.0 npm package and render at 13px. scripts/verify_vpn_theme_sources.ps1 verifies exact shape definitions; scripts/generate_theme_icons.ps1 generates native geometry directly from SVG.
