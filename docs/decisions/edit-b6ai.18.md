# edit-b6ai.18: wrapped caret jumps scroll minimally (Ctrl+End fills the viewport)

Bead edit-b6ai.18 (terra drive 2026-11-03, "Large-file Ctrl+End discards
visible context", `build/terra/shots/big-end.png`). Worker session s29.
Tests: `tests/editor_test.c` `b6ai18_end` (`--b6ai18-end`),
`tests/editor_large_test.c` `b18_case` (`EDIT_B6AI18_REPRO=1`), and an extra
`LAYOUT_SCROLL_ANY` decision in `tests/wrap_test.c` `c26-follow`. Worker
report: `docs/worker-reports/edit-b6ai.18-s29.md`.

## Cause

In wrapped mode a caret motion that lands more than one row below the
cached bottom row was "followed directly": the caret's row became the top
(`view.c finish_move`, and the same branch in `relative_wrap_place` for a
lazy file whose line count is not exact). Ctrl+End therefore showed the last
row alone at the top of a blank editor. Unwrapped mode goes through the
scroll module, whose top is clamped to `lines - rows`, so it showed the last
page. Wrapped go-to-line placed the target line at the top unconditionally,
so Ctrl+G to the last line showed the same picture.

## Rule

- **A caret jump that must reveal a caret below the screen scrolls
  minimally**: the caret's row is the bottom row, the rows above it fill the
  viewport (Ctrl+End, Down, word/selection keys, any distance). This is
  unwrapped mode's and Sublime's picture.
- **A last line taller than the viewport**: the same rule; the caret's
  (last) row is the bottom row and the rows above it are that line's
  continuation rows (the top row is a continuation).
- **Go-to line** (Ctrl+G): the target stays the top row, unless fewer rows
  than the viewport run from it to EOF; then EOF's row is the bottom row
  (the last page), as unwrapped scroll clamps. A document shorter than the
  viewport keeps byte 0 at the top.
- **Not changed**: upward jumps (the caret's row is the top, already
  minimal); PageUp/PageDown (their page displacement is the scroll: the
  caret's row stays the top past one row, as before); a repair-follow (empty
  TYPE, a target the host installed); edits (edit-cxho.25: minimal up to a
  screen below, farther the caret's row); wheel and minimap drag (scroll past
  end, docs/decisions/edit-cxho.8.md, settled).

## Mechanism

- `layout_wrap_follow_edit_anchored` takes `max_scroll = LAYOUT_SCROLL_ANY`
  (new, `layout.h`): minimal at any distance. With it the walk stops after
  rows-1 rows above the caret: unmatched there, d >= rows, and the answer is
  that row. Cost (P): at most rows-1 row queries, fewer than the rows-1+
  max_scroll the edit follow already makes.
- The walk's line anchor now moves up with it. At the anchored line's first
  row the row above ends the previous logical line; `line_before` proves its
  start by one backward LF pass of at most LAYOUT_BYTE_BUDGET bytes (as
  edit-1vun's `above_top`) and the ordinal becomes the anchor's minus one (a
  label stays a label). Before, a row above the caret's line outside the
  resident top was `LAYOUT_ERR_STATE`, so on a lazy file the walk could
  never leave the last line. Every row query stays anchored: no piece line
  query (the edit-1vun probe reads 0 on the 50 MB log with the index held,
  and on the 1 GiB wv7/1vun fixtures). A line longer than the budget above
  the caret's line is still unproven; then the caret's row is the top (it
  stays visible), as before.
- `view.c`: `follow_scroll` names the rule per key; `far_follow` replaces the
  two "follow directly" branches, anchored at the caret row's certified line
  (`c.line_start`, `c.line` in the exact path; the proven `start` and its
  label in the relative path). The busy-layout path (`follow_rows`) uses the
  same rule for motions.
- `layout_wrap_clamp_end` (new): from a go-to target's first row, forward up
  to rows-1 anchored row queries; if EOF comes first, up rows-1 rows from
  EOF's row (same anchored walk). `input.c goto_continue` calls it only
  when the count is not exact or fewer than `rows` lines follow the target
  ((P) a logical line takes at least one row, so otherwise nothing clamps
  and no row is queried).

No allocation: the backward pass uses a LAYOUT_CHECKPOINT_STRIDE stack
buffer as `above_top` does; the editor tests' counting allocator reads 0.

## Rejected

- Laying the viewport out bottom-up from EOF (a "bottom anchor" mode in
  layout): a second layout direction for one key; the walk already exists
  and is bounded by the row count.
- Changing far edits (a large paste, undo far away) to the same rule:
  outside the bead; edit-cxho.25's tests hold that policy. Reported.
