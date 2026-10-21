# Worker report: design-workups-s10 (visual design workups, 2026-10-21)

The baseline screenshot was taken at 08:39 [AC]:

```
$ cat /sys/class/power_supply/BAT0/status
Charging
$ uptime
 08:39:25 up  9:46,  1 user,  load average: 0,53, 0,58, 0,72
```

## Produced

All under `docs/design/workups-2026-10-21/`:

- `README.md`: the comparison table, one section per workup with palettes and contrast ratios (P), typography derivations, memory and typing-path pricing, what each rules out, and which modules each touches.
- `baseline.png`: today's installed build at 2880×1800, window resized to full screen. `baseline-default-window.png` shows the default 1728×1260 window. Both were captured on a private `Xvfb :96 -dpi 243 -noreset` with `XDG_DATA_HOME=/tmp/claude-design-profile`. The editor, `xprop` and Xvfb I started were all killed afterwards.
- `01-quiet-tokens/{dark,light}.{svg,png}`: tokens only.
- `02-calm-chrome/{dark,light}.{svg,png}`: tokens plus layout (2-row tab bar, dot, status row, gutter, minimap off, bar caret).
- `03-type-and-air/dark.{svg,png}`: Iosevka 34 px with line height 1.45, plus hairlines.
- `04-rail/dark.{svg,png}`: left file rail, breadcrumb with status, overview ruler, no tab bar.
- `05-floating/dark.{svg,png}`: Inter label tiles, rounded tab, syntax-texture minimap, and the palette on a new RGBA overlay primitive.
- `contact-sheet.png`: today plus all seven mocks, labelled.

**How they were rendered.** `rsvg-convert`, `inkscape` and `magick` are not installed (M: `which`), and ImageMagick 6's internal MSVG renderer is not faithful. So each mock is drawn by one pycairo script into both an SVGSurface (glyphs embedded as outlines) and a 2880×1800 PNG. All fonts used are installed: DejaVu Sans Mono, Iosevka and Inter. The script is `/tmp/claude-1000/-home-tobias-Projects-sublimite/e2074f97-4943-44fa-93ff-ae687b7a6644/scratchpad/gen.py` (contact sheet: `.../scratchpad/sheet.py`). It is not in the repo, because the brief allowed docs and images only. Copy it to `docs/design/workups-2026-10-21/` if you want the mocks to be reproducible.

## Not done

- No light mocks for 03–05. The brief asked for light versions of the two most conservative workups only.
- No empty-state, find-panel or palette-closed variants were drawn. Each README section describes its empty state in words.
- None of JetBrains Mono, Geist Mono, IBM Plex Mono or Commit Mono is installed (M: `fc-list`), so none could be rendered. 03 proposes Iosevka, which is installed, and renders it faithfully.
- No benches were run. All per-frame costs are (P) arithmetic or are marked (E).

## Recommendation

**02 Calm chrome, landed as two beads: 01's tokens first (one file, `theme.c`, zero risk), then 02's layout.** 02 fixes everything that reads as rudimentary today: the flat tab bar, the stray `|`, the missing status line and the barcode minimap. It needs no new primitive, frees about 1 MB (P), and adds no allocation to typing. 03's Iosevka can be trialled before any code: `config.h` already takes `font_file` with a TTC index. A `font.line_height` key is the only missing piece for the 1.45 look, and Tobias should choose a line height on the real panel because 1.45 costs a quarter of the rows. 04 is a good alternative if tabs feel wrong after 02. 05 is worth it only if the palette becomes central, because it reopens the frozen `render.h`.

## Wrong on today's screen (M, from baseline.png; nothing filed)

1. **No status line at rest.** The bottom row is a transient prompt row only (`src/editor/paint.c:187`). Nothing on screen shows the path, line or column.
2. **A stray-looking `|` after the active tab.** It is drawn at x = 308–311 in `tab.active.foreground`. This is the deliberate "final separator" (`src/tabs/tabs.h:111`), but it reads as a second caret.
3. **The active tab reads as a button, not as the page.** It is `#384356`, lighter than the page `#20242C` (1.6:1, P), and its label sits about 10 px from the top of a 40 px bar.
4. **The minimap is a barcode and the loudest thing on screen.** All 1,745 lines are squeezed into 1,760 px, and 1 px rows alternate ink and background (column scan at x = 2725). The ink is `#8091A8` at 4.8:1. The viewport box spans the full 160 px region while the ink is at most 73 px wide.
5. **The syntax hierarchy is inverted.** Comments are 6.8:1, louder than keywords (6.3:1) and functions (6.4:1) (P, from `theme.c`).
6. **The default window does not fill the screen.** It opens at 1728×1260 at 0,0: 60 % × 70 % of the screen (M: `xwininfo`).
7. **The caret is a full block**, not §7's 2 px bar, and code starts one cell after the gutter digits.
8. **EGL init fails on Xvfb** (`EGL/GL/Present unavailable (code=-10); using raster`). This is expected without a GPU, so the baseline shows the raster backend.

## Notes for the coordinator

- **Rows are uniform.** This holds for every future chrome brief: margins are right and bottom only (`src/raster/raster.c:336`). "1.6× line height" bars and "1.5-cell" gaps cannot be expressed. They become 1× or 2× bars and 1- or 2-cell gaps. Split-glyph centring (README) is the cheapest in-contract answer.
- **I read display `:0` once.** I ran one read-only `xdpyinfo -display :0`, which reported 96×96 DPI, before deciding to rely on the brief's 243. Nothing else touched `:0`.
- **I left another editor running.** A second `sublimite` was already running (`/tmp/probe.txt`, pid 14825). It was not mine, so I left it alone.
