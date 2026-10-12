# edit-u82: editor background at the surface edge

Choose the strip-only strategy on each frame. Raster retains the grid in its
native pixmap; GL retains it in its framebuffer texture. Fill the right strip
through the full window height, then the bottom strip through the grid width.
Their shared corner belongs only to the right strip. Full grid painting plus
these strips covers the entire surface before presentation, including chrome,
sidebar and fractional text-scroll frames.

A clear only on geometry changes would require tracking the contents/age of
both native GL swap buffers. EGL does not promise retained swap-buffer contents.
Instead, GL uses scissored clears on the default framebuffer each frame and
blits the retained grid over its covered rectangle. Its former full-window
black clear is removed. Raster uses a native rectangle fill before scheduling
cell painting, without a SHM pixel scan or upload for the margins. Its former
full-reserve black clear at init/resize is removed. Exact-cell surfaces issue
no margin clears. Raster includes changed margin colour in the presentation
region even when cell or sidebar damage would otherwise restrict the copy.

`render_grid.surface_background` carries opaque RGB from the composed frame.
`editor_compose` takes it from `config_theme_color(theme, "editor.background")`
on every frame, including composition early returns. This avoids deriving it
from a decorated cell or the differently coloured tab strip. Existing render
callers default to black through zero initialization (P: zero RGB means black).
The common validator rejects values outside the opaque RGB channels.

The pixel cost is derived from surface geometry (P): for window width W,
height H, grid width w and height h, the disjoint area is
`(W - w) * H + w * (H - h) = W * H - w * h`. There are at most two rectangles
(P: one uncovered interval per surface axis), with no overlap and no new
allocation. The same formula applies to raster and GL, independent of dirty
cell count; exact-cell dimensions give zero pixels (P: both remainders vanish).

For the supplied screenshot (M)[AC], W = 1728 px and H = 1260 px at 243 DPI,
with remainder widths 8 px and 20 px. Thus w = 1728 - 8 = 1720 px and
h = 1260 - 20 = 1240 px (P arithmetic using the supplied measured inputs).
The strip cost is `8 * 1260 + 1720 * 20 = 10080 + 34400 = 44480` pixels per
frame (P). A full surface clear would touch `1728 * 1260 = 2177280` pixels
(P). These are pixel-work derivations, not measured runtime or a performance
budget. The screenshot is the bead's supplied evidence; the sandbox cannot
reproduce the live Sublime comparison.

Regression coverage models native raster rectangle filling and SHM uploads
around the actual raster backend and row kernel. Its minimal fixture has one
cell per axis, two pixels per cell side, and one remainder pixel per axis
(P: the smallest integer cell with a positive remainder smaller than a cell).
It checks all surface pixels, preservation of the differently coloured grid,
stable frames, theme changes, geometry shrink and allocation-free submission.
The shared native resize contract now expects the theme background on raster
and GL and alternates the existing dark/light palettes (P: the available
palette alternatives), pending the coordinator's display rerun.
