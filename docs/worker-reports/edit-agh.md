# edit-agh worker report

Branch wt/edit-agh (from ae042e8). Box: power `Not charging` [AC], Xvfb :99,
private XDG_RUNTIME_DIR under the session scratchpad, clang ASan/UBSan
binaries (`build/san/tests/*`), run through `tools/with_display_lock.sh`,
serially.

## Root cause

edit-3kk (ae042e8) made scrollctl the owner of unwrapped viewport labels.
Three effects broke `tests/editor_large_test.c` `gutter_estimates`, and the
third also broke the main section:

1. **Starved follow (line 117/341/969).** `editor_large_goto_byte`
   (src/editor/large.c:464, was `return editor_refresh_cursor(e, old)`)
   discards its own certified start: `editor_refresh_cursor` ->
   `editor_scroll_follow` (src/editor/scrollctl.c) marks `scroll_pending` and
   `scroll_following`, and neither shortcut applies (the layout rows are stale;
   `lg.lines_exact` is false). The follow then needs a resident window, and
   `src/scroll/resident.c:119` submits that on `WORK_BULK`. The test holds the
   bulk lane with the index build behind it (an unindexed 1 GiB file), so
   `scroll_pending` stays set, `editor_get_stats().pending` stays true, and
   `settled()` never holds. Instrumented: `pending=1 following=1 respend=1
   top_est=1 lines_exact=0`.
2. **Early relabel (line 358, found after fixing 1).** `editor_scroll_slice`
   re-resolves on every `lineidx_built_prefix` change. When the prefix reached
   the jumped byte (prefix=8215) it relabelled the top exactly and submitted
   a frame while `lines_exact` was still false. That is a dim-gutter frame
   between the last estimated frame and the exact publication: est_frame=7,
   first_exact=9. Before edit-3kk, large.c `index_progress` relabelled only
   on index completion.
3. **Approximate label clamped as a position (line 1017, found after fixing
   2).** At index completion `scroll_set_extent` -> `clamp()`
   (src/scroll/scroll.c:58) treats the density-estimated label (23107, an
   overestimate by design) as a line, clamps it to the exact `max_top` (21472)
   and sets `SCROLL_LINE`. The view then seeks to EOF instead of relabelling
   byte 1160000 as line 21000.

## Fix (src/editor only)

- scrollctl.c:92 `editor_scroll_place`: adopts a seek-certified (byte, line,
  approximate) pair as `position_buffer` does, but only if the pure
  `scroll_seek_line` keeps the same top row. Otherwise it returns false and
  the old follow path runs. large.c:464 uses it, plus the new
  `editor_refresh_placed` (editor.c:215: clip and refresh without re-follow).
- scrollctl.c:196 `labels_held = lg.lazy && !lg.lines_exact`: prefix growth
  alone does not re-resolve. The flip of `count.exact` does.
- scrollctl.c:235: when a READY state is approximate, an extent change only
  stores the extent, with no clamp. The resolver relabels the byte anchor.

No deadline or test was changed. No allocation was added: the new code is
stack/state assignments only. Decision: docs/decisions/edit-agh.md.

## Red run (unchanged test, ae042e8)

`editor_large_test` exit=1, load1 14.84 at start (other agent's run):
```
editor_large_test:117: FAIL trace_now_ns() < deadline
editor_large_test:341: FAIL step_until(e, settled, UINT64_C(5000000000)) == 0
editor_large_test:969: FAIL gutter_estimates() == 0
```
Intermediate reds (instrumented, debug prints since removed): after fix 1,
line 358 `h.first_exact_frame == estimated_frame + 1u` (7 vs 9). After fix 2,
line 1017 `editor_view(e).first_line == A_LINES + 1000u` (21472).

## Green run (serial, 20:32, load1 1.3-4.2 at each start, [AC] Not charging)

| suite | exit |
|---|---|
| editor_large_test | 0 (`editor_large_test: ok`) |
| scroll_test | 0 |
| scroll_display_test | 0 |
| editor_scroll_test | 0 |
| editor_test | 0 |
| editor_p4_test | 0 |
| cli_test | 0 (2 FAIL lines from its own forced-timeout child, expected) |

Release gcc compile of the changed objects (`build/rel/src/editor/{scrollctl,editor,large}.o`) is clean.

## Out of scope (reported, not fixed)

- Fix 3 is a workaround in scrollctl for a scroll-module policy:
  `scroll_set_extent` clamps `first_line` even when `approximate` is set.
  Other callers that hold an approximate label across an extent change
  (`editor_scroll_edit` -> `scroll_set_extent` on a lazy file) can still jump
  to a clamped guess. scroll.c should probably skip `clamp()` for an
  approximate READY state.
- Wrapped mode was not touched. goto_byte there still uses
  `editor_refresh_cursor`, and its labels still go through large.c.
- Other unwrapped follows (Ctrl+End, PageUp/Down outside the resident rows)
  still need a bulk-lane window. That lane can be busy with a large file's
  index/warm/find jobs, so they wait behind them. The near-end tests pass, but
  `src/scroll/resident.c:119` has no foreground lane. A follow-up should decide
  whether resident windows over already-mapped pages may use WORK_FOREGROUND.
