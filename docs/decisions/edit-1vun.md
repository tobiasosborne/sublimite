# edit-1vun: layout's row queries near the top and unwrapped Select All never count lines from byte 0

## Root cause

On an unindexed lazy file the piece tree's newline counts for the mapping are
lazy; `piece_byte_to_line`/`piece_line_to_byte` resolve them from byte 0 on
the UI thread the first time they are asked (once; later calls are cheap).
Three paths still asked:

1. `layout_visual_row(direction -1)` at the viewport top (`byte ==
   first_byte`) dropped its resident anchor, and every query above the top
   (`byte < first_byte`, the further rows of a PageUp) had none, so
   `scan_row` called `piece_byte_to_line`. Red (release, wv7_case PageUp):
   3 layout piece line queries, 216.7 ms settle (M) [Charging, load1 3.80];
   the wv7 worker measured 253 ms release, 311-1135 ms ASan.
2. `editor_full_layout` asked `layout_visual_row(visual_byte)` when the top
   row is a continuation; more than `LAYOUT_BYTE_BUDGET` past the line start
   no resident seed applies. Red (new continuation-top fixture, Ctrl+End): 1
   query, 220.3 ms (M) [Charging, load1 3.80]. PageUp from there walks rows
   above the visual top on the same long line: same fallback.
3. Unwrapped Select All ran `view_command`, whose `follow_line` asks
   `line_at(EOF)`. Red (ptn PTN_KEYS fixture): line_scanned 1,073,741,825 B
   (the whole file plus the typed byte), 712.7 ms (M) [Charging, load1 3.80].

## Decision

1. **Anchored at or above the top** (`above_top`, src/layout/wrap.c). When no
   resident seed applies and `byte <= first_byte`, one backward pass from
   `first_byte` counts the LFs in `[byte, first_byte)` and stops at the LF
   before `byte`: the line start is certified and its ordinal is the top's
   label minus that count (exact when the label is exact; an estimated label
   stays an estimate, as with `resident_seed`). Upward from a line start the
   row above ends at the previous line's LF (the query becomes direction 0 at
   `byte - 1`, as `scan_row`'s piece branch did). `scan_row` then runs with
   that logical seed (as `layout_visual_row_anchored`).
2. **On the top's logical line** (`on_top_line`). Past `first_byte` beyond the
   resident budget, but not past laid-out row 0 whose `line_start` is
   `first_byte` (idle layout only): seed with `{first_byte, first_line}`.
3. **editor_full_layout continuation**: beyond `LAYOUT_BYTE_BUDGET` past
   `first_byte` it calls `layout_visual_row_anchored(visual_byte, first_byte,
   lay.first_line)`; `visual_byte` lies on the line at `first_byte` (the
   view's wrapped invariant, also what `visual_seed` checks). Within the
   budget the resident seed path is unchanged (anchored seeding differs there:
   it deep-seeds past half the budget).
4. **Unwrapped Select All routes relative**: `view_relative_command` accepts
   `VIEW_SELECT_ALL` unwrapped (phase `P_SETUP`; `finish_move` then follows
   byte-relatively, walking back from EOF to the last LF). input.c routes it
   whenever `relative_repair` holds. Wrapped routing is unchanged (wv7).

Results are identical to the piece path when labels are exact:
`tests/wrap_test.c` `above_top_queries` compares every query at or above a
top (upward from row starts, as view asks; 0/+1 everywhere) with a twin laid
out from byte 0, and asserts zero piece line queries. With the new paths
disabled the comparisons still hold and only the probe assertion fails.

## Bound

- Walk: at most `LAYOUT_BYTE_BUDGET` = 65,536 B (P, layout.h) below the top
  per query. Derivation: it is the distance within which `resident_seed`
  already trusts the top and the decode bound of the `scan_row` that follows;
  a byte compare costs less than a cluster decode, so a query's byte work
  stays within 2 x LAYOUT_BYTE_BUDGET. Layout cannot include view.h, so the
  brief's VIEW_SCAN_BOUND is not used here; view's own walk (seek_scanned)
  stays under VIEW_SCAN_BOUND in wv7_case (256 B for PageUp).
- No allocation: one 4 KiB stack window (`LAYOUT_CHECKPOINT_STRIDE`, as
  scan_row's existing windows).
- Not covered: a line start more than 65,536 B above the top (a previous line
  longer than the budget) keeps the piece query; on an unindexed lazy file
  that is still the one-time lazy count. `above_top_queries` pins this.

## Probe

`layout.line_query_probe` (optional `uint64_t *`, NULL after `layout_init`)
counts the piece line queries `scan_row` makes. editor_large_test asserts 0
per key in wv7_case and the new continuation-top case (`vun_case`,
`EDIT_1VUN_REPRO=1`) and checks the probe was not detached by a re-init.
