# edit-a7fo — mouse click lands at the row edge with word wrap on

## Cause

With the default config, word wrap is on for untitled buffers and `.md`,
`.txt` and `.tex` files (`view_wrap_default`, `src/view/view.c`), off for
other extensions. A trial on such a file runs the wrapped layout
(`src/layout/wrap.c`); on a `.c` file the bug does not occur. Pointer hit
testing (`src/editor/input.c` `pointer_byte`) reads per-cell `editor_hit`
records that layout writes through `layout_config.hit`. The wrapped layout
writes them in three places: `start_row` prefills every cell of the row with
the row's start byte (and the gutter with the line start), `end_row` fills the
cells after the text with the row's end byte, and the general cluster path
writes one record per cluster. The printable-ASCII fast path `ascii_span`
(the path every ASCII byte takes) wrote cells but no hit records. So every
cell of ASCII text kept the prefill: a click anywhere on ASCII text put the
caret at the start of the clicked row, a click right of the text put it at the
row's end, and a drag moved the caret only between row edges, so it selected
whole rows.

Not the cause (checked on a private Xvfb at 2880x1800 with `Xft.dpi: 192`, the
trial display's geometry and resource from `xdpyinfo`/`xrdb -query` on :0):
XI2 coordinates (event_x FP16.16 at wire offset 40, decoded correctly), cell
size (16x31 px at 192 dpi, used consistently by paint and pointer), gutter
classification (gw=2, clicks were at text columns), the menu-bar row offset
(menu off by default; `tab_rows` includes it when on). gdb on the real binary
showed `hits[row 1][2..27] = {before=27, after=27}` (the row start) where each
cell should carry its own byte.

The unwrapped layout's ASCII fast path (`src/layout/layout.c`) has always
recorded one hit per byte, which is why `mouse_contract` (wrap off) passed.

## Fix

`ascii_span` records one `(before=at+i, after=at+i+1, within=0, width=1)` hit
per drawn ASCII cell, exactly as the unwrapped fast path does, only when the
row is being drawn (not in the sink or planning passes). The record writes
into the editor's arena-backed `hits` table: no allocation on the typing path.
The cost is one indirect call per drawn ASCII byte, the same as the unwrapped
path already pays.

## Side effect

`src/editor/highlight.c` builds syntax-highlight spans from the same hit
records and skips records with `after <= before`. With wrap on, ASCII text had
no usable spans; it now gets them as the unwrapped path always did.
