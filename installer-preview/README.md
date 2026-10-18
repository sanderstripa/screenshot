# Screenshot installer 0.6.7 — visual review only

Open gallery.html for every screen, or index.html for an interactive visual walkthrough. No installation, shortcut registration, Windows changes or capture operations are performed. Bottom navigation is a review tool outside the installer frame.

Visual source: sanderstripa/connect main, cmd/installer/web/style.css and index.html, retrieved 2026-10-19. Montserrat, 840x540 frame, spacing and light/dark tokens follow that installer. Version is 0.6.7. Browser-extension UI is omitted. No new destination option is introduced. The review now includes the explicitly requested Desktop and Start menu shortcut switches, and the final Done action with a launch switch. These choices are interactive in the visual review; operating-system integration waits for approval. Screenshot's existing System theme option is retained.

Ribbon: user-supplied pink artwork, original retained in assets/ribbon-original.png; assets/ribbon.png is the transparent-background derivative. Geometry and pink shading were preserved in the background-removal pass and visually inspected on both themes.

Screens: welcome, settings, installing, installed, hotkey, explicit Print Screen consent, ready, failure. Preview theme/language selection and shortcut listening work locally. Progress and operating-system actions are deliberately visual only; the native installer remains unchanged until approval.

Checked 16 stage/theme renders, no frame overflow, RU/EN and theme controls. Screenshots are in screens/. render.cjs is a local review renderer and uses the workstation's Playwright/Chrome paths.

Approval gate: do not finish the Windows installer build or publish 0.6.7 before the user approves this visual review. No release workflow was changed or triggered by this branch.
