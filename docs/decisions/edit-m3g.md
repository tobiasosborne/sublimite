# edit-m3g: display-derived font sizing and worker zoom

The launch configuration reads the root `RESOURCE_MANAGER` resource database
for `Xft.dpi` (including the Xft wildcard/class spellings). A valid positive
resource value takes precedence over physical DPI, respecting configured desktop
scale. Otherwise, the primary active RANDR output supplies pixel dimensions and
physical millimetres; the first active output is the fallback when no primary is
set. Missing output millimetres retain the root's physical dimensions only when
that output covers the root, which preserves Xvfb's configured DPI. Without usable
physical dimensions, the conventional Xft fallback is 96 DPI (E, desktop convention).

Physical DPI is `hypot(width_px,height_px) * 25.4 / hypot(width_mm,height_mm)`
(P: millimetres per inch). For the bead's panel,
`hypot(2880,1800) / 14 = approximately 243 DPI` (P: supplied resolution and diagonal).
The default is 10 pt (E: the bead's Sublime reference), so
`10 * 243 / 72 = 33.75`, rounded to 34 em pixels (P: points per typographic inch).
`font_set_em_px` uses units-per-em scaling. The old `font_set_px` deliberately
keeps its ascent-plus-descent pixel-height semantics for existing callers/bakes.

The default window uses the current EWMH desktop work area intersected with the
selected output. Without a work-area property, it uses the output extent.
Width is floor(work-width * .60), height floor(work-height * .70) (E: the bead's
launch policy). On the supplied panel without exclusions this gives
`2880 * .60 = 1728` and `1800 * .70 = 1260` pixels. Native surface storage is
reserved to the work-area pixel extents, independent of cell size. Existing grid
reservation bounds remain in effect; the CPU's existing frame cell limit also
caps rows at extreme zoom-out, rather than failing submit.

The vendored DejaVu outline bytes are embedded as immutable C data, regenerable
with `python3 tools/embed_font.py`. Font setup and ASCII rasterization execute on
the foreground work-pool worker. INIT reserves both atlas versions: the frame's
current image and the worker's replacement. Each has
`ASCII_count * maximum_cell_w * maximum_cell_h` bytes; ASCII_count is
`0x7e - 0x20 + 1 = 95` (P: the existing atlas character range).
Scratch inherits the existing atlas baker's `1 << 20` byte arena (E: existing
`tools/atlasbake.c` reservation, not a new timing budget). Zoom does not allocate
heap storage or perform font I/O.

The maximum whole em is derived from the existing GL reference-pixel bound,
262144 pixels (G: `GL_FRAME_PIXEL_LIMIT` in `src/gl/renderer.inc`). DejaVu has
2048 units per em, ascent 1901, descent -483 and ASCII advance 1233
(P: the vendored font's head/hhea/hmtx tables). At em 62, rounding gives
advance 37, ascent 58, descent 15, so the complete ASCII band uses
`95 * 37 * (58 + 15) = 256595` pixels. Em 63 gives
`95 * 38 * (58 + 15) = 263530`, which exceeds that bound. Thus the atlas cap
is 62 em pixels. The minimum em is 1 pixel (P: the smallest positive whole em accepted by
`font_set_em_px`; the vendored font rounds its cell advance/ascent to 1 pixel
at this size). These bounds select prepaid resources;
no elapsed-time budget is invented.

Ctrl+= and Ctrl++ increase by one point, Ctrl+- decreases by one point
(P: existing Sublime binding arguments). Ctrl+0 resets to this launch's default,
as requested by the bead, replacing the old unimplemented sidebar binding.
`EDIT_FONT_SIZE_PT` overrides the launch's point size; reset restores that override.
Values must be finite and positive, and raster size stays inside the atlas bounds.
A persistent `font.size` preference is left to the config bead.

Zoom stores the latest requested point size, coalesces while a job is running,
and authenticates completion with the work slot/epoch. It adopts only after
physical job completion and backend frame retirement. It then updates the cell
dimensions, glyph table and compact layout, and schedules full damage in the
same turn. There is no frame timer, artificial delay, UI-thread rasterization or
UI wait for a zoom job. Actual frame parity with Sublime still requires the drive.

Only ASCII atlas sizing is changed. The separate Unicode runtime-owner bead
must use the same em scaling when composing primary and fallback fonts.
Runtime DPI changes after moving a window between outputs and persistent font
preferences are outside this startup/zoom bead.
