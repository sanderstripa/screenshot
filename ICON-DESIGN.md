# Approved circular Screenshot icon

The user-approved circular graphite icon with pink screenshot corners is stored in `assets/Screenshot-approved-original.png`. Preserve this artwork exactly. The exterior is transparent. `Screenshot.png` is the identical application/installer master; `Screenshot.svg` references that same master without redrawing it.

Run `python scripts/make_icon.py` to package all nine Windows ICO sizes (16, 20, 24, 32, 40, 48, 64, 128, 256). The historical family renderer also reads this approved master for Screenshot rather than regenerating the old square artwork.

The app and installers embed this ICO. The shared installer embeds the PNG and installs content-addressed ICO files for desktop/Start menu shortcuts and the uninstall entry, refreshing the shell icon cache.
