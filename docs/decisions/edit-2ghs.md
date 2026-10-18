# edit-2ghs: a binary (PDF) tab must paint, never end the editor

## Origin of -2

`-2` is `RENDER_ERR_BOUNDS` from `render_atlas_validate` (src/render/render.c:144-146, the `!glyph->w || !glyph->h` descriptor check), returned by `render_backend_submit` (render.c:324-325) into `submit` (src/editor/editor.c:404-405), then `editor_step` (editor.c:800) and `editor_run` (editor.c:824) to `main.c:199`. The offending descriptor was runtime slot 162 of 276, `{page 0, x 0, y 0, w 0, h 0}`. Its key was `ef 88 a7` (U+F227, private use), which a fallback face on this box draws without ink. The runtime glyph owner (`src/font/async.c`, edit-08z/r1l) stored a zero-sized rectangle for any settled image with no ink, and for any slot vacated by LRU eviction. It then bound the whole table, `ASCII + count` entries, to the grid. render.h requires every bound descriptor to be a real rectangle ("Empty glyphs use RENDER_NO_SLOT, rather than zero-sized rectangles"), and render_test pins that rule. So the first frame that admitted a blank cluster was rejected, and the editor exited. Cells never referenced the slot: the lookup already answered NO_SLOT for `w == 0`. The table alone was invalid. The check sits in the shared front end, ahead of `ops.submit`, so raster and GL fail the same way. This explains Tobias's GL death when he activated the restored PDF tab.

The repro depends on the viewport size. A 1024x768 Xvfb (:97) did not reach the blank cluster in its first screen and stayed up. A 2880x1800 Xvfb (:99) died in its first frame, as the coordinator saw.

## Fix

The producer is fixed; the render contract is unchanged. `async_request` now has an explicit `inked` flag: true while a request is pending behind its `?` placeholder or holds ink, false for a blank result or a vacant slot. Blank and vacant slots get `blank_descriptor`, which is the baked ASCII space rectangle (page 0, slot 0: a full cell, no ink) under the slot's own `glyph_index`. Lookups answer NO_SLOT from `inked`, not from `.w`. `editor_font_event` used `.w == 0` to retarget cells to NO_SLOT after a blank adoption; it now asks the new `font_async_inked()`. No allocation, no new storage beyond one bool per request, and no change on the typing path.

Rejected: relaxing `render_atlas_validate` to skip zero-sized descriptors. That would change a frozen, tested contract for both backends to hide one producer's bookkeeping.

## Capacity conditions in paint

These were checked, and none ends `editor_run`. Font admission with no legal victim returns `FONT_MORE`, and layout replays the cluster on the next slice (layout.c:448-456). `FONT_ERR_NOMEM` marks the row approximate. Unsupported scalars get U+FFFD artwork in `glyph_job`. Eviction was the second producer of zero-sized descriptors and is fixed above. A generic "drop the frame on any validation error" net was not added. The next step would build the same frame again, so the window would freeze without a visible error, and it would hide contract bugs like this one.

## Binary-content policy

No detection and no threshold: every file opens as text, as Sublime does without a hex package. Invalid UTF-8 and NUL bytes already reach the existing replacement-glyph paths (utf8/layout artwork, U+FFFD in `glyph_job`). With the fix above, that path cannot end the editor. An invalid-byte ratio rule would need a number with no physical derivation (law 4). It would also change nothing visible, because the text view is what the user gets either way.

## Crash-loop guard (verified, not changed)

A restore that fails while installing the session is set aside with the notice "session recovery failed; its journal was set aside" (recovery.c:529-533, open.c:288-300). A paint failure after a successful restore is not a restore failure. `main` keeps the journal, and the next launch restores the same tabs. Before this fix, activating the PDF tab therefore killed every launch. After it, the tab paints. A general guard for "died while painting a restored tab" would need a startup marker in the journal. That is outside this bead and is reported as a candidate.
