# edit-36b3: app icon é

2026-10-26, session 23, Tobias present.

## Choice
Tobias: "the icon sucks, stupid single lower case s". Two rounds of workups in `docs/design/icon-workups-2026-10-26/` (five typographic, five pictorial); he chose **04, the é**: a Bodoni Moda Italic e (the title face, PRD §11) on the editor's dark tile #222226 (edit-29y2), cream #efeae0, the acute in Mint-Y-Dark-Aqua #1f9ede. The old icon was a near-black s on a transparent ground and vanished on dark panels.

## Implementation
- `tools/mkicon.py` (pycairo) generates `tools/sublimite.svg` and `tools/icons/sublimite-{16,24,32,48}.png`. The outputs are committed and the font is not: Bodoni Moda is OFL from google/fonts and only needed to regenerate (docstring has the fontconfig line).
- The SVG holds three plain `<path>`s (tile, e, acute), glyphs converted to outlines, so no font is needed where it is installed. tools/test_install.sh rejects `<text>`, `<use>` and `<symbol>` (cairo's own SVG output uses glyph symbols, which is why the generator writes path data itself).
- The bitmaps are tuned per size (table `TUNING`): less tile margin, a larger e and the bold instance at 32 px and below so the Didone hairlines survive on a panel. Icon themes take an exact-size PNG over the scalable SVG at these sizes.
- `make install` puts the PNGs in `hicolor/NNxNN/apps/sublimite.png` (ICON_SIZES 16 24 32 48); `make uninstall` removes them. tools/test_install.sh compares each installed PNG with its source and checks uninstall.

## Verification
Red: test_install stopped at `cmp: tools/icons/sublimite-16.png: No such file or directory`. Green: `test_install: release binaries, desktop validation, icon, help, MIME default and uninstall passed`. No C changed. Installed with `make install`.

## Not done
The empty-state wordmark (edit-457.20) is a separate bead; the window's own `_NET_WM_ICON` is not set (Cinnamon takes the icon from the desktop entry via WM_CLASS).
