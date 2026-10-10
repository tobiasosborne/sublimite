# edit-9yd: estimated gutter colour

The gutter uses the configured exact foreground blended halfway toward its
configured background while the mapped buffer's line total is provisional or
its top line remains estimated. Marking the whole provisional gutter also
covers the initial open at the known first line, before a byte jump sets
`top_estimated`. Exact totals alone do not remove the style while viewport
correction is still pending.

Use dimming rather than a marker so digit glyphs, gutter width and text geometry
stay the same. The repository had no separate theme header; the small inline
colour policy lives in `src/editor/theme.h` and derives its colour from
`layout_config`, without a fixed extra palette or process globals.

`editor_paint_prepare` chooses the colour before its existing decoration
relayout writes the gutter. This adds only a scalar colour choice to frame
preparation: no allocation, cell/row traversal, layout pass, index query or
worker operation. Caret-only composition retains the prepared gutter. The
existing index-publication path requests full layout and corrects the estimated
top line before submission, so that frame restores the exact foreground.
No changes to `editor.c`, `input.c`, `open.c`, or index scheduling are needed.

The null-backend test inspects the submitted cell grid through `on_submit`,
including the first frame after publication. It reads the supplied log corpus,
restarts the index behind a held bulk job after mapping acquisition, and releases
the hold only after checking initial and byte-jumped provisional frames. Mapping
itself needs the bulk lane, so holding that lane before acquisition would
deadlock the test. Unfocusing before release excludes unrelated blink frames
from the frame-ID assertion. A second configured palette proves theme derivation;
release allocation guards cover an unindexed arrow and normal typing.

Red/green output and final verification are in
[the worker report](../worker-reports/edit-9yd-s9.md).

## Loaded paint-path comparison

The existing null typing row was called for both paint variants back to back
through a build-only wrapper, using the same generated fixture. Each variant
completed 10,000 keys (M)[AC] with zero allocations (M)[AC]. BAT0 was
`Not charging`. Injection-to-T4 p50/p99 in ms:

| Variant | p50 / p99 (M)[AC] | Load1 (M)[AC] | Initial index state |
|---|---:|---:|---|
| Main paint | 1.904 / 6.199 | 3.03 | published |
| Current paint | 2.689 / 3.293 | 5.60 | progressing |

The different load/index states prevent a latency conclusion. These are TRACK
rows, with no timing acceptance verdict. The full null/raster driver reached
its per-variant execution limit in raster in both variants; that renderer is
outside scope and was not tuned. Structural cell/allocator assertions establish
the scoped behaviour; complete commands and limitations are in the report.
