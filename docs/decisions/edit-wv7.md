# edit-wv7: wrapped placement never labels the top with a line query from byte 0

## Root cause

Wrapped Ctrl+End on an unindexed lazy file ran `view_command(VIEW_DOC_END)`
(the editor's `large_doc_end` and `relative_repair` both excluded wrapped
buffers). The cursor is placed directly, but `finish_move`'s wrapped branch
then labels the new top with `line_at(visual_byte)` + `line_start(line)`: a
cooperative line query counting LFs from the nearest certified anchor. While
the index is incomplete there is none near EOF (`editor_seed_view_lines` only
installs the laid-out top, which was at byte 0), so the query counted the
whole file in 3840-byte slices, re-running `layout_visual_row` on every slice.
Measured (red, `EDIT_WV7_REPRO=1`): release, line_scanned = 1,073,741,824 B
(2^30, the whole fixture) and a 7025 ms settle; ASan, overdue at the 10 s
watchdog after 783,085,056 B. The dz0 wrap=1 sanitizer timeout was this.

## Decision

1. **Relative wrapped placement in view** (`relative_wrap_place`, phase
   `P_REL_WRAP`). When `v->relative` is set and the buffer is wrapped,
   `finish_move` first proves the cursor's line start by walking back to LF
   (edit-jf9's `relative_start`, counted in `seek_scanned`). The visual row
   query stays local: if the cursor lies within `LAYOUT_BYTE_BUDGET` (P) of
   layout's certified top (the same condition as layout's `resident_seed`),
   the ordinary `layout_visual_row`/`_fresh` (resident rows or seed, no piece
   line query); otherwise `layout_visual_row_anchored` at the walked start,
   which avoids the whole-source `piece_byte_to_line` (lazy newline counts on
   an unindexed mapping). The top-row choice is the existing wrapped logic
   unchanged. The new top's line start is the chosen row's certified
   `line_start`; no line query runs.
2. **Label, not ordinal** (edit-jf9/dz0 rule). If the top stays on the same
   logical line its label and estimate marker are kept; byte 0 is labelled
   line 0; otherwise `first_line` keeps the previous label, `approximate` is
   set and the new runtime output `view.relative_top` asks the host to
   relabel. No estimate enters the view's line caches.
3. **Host relabel** (`editor_large_relabel_top`, called at the start of
   `refresh_cursor`, i.e. before any layout reads the label, for both
   `editor_refresh_cursor` and `editor_refresh_placed`): the index ordinal when
   exact, else the existing density estimate with `lg.top_estimated` set, so
   `editor_seed_view_lines` never installs it as a certified anchor and
   `index_progress`'s wrapped path relabels it exactly at publication.
4. **Routing.** `view_relative_command` accepts a wrapped view (bound wrap
   layout) for its existing keys plus document Home/End and Select All (every handle_key motion, so no
   wrapped key falls back to the exact path once the top is an estimate);
   wrapped keys run
   view_command's wrapped command path (layout owns visual rows), word/select
   keys prove their metadata first (`P_REL_META`), wrapped Home/End use the
   visual row. `view_relative_restore` accepts wrapped state that matches the
   view's wrap flag and layout. The editor's `relative_repair` predicate is
   now `lazy && !lines_exact` (the `!wrap` term is dropped), and document
   Home/End route relative when wrapped. This supersedes edit-dz0 decision 4
   ("wrapped buffers keep the exact repair"): with the top now an estimate,
   the exact repair would count from byte 0 on the next key. Small files are
   not lazy, so their paths are unchanged. The unwrapped path (ptn,
   `large_doc_end`) and the index builder are unchanged.

## Bound (per key, lazy wrapped file before the index completes)

- line_query bytes: 0 on the routed keys and the typing repair.
- seek bytes: the walk from the cursor back to its line's LF (one 256-byte
  quantum (P: relative_start's read) for the fixture's 64 B rows), plus the
  existing ptn seeds. A very long last line costs its own length in
  cooperative slices, as edit-jf9/ptn record; never the file prefix.
- Asserted in `tests/editor_large_test.c` `wv7_case`: line_scanned +
  seek_scanned <= VIEW_SCAN_BOUND = 4096 (P) for Ctrl+End, a key at EOF,
  PageUp, Ctrl+Home, Ctrl+End, Ctrl+A, Ctrl+End; after the index publishes, the key's byte is
  at EOF, the cursor follows it, the top is labelled exactly and every laid
  out wrap row carries its exact logical line (no stale rows).
  `tests/view_test.c` `wrapped_relative` compares the relative and exact
  wrapped document End on twin trees (same cursor, first_byte, visual_byte;
  zero line_query bytes; no allocation under `edit_malloc_guard`).

## Not covered (reported)

- The first wrapped PageUp after Ctrl+End settles in 240-280 ms release,
  938-1135 ms ASan (M), with 256 probe bytes; a second PageUp takes 0.047 ms.
  It is layout's upward row query at the viewport top (`layout_visual_row`
  direction -1 at `first_byte` drops the resident anchor, so `scan_row` calls
  `piece_byte_to_line`, which resolves the mapping's lazy newline counts once
  on the UI thread). Layout/piece work, outside this bead.
- `editor_full_layout` asks `layout_visual_row(visual_byte)` when the top row
  is a continuation (`visual_byte != first_byte`, a long wrapped last line
  without a trailing LF); off the resident budget that also reaches the piece
  line query. Not hit by the fixture (EOF follows an LF).
- Unwrapped Select All on a lazy unindexed file still runs `follow_line`'s
  `line_at(cursor)` (not routed relative; outside this wrapped bead).
