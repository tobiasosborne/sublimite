# edit-vp4m: unwrapped Return on the bottom row left the caret on the hidden row for one frame

Bead edit-vp4m (found by the edit-6090 worker, s9). Code:
`src/editor/scrollctl.c` `editor_scroll_follow` (the resident fast path).
Test: `tests/editor_test.c` `vp4m_return_bottom` (`--vp4m-return`, in the
suite after `n3f2k_save_with_results`).

## Cause

`editor_scroll_follow` runs after `layout_edit` and before any relayout
slice. Its fast path looks for the caret in `e->row_byte[]` and, when a row
contains it and scroll's policy keeps the top, adopts the current top.
`layout_edit` (unwrapped) shifts the later row starts only for an edit that
keeps the newline count (`shift_rows`); for a newline-changing edit it marks
rows `r..rows` for relayout and leaves their starts pre-edit. A Return typed
mid-line on the bottom visible row then leaves the stale bottom row spanning
the caret (old start ... old next-line start), the fast path keeps the top,
and the relaid frame shows the caret's new row on the hidden row below the
viewport until the next key's view follow corrects it. (A Return at the end
of the line happens to escape: the stale next row starts exactly at the
caret and lies outside the visible rows, so the fast path finds nothing.)

## Choice

Trust a row in the fast path only when both its start and its end are
certified: while the layout is busy, a `row_byte[k]` with
`lay.row < k < lay.row_end` is queued for relayout and may be pre-edit, and
the hidden row's end (`lay.pos`) is mid-scan. Rows `<= lay.row` (already
laid, or the edit's own row whose start the edit cannot move) and rows at or
after `row_end` are kept. When the caret lies in no certified row, the
existing fallbacks run unchanged: the view-certified follow (`e->v.line`,
exact line counts) or the pending resolver.

Not chosen: transforming `row_byte` in `layout_edit` (needs the inserted
text's newline positions inside layout, a layout-module change for the same
outcome; the rows are relaid anyway).

Cost (P): one comparison pair per row of the existing loop, no allocation.
Ordinary typing is unaffected: a same-newline edit queues only its own row
(`row == lay.row`, `row_end == row + 1`), so the caret's row stays certified
and the fast path still adopts the top without a raw-source window.

## Not changed

- `view.c relative_wrap_place` (lazy large files, inexact count): the same
  stale-rows pattern, noted by edit-6090; out of this bead.
- With an inexact line count (large file still indexing) the fallback is
  the asynchronous resolver, so the caret's row may be corrected a frame
  later there; the view-certified path needs `lines_exact`.
