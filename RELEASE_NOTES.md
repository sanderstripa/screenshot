# Screenshot v0.6.2 — approved CONNECT settings icon #8

- Replaced settings text actions in the installer and app with the exact approved `CONNECT-settings-08.svg` from `sanderstripa/connect`, branch `connect-ui-system-v1-reference`.
- No background tile or mouse-focus outline. Keyboard navigation retains its focus indicator.
- The original SVG is retained byte for byte (Git blob `c7a6eedf689fdb759133b35e487c35e4505af236`). Its curves, arcs, circle, fill rule and viewBox are converted directly to native Direct2D geometry and checked by CI.
- Existing startup permission fixes, Montserrat, palettes, dimensions, capture behavior and shortcuts are preserved.

Tested on Windows: actual icon clicks, both themes, settings persistence and shortcut reassignment, 100–250% scaling, installer update/settings flow, window controls and PNG disk/clipboard output.

Exact tested installer SHA-256: `06c0583cddc98402152ce8abd0b2d90e30bd07cc29bf96010ddfa7fa2652491b`.
