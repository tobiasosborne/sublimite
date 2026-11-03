# edit-b6ai.18 worker report (session s29)

Bead: "Ctrl+End in a 50 MB log shows only the final partial line at the top
of a blank editor". Decision note: `docs/decisions/edit-b6ai.18.md`.

## Outcome

In wrapped mode a caret jump below the screen now scrolls the minimum: the
caret's row is the bottom row and the rows above it fill the viewport. This
is how unwrapped mode and Sublime already behave. Ctrl+End, Ctrl+G to the
last line, and Ctrl+G into the last page all show the last page. That holds
for the 50 MB log while its index is still building, and it makes no piece
line query. When the last logical line is taller than the viewport, the
caret's (last) row is the bottom row and the rows above it are that line's
continuation rows.

Unchanged: PageUp/PageDown, the repair-follow (empty TYPE), the edit follow
(edit-6090, edit-cxho.25 and .26), wheel scrolling and minimap drag (scroll
past end stays settled). All of their tests stay green.

## Files

- `src/layout/wrap.c`:
  - `line_before` (new): a backward LF pass bounded by LAYOUT_BYTE_BUDGET.
  - `row_above`: its line anchor now moves up across logical lines.
  - `layout_wrap_follow_edit_anchored`: accepts `LAYOUT_SCROLL_ANY`.
  - `layout_wrap_clamp_end` (new): the go-to last-page clamp.
- `src/layout/layout.h`: `LAYOUT_SCROLL_ANY` and `layout_wrap_clamp_end`. The anchored-follow comment is updated.
- `src/view/view.c`: `follow_scroll` gives the per-key rule. `far_follow` replaces the two "caret's row becomes the top" branches, in `finish_move` and in `relative_wrap_place`. `follow_rows` uses the per-key rule.
- `src/editor/input.c`: `goto_continue` clamps a wrapped target to the last page. It queries no rows when the exact count shows at least `rows` lines after the target.
- `tests/editor_test.c`: `b6ai18_end` (`--b6ai18-end`, in part 3), with these cases:
  - wrapped and unwrapped Ctrl+End, Ctrl+Home, then Ctrl+End again;
  - Ctrl+G to the last line, to a mid-file line (the line stays on the top row), and into the last page;
  - a long last line (6000 B, about 80 rows);
  - a short document in both modes;
  - the malloc counter must read 0.
- `tests/editor_large_test.c`: `b18_case` (`EDIT_B6AI18_REPRO=1`, in the default run). It copies the 50,000,000-byte prefix of `/tmp/edit-corpus/log_1g.txt` to a .txt so wrap is on by default, holds the bulk lane so the index cannot finish, and presses Ctrl+End. Checks:
  - all text rows are laid out and contiguous;
  - EOF is on the bottom row;
  - each row's line start follows an LF (pread oracle);
  - the edit-1vun probe counts 0 piece line queries.

  After the index publishes it checks the same picture again, now with exact labels.
- `tests/wrap_test.c`: `c26-follow` also checks `LAYOUT_SCROLL_ANY` against a fresh layout, idle and busy.
- `tests/view_test.c`: `wrapped_relative` pinned the old Ctrl+End top (the last line). It now expects the new top, the "mid two" line, with the derivation written next to it. The rest of the test stays: relative and exact agree, and the relative placement makes no line query.

## Red (before the fix)

```
edit-b6ai.18 wrapped Ctrl+End from the top: wrap 1 caret row 0 (want 25) of 26 rows, top 11155 (view 11155) filled 0 caret 11155 len 11155
edit-b6ai.18 wrapped Ctrl+End again: wrap 1 caret row 0 (want 25) of 26 rows, ...
edit-b6ai.18 wrapped Ctrl+G last line: wrap 1 caret row 0 (want 25) of 26 rows, ...
edit-b6ai.18 wrapped Ctrl+G into the last page: caret row 0, last row 5 of 26
edit-b6ai.18 wrapped Ctrl+End onto a last line taller than the viewport: wrap 1 caret row 0 (want 25) of 26 rows, top 7620 ...
edit-b6ai.18 wrapped Ctrl+End onto a long last line again: wrap 1 caret row 0 (want 25) ...
edit-b6ai.18 short document Ctrl+G scrolled: top 235
editor_test:7087: FAIL failed == 0

editor_large_test: edit-b6ai.18 index building row 1 of 42: reason 1 start=18446744073709551615 next=18446744073709551615 line_start=49999920
editor_large_test: edit-b6ai.18 rc=1
```

All unwrapped cases passed in the red run. They are pinned. In the large-file
red run, row 1 is void: only the final partial line (start 49999920) is laid
out, which is the screenshot.

## Green

- `editor_test --b6ai18-end`: `edit-b6ai.18: Ctrl+End and go-to into the last page fill the viewport, wrapped and unwrapped`.
- `editor_large_test` (b18 lines from the make check log):
  - `ctrl+end rows=42 layout_line_queries=0 key_settle=2.003 ms (M) rc=0`
  - `index published after 24.922 ms (M) rc=0`
  - `wrapped Ctrl+End at the end of a 50 MB log fills the viewport`
- Also green when run alone: `EDIT_WV7_REPRO` (worst_probe_bytes=512, bound 4096), `EDIT_1VUN_REPRO` (layout_line_queries=0), and `wrap_test` all.
- `make check` (second full run, on the final tree): `check: 118 test binaries passed in 83.5 s (parallel, 36 jobs, 6 displays)`. The replay, win, install-omarchy and display-lock contracts passed, rc=0.
  - In the first full run, view_test failed in `wrapped_relative`: its old expectation, fixed above. The runner's "FLAKY, passed alone" verdict was wrong, because I rebuilt view_test while that run was going. The second run is the clean one.
- `make all` and `make TARGET=win all`: both rc=0.

## Measurements

[AC, desktop monolith] (no BAT0; load1 about 5 to 7 while make check ran). Sanitizer build:

- Ctrl+End key settle on the 50 MB log with 42 text rows: 1.4 to 2.9 ms (M), over three runs.
- Index publication after the lane is released: about 25 ms (M).

These are tracked prints, not asserts. The walk's cost (P) is at most rows-1 anchored row queries, plus one backward LF pass of at most LAYOUT_BYTE_BUDGET per logical line it crosses. There is no allocation: the pass uses a LAYOUT_CHECKPOINT_STRIDE stack buffer.

## Manual check (private Xvfb :97)

- Setup: the release `build/sublimite`, with throwaway XDG dirs under `build/b18/`, opened on the 50 MB prefix. The window was resized to 3840x2160 and the keys sent with `xdotool key --window`.
- Ctrl+End: the xwd capture shows the viewport filled. Line 416667, the final partial line ending "lat=30896us x", is on the bottom row.
- Ctrl+Home, then Ctrl+G 416660: the caret is on 416660 and the last page is shown, with 416667 at the bottom.
- The instance was closed with kill on its pid. The fixture copy and the xwd files were removed. `/tmp/.X11-unix/X0` was present throughout.

## Found (not fixed)

- Far edits still put the caret's row at the top when the caret lands more than 2 rows-2 rows below the old top (edit-cxho.25's rule): a large paste or an undo far away. It is the same picture as this bead. The fix would be one argument, `LAYOUT_SCROLL_ANY` in `finish_edit` and `follow_rows` for edits, but cxho.25's policy and tests own it.
- Wrapped PageDown from a mid-screen caret makes the caret's row the top. From the top row it advances one row only (the one-row rule). Neither is Sublime's PageDown, which keeps the caret's screen row. Unchanged here.
