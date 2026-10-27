# edit-cxho.26: cached wrap rows say how many of them are current

Bead edit-cxho.26 follows edit-cxho.25 (worker session s24). The tests are in
`tests/wrap_test.c` (`c26-restart`, `c26-reset`, `c26-follow`) and
`tests/view_test.c` (`c26-resident`, `c26-anchored`, `c26-reset`). Worker
report: `docs/worker-reports/edit-cxho.26-s24.md`.

## Hazard

`layout_wrap_begin` zeroed `wrap_rows` and left `row_byte[1..]` stale. A
layout restarted while busy (a second key before a frame) therefore had rows
1..n-1 that read as real rows at byte 0, with `next == 0`. edit-cxho.25 made
finish_move and finish_edit avoid them. Two more states had the same problem.
After `LAYOUT_RESET` (an edit before a continuation top on its own line, or an
edit across the top's start), the layout is idle and holds pre-edit rows that
are not shifted. The host lays out again only on its next turn, and queued
keys run before that. While planning partway through a line, rows from the
edited line onward are pre-edit too.

## Decision

1. **Empty, not zero.** The restart marks rows 1..n-1 as
   `{start, end, next} = LAYOUT_VOID_ROW` and `row_byte[1..] = LAYOUT_VOID_ROW`,
   the same "nothing here" shape that rows past EOF already use. Readers that
   skip void rows (paint, minimap, find visibility, hit testing) see no rows
   instead of rows at byte 0. Cost (P): the same n stores as the old memset.
2. **One question for every reader: `layout_wrap_current_rows(l, t)`.** It
   returns k, the number of leading rows that describe `t` as it is now:
   - n when the layout is idle;
   - the row being laid out while it is busy;
   - the edited line's first row while planning;
   - 0 for another source, a changed length, `!first_line_valid`, or the new
     `wrap_stale` flag. `layout_wrap_edit` sets `wrap_stale` on both of its
     `LAYOUT_RESET` returns, and begin clears it.

   Rows below k are not trusted. Inside the layout, the start of row k is also
   proven while busy (the "frontier"). It is the seed of the row being laid
   out, or the kept first row of the edited line.
3. **Readers.**
   - `layout_visual_row` uses its cached rows only when all n are current.
     Before, it checked only `!busy` and the source length, so it answered
     from rows left by `LAYOUT_RESET`.
   - `layout_wrap_follow_edit` takes its certified count from the accessor,
     plus the frontier. With the frontier, typing on the edited line stops at
     that line's first row without a query above it.
   - `view.c finish_move` follows from fresh rows when `edited` or when
     `current < n`. It used `edited || busy`, so the idle, stale state after
     `LAYOUT_RESET` still compared against the stale bottom row.
   - `view.c relative_wrap_place` (a lazy file whose line count is not exact)
     had the edit-cxho.25 stale comparison. It now uses the same follow when
     `edited` or when `current < n`, through `follow_rows` (a shared helper
     that fresh_follow now wraps). If the caret is beyond layout's resident
     budget (`LAYOUT_BYTE_BUDGET`) from the top's line start, then
     `layout_wrap_follow_edit_anchored` reads rows on the caret's line through
     the anchored query at its proven start. It reads a row above that line
     only from the resident seed, never through a piece line query (edit-wv7).
     If no proof exists it returns `LAYOUT_ERR_STATE`, and the caret's row
     becomes the top: the caret stays visible.

## Readers left as they are (audit)

- `editor/paint.c` byte_visible, paint_prepare and minimap lines, the
  `editor.c` submit seed, `input.c` pointer_byte, and `clip.c` middle_target
  all run only while the layout is idle and `!full_pending`. Paint and submit
  are gated in the frame loop. Pointer events are held by queue_pick while the
  layout is busy, `full_pending` or `!paint_ready`.
- `editor.c` refresh_cursor (`cursor_row`) reads `row_byte` while busy, only
  to widen the rows to repaint. It reads only rows already laid out, then
  stops at a void row. Rows from the one being laid out onward are repainted
  anyway.
- `findpanel.c` visible_byte runs on find keys, which can arrive while busy.
  It reads `row_byte` and hit cells. With void rows during a restart it
  answers "not visible", which takes the navigation path. That is
  conservative.
- `scrollctl.c` reads at line 254 only unwrapped (`editor_scroll_follow`
  returns when wrapped), and at line 386 only when idle.
- `wrap.c` scan_row takes the continuation indent from the first cached row
  with the same `line_start`. A void row has indent 0, the same as no match.
  The only consumer is a seeded deep query that is already marked
  approximate.
- `wrap.c` on_top_line checks `!busy` and `first_line_valid`, but not
  `wrap_stale`. Its logical anchor is still valid after a continuation
  `LAYOUT_RESET`, so a wrong "on the top's line" answer still scans forward
  from a certified line start. No red case was found, so it is unchanged.
- `src/scroll` and `src/minimap` read no wrap rows.

## Not changed

- The planning count (k = `wrap_first`, not `l->row`) has no red case. The
  `c26-follow` property test (follow decision against a fresh layout twice as
  tall) gave 0 wrong out of 100000 both with the old count and with the new
  one, including 1502 decisions taken partway through planning. The accessor
  reports `wrap_first` because only those rows are proven current. Reporting
  fewer certified rows means at most one more fresh query per row.
- Unwrapped `row_byte` after `LAYOUT_RESET` is outside this bead (wrap rows).
