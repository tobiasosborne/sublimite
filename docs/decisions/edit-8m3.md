# edit-8m3: miniature character summaries

The old fill reduced each line to its non-space count and painted a contiguous
prefix. The replacement retains character positions in caller-owned row
summaries. Spaces leave holes; tabs advance to the editor's default tab stop.
The first line in a grouped row is representative. Large-file summaries retain
the existing bounded sample strategy and reuse its source reads. Total ink and
average density remain available for the existing summary/navigation contract.

(G) The existing clipped line bound is `MINIMAP_LINE_CAP`, currently eighty
columns. Mask storage derives as ceil(column cap / bits per uint64_t). Tab width
(G) comes from `editor/open.c`'s layout/view default, four columns. Glyph ink is
binary non-space coverage; no claim is made that narrow and wide glyphs differ.
UTF-8 bytes retain the module's existing byte-oriented approximation.

`minimap_pixels` renders prepared rows into caller-owned opaque RGB pixels,
without source reads or allocation. (G) Logical line spacing is two pixels,
from `perf/01-perf-target.md`'s minimap geometry. Each character occupies one
pixel on the first scanline; the following scanline is background. Long lines
clip at the lesser of the raster width and the existing summary column cap.
The viewport uses the same grouped line intervals as hit testing, with a
one-pixel perimeter (G: bead acceptance). Its interior leaves document marks
visible. (E) Interior coverage derives from the prior viewport coverage divided
by four: 128 / 4 = 32 out of the R8 maximum 255. Edge coverage derives as three
interior coverages: 3 * 32 = 96 out of 255. These are visual choices, not timing
budgets. The fill adapter uses the same wash and edge colours at cell resolution.

The editor colour struct reuses the established editor background, gutter ink,
and cursor foreground as minimap background, ink, and viewport. The stale colour
is unchanged. Colour derivations are the corresponding fields in
`editor/open.c` and the prior `minimap_style` initializer in `editor/paint.c`.

## Binding scope limitation

The editor map grid inherits the text font's cell dimensions in `editor/open.c`.
`editor/paint.c` copies those cells into the text renderer. (G: existing editor
wiring) Its strip has eight text cells across. Those operations cannot represent
sub-cell pixel marks or a pixel perimeter. The frozen render cell contract has
only a background colour and an atlas glyph reference, and the editor binds only
its text atlas. An exact miniature therefore requires map pixel/atlas binding,
composition, reserved storage, and matching pointer coordinate mapping outside
the authorized scope. The requested restriction to the colour struct is kept.

The PNG is a standalone module dump, **not an editor screenshot**. The running
editor still uses the coarse cell adapter, which now displays individual
character columns but cannot meet the requested physical geometry. Do not close
this bead or claim the Astra comparison passes until the coordinator resolves
that integration scope and re-runs the comparison drive. Navigation APIs and
cache/publication identities are unchanged.
