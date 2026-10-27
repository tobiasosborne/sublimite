# edit-cxho.25: wrapped follow after an edit decides from fresh rows

Bead edit-cxho.25 (trial, Tobias 2026-10-27: "the view still occasionally
jumps while typing so the current line, usually the last line, lands at the
top of the screen"). Worker session s24. Test: `tests/editor_test.c`
`cxho25_typing_jump` (`--cxho25-typing`): seven named cases plus a seeded
random walk. Worker report: `docs/worker-reports/edit-cxho.25-s24.md`.

## Mechanism (reproduced red, all wrapped)

edit-6090 fixed only one shape. Both wrapped follow sites still compared the
caret with the **pre-edit** cached rows (`layout_wrap_edit` leaves the
edited line's rows stale until relayout; a layout restarted while busy
zeroes them):

1. `view.c finish_move`, edited: `cursor >= wrap_rows[last].next`. The
   stale `next` of the bottom row is the old line end + 1, so a **single
   letter typed at the end of the bottom row's line** already reaches it;
   the edit-6090 one-row proof fails and the caret's row became the top.
2. `input.c finish_edit`: `cursor.start > wrap_rows[last].start` with the
   same proof. A **Return at the start of a soft-wrapped row** on the bottom
   row leaves the caret on the bottom row, but the row above the caret is not
   the old bottom row, so the caret's row became the top.
3. **Several keys before one frame** (key repeat; a fast Return-Return
   paragraph break; fast typing while the frame is late): the second key
   finds the layout busy. `layout_wrap_edit` restarts it and the cached rows
   read 0, so `cursor >= 0` held everywhere, including the non-edit path
   (BackSpace's LEFT motion), and the caret's row became the top, also with
   the caret mid-screen at the end of the file.
4. A **paste** of a few lines at the bottom row (more than one row below):
   the one-row proof fails, far-jump follow.
5. Non-edit, **Down onto a soft row end** (trailing affinity) below the
   bottom row: the follow used the byte's row, not the displayed row above
   it, so the top landed one row below the caret (caret hidden).

Unwrapped mode never failed in the random run (0 violations), consistent
with edit-6090's and edit-vp4m's results.

## Decision

One helper in the layout module, `layout_wrap_follow_edit`, used by both
sites: starting from the caret's fresh row, walk fresh rows upward until the
top (caret on screen: keep the top) or a **certified** cached row: rows
`[0, l->row)` when the layout is busy after `layout_wrap_edit` (it restarts
work at the edited line's first row; rows before an edited line cannot
change), every row when idle, none when the layout is stale
(`total`/`src`/`first_line_valid` mismatch) or the top is not a certified
row start. The distance d decides: d < rows keep; d up to rows-1+max_scroll
scroll minimally (top = the row rows-1 above the caret, caret on the bottom
row); farther, the caret's row is the top (the old far-jump policy).
max_scroll is rows-1 for edits (less than a screen below: Return bursts,
pastes) and 1 for a non-edit motion over a busy layout (the cached path's
one-row rule). finish_move uses the fresh path for every edit and for any
motion while the layout is busy; both paths honour trailing affinity.

Rejected: patching each stale comparison (letter, Return, burst, paste each
need their own proof, which is how edit-6090 left four shapes open);
deferring the follow to after relayout (a frame with the caret hidden, and a
larger change to the frame loop).

## Cost (P)

Typing inside a line: the walk stops at the certified row above the edited
line's first row, i.e. one fresh query per row of the caret's line above
the caret plus one, at each of the two sites. Worst case (far edit, stale
layout): rows-1+max_scroll fresh queries, each bounded by
LAYOUT_BYTE_BUDGET. No allocation (malloc guards in the new test and the
existing suites stay at 0). G1 A/B is in the worker report.
