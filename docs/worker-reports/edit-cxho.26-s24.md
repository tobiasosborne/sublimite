# edit-cxho.26 (s24): cached wrap rows after a restart, LAYOUT_RESET and relative placement

Worker: Claude Opus 5.5, worktree `.wt/edit-cxho.26-s24`, display :95. Decision note:
`docs/decisions/edit-cxho.26.md`, which also has the full audit of every reader.

## Result

- **(1) Fixed at the source.** A layout restarted while busy now marks rows 1..n-1 empty
  (`start/end/next = LAYOUT_VOID_ROW`, `row_byte = LAYOUT_VOID_ROW`). Before, it zeroed them, so they
  read as real rows at byte 0.
- **One accessor for every reader.** `layout_wrap_current_rows(l, t)` says how many leading rows
  are current:
  - all rows when idle;
  - the row being laid out while busy;
  - the edited line's first row while planning;
  - 0 for another source or length, or after `LAYOUT_RESET`. A new `wrap_stale` flag is set on both
    `LAYOUT_RESET` returns and cleared by begin.
- **(2) Audit.** I changed three readers, each with its own red test:
  - `layout_visual_row`'s cached path answered from the pre-edit rows that `LAYOUT_RESET` leaves.
  - `view.c finish_move` followed from a stale bottom row in that idle state.
  - `layout_wrap_follow_edit` now gets its certified count from the accessor, plus a "frontier" row
    whose start is proven while the layout is busy.

  Every other reader is gated to an idle layout with `!full_pending`, or is conservative with void rows
  (decision note, "Readers left as they are"). `src/scroll` and `src/minimap` read no wrap rows.
- **(3) `relative_wrap_place` had the stale comparison, and it can trigger.** On a lazy file whose
  line count is not exact, typing repairs through `view_relative_restore` with `edited = true`, so it
  goes through this path while the line index builds. A letter typed at the end of the bottom row's
  line made the caret's line the top. That was red for both the resident query and the anchored one
  (over `LAYOUT_BYTE_BUDGET`). The fix uses the same fresh follow as finish_move (`follow_rows`). When
  the caret is not resident, `layout_wrap_follow_edit_anchored` reads rows on the caret's line through
  the anchored query. It reads rows above that line only from the resident seed, never through a piece
  line query. If no proof exists, the caret's row becomes the top, so the caret stays visible.
- `cxho25_typing_jump` and the 6090 test stay green.

## Red run (before the fix; same tests)

```
Not charging
 11:34:33 up 1 day, 13:37,  1 user,  load average: 2,40, 1,28, 1,44
$ build/tests/wrap_test c26-restart
edit-cxho.26 restart: row 1 start=0 next=0 row_byte=8, want empty
FAIL wrap_test:500 w->start==LAYOUT_VOID_ROW && w->end==LAYOUT_VOID_ROW && w->next==LAYOUT_VOID_ROW && f.rb[r]==LAYOUT_VOID_ROW
exit 1
$ build/tests/wrap_test c26-reset
edit-cxho.26 reset: byte 30 cached row 30..41, fresh 30..36
edit-cxho.26 reset: byte 31 cached row 30..41, fresh 30..36
... (bytes 30..42 answered from pre-edit rows)
exit 1
$ build/tests/wrap_test c26-follow
edit-cxho.26: 1200 follow decisions checked against a fresh layout, 0 wrong (idle 170, planning at its first row 144, planning partway 11, painting 275; caret below the screen 376)
exit 0
$ build/tests/view_test c26-resident
edit-cxho.26 relative letter (resident): top 8 -> 16, caret 20 on the bottom row
tests/view_test.c:938: assertion failed: f.v.state.visual_byte==top_byte
exit 134
$ build/tests/view_test c26-anchored
edit-cxho.26 relative letter (anchored): top 69990 -> 70004, caret 70008 on the bottom row
tests/view_test.c:938: assertion failed: f.v.state.visual_byte==top_byte
exit 134
$ build/tests/view_test c26-reset
edit-cxho.26 reset motion: caret 30 (row 30) hidden below top 0
tests/view_test.c:961: assertion failed: cursor==30 && f.v.state.visual_byte+20u>=row_start && f.v.state.visual_byte<=row_start
exit 134
```

`c26-follow` is a property guard for the follow_edit change, not a red. It compares follow_edit's
decision with a fresh layout twice as tall, in every state a key can find the layout in. It was green
before the fix: the planning count I suspected gave 0 wrong out of 100000 both with the old count and
with the new one.

Each partial fix is load-bearing. I checked by reverting one piece at a time with the rest in place:
- With the old `fresh = edited || busy` in finish_move, `c26-reset` is still red ("caret 30 hidden below
  top 0").
- Without the frontier, `c26-anchored` is still red ("top 69990 -> 70004"); `c26-resident` passes.

## Green run

```
Not charging
 11:41:35 up 1 day, 13:44,  1 user,  load average: 1,36, 1,64, 1,60
$ build/tests/wrap_test c26-restart       -> edit-cxho.26: a restarted layout's rows read empty, not row 0   exit 0
$ build/tests/wrap_test c26-reset         -> edit-cxho.26: rows left by LAYOUT_RESET are not answered from   exit 0
$ build/tests/wrap_test c26-follow        -> 1200 follow decisions checked against a fresh layout, 0 wrong   exit 0
$ build/tests/view_test c26-resident      exit 0
$ build/tests/view_test c26-anchored      exit 0
$ build/tests/view_test c26-reset         exit 0
$ C26_SEEDS=50000 build/tests/wrap_test c26-follow
edit-cxho.26: 100000 follow decisions checked against a fresh layout, 0 wrong (idle 14688, planning at its first row 11697, planning partway 1502, painting 22113; caret below the screen 31328)   exit 0
$ build/tests/wrap_test   -> 10000 relayouts mallocs=0 guard=active; 10000 typing edits mallocs=0 guard=active; all passed   exit 0
```

## Verification (box: Charging / Not charging, load ~0.6-3.6)

- `make -j4 all` (gcc, -Werror): exit 0.
- ASan with `ASAN_OPTIONS=detect_leaks=1`:
  - `build/san/tests/layout_test`: exit 0 (all passed; layout no-malloc 0 allocations over 10000
    relayouts).
  - `build/san/tests/wrap_test`: exit 0 (includes the three c26 cases).
  - `build/san/tests/view_test`: exit 0 (`c26_bottom_row` is in the default run).
  - `build/san/tests/editor_test` (full default run, `DISPLAY=:95` via `tools/with_display_lock.sh`):
    exit 0. It includes `edit-cxho.25: 15012 keys checked, 0 violations` and
    `edit-6090: mid-file typing keeps the caret's line on screen and the top steady`.
- Release run with the malloc guard active (:95):
  - `editor_test --cxho25-typing`: exit 0 (`15012 keys checked, 0 violations`; the test asserts 0
    allocations).
  - `editor_test --6090-typing`: exit 0.
- Full `DISPLAY=:95 make -j4 check`: **exit 0, `check: 110 test binaries passed`**. Also passed:
  - `test_replay_cli: all passed`.
  - `display lock contract: ... PASS`. The `make[1]: *** ... Error 1` and the "lock timeout" lines in
    the log come from inside that contract, which holds :99 on purpose and checks the timeout.
- Malloc on the typing path: none added. The new code reads arena rows and queries on the stack, as
  `layout_wrap_follow_edit` already did. The wrap_test guard shows `typing edits mallocs=0`; under ASan
  the editor_test lines show `null 10000 keys mallocs=0`.

## Files touched

- `src/layout/layout.h`:
  - `wrap_stale` field;
  - declarations of `layout_wrap_current_rows` and `layout_wrap_follow_edit_anchored`.
- `src/layout/wrap.c`:
  - void rows on begin;
  - `wrap_stale` on `LAYOUT_RESET`;
  - `current_rows` and the accessor;
  - `layout_visual_row`'s cached-path gate;
  - `row_above` and `layout_wrap_follow_edit_anchored` (with the frontier). `layout_wrap_follow_edit`
    now wraps it.
- `src/view/view.c`:
  - `follow_rows`, a shared helper that `fresh_follow` wraps;
  - `relative_wrap_place` follows fresh rows when edited or when rows are not current;
  - `finish_move` treats `current < n` as fresh.
- `tests/wrap_test.c`: `c26_restart_rows`, `c26_reset_rows`, `c26_follow_truth` (`C26_SEEDS` sets the
  soak length); selectors `c26-restart`, `c26-reset`, `c26-follow`; `main` takes argv.
- `tests/view_test.c`: `c26_place`, `c26_relative_letter` (resident and anchored), `c26_reset_motion`;
  selectors `c26-resident`, `c26-anchored`, `c26-reset`; `c26_bottom_row` added to the default run.
- `docs/decisions/edit-cxho.26.md`, and this report.

## What remains

- When the caret is not resident and the walk needs a row above the caret's line, the placement is
  unproven, so the caret's row becomes the top. Two cases reach this:
  - two keys before one frame, which restarts the layout so no rows are certified;
  - a top more than `LAYOUT_BYTE_BUDGET` (65536 B, (P)) from its line's start on a lazy, unindexed file.

  Proving it needs a backward LF walk to the previous line's start, which does not exist today.
- `on_top_line` and scan_row's indent lookup were left as they are, with reasons in the decision note
  (no red case; both are benign).
- I did not run G1: the change adds no work to ordinary typing. The walk stops at the edited line's
  first row (the frontier) rather than one row above it, so the cost is one fewer fresh query or the
  same.

## Found outside scope (reported, not fixed)

1. Unwrapped `layout_edit` returns `LAYOUT_RESET` the same way and leaves `row_byte` unshifted while
   the layout is idle until the host's next turn. I did not audit unwrapped readers of `row_byte` in
   that window. The bead is about wrap rows.
2. `findpanel.c visible_byte` can run on find keys while the layout is busy. It reads `row_byte` and
   the hit cells of rows not yet laid out again. With this change those rows read void during a
   restart, which is conservative. During a non-restart busy layout, the hit cells of rows queued for
   relayout are still the old ones.
