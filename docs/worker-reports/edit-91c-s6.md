# edit-91c (s6): large-file follows outside laid-out rows vs the bulk lane

Worktree `.wt/edit-91c-s6` (branch wt/edit-91c-s6 at main faa7baa). No bd/git run.
Budget cut by the coordinator mid-bead (25 tool calls left): full `make check`
was NOT run; own suites were run once (see below).

## Files touched
- `src/scroll/resident.c`: `window_class()` lane probe (mincore, <= 16 spans), foreground submit with bulk fallback.
- `src/scroll/scroll.h`: resident bridge comment (lane).
- `src/editor/input.c`: `large_doc_end()` and its dispatch after `scroll_key_motion` (unwrapped, lazy, `!lines_exact`, VIEW_DOC_END).
- `tests/scroll_test.c`: `resident_lane_choice` (+ `P4_RESIDENT_LANE` env gate).
- `tests/editor_large_test.c`: `starved_follows(hold, after_index)` x3 in main (+ `EDIT_91C_STARVED`, `EDIT_91C_NOHOLD`, `EDIT_91C_AFTER_INDEX`, `EDIT_91C_TRACE` env gates).
- `docs/decisions/edit-91c.md`, `src/scroll/STATUS.md`, this report.

## Decision (docs/decisions/edit-91c.md)
1. In-core windows take WORK_FOREGROUND; bound relied on: 64 KiB copy at 6.3 GB/s (M)[bat] = ~10 us (E) + 16 minor faults x 1 us (E) = 16 us (E), inside the lane's 5 ms CPU poll bound (G); file-backed pages not in core, fragmented windows, probe failure, no lane or exhausted reserve keep WORK_BULK. No new lane; `work_prioritize` not used (same no-I/O rule).
2. Ctrl+End on a lazy file without an exact line count bypasses view's exact EOF line query (the actual 1.4 s wait: a UI-thread file scan seeded only when the index completes) and places the cursor as the byte jump does; scrollctl resolves and later relabels.

## Red runs (verbatim)
scroll_test (before resident.c change), bulk lane held by a blocking job, in-core source:
```
scroll_test: edit-91c in-core window while bulk held: rc=1 wait_ns=5000013206 (M)
scroll_test:1034: FAIL rc == SCROLL_OK
```
editor_large_test, log_1g.txt, bulk lane held from the first frame, Ctrl+End:
```
edit-91c stalled: hold=1 action=0 scroll_pending=1 resident_pending=0 windows=0/0 view_busy=1 phase=11 query_pos=283353088 query_target=1073741824 index_complete=0 (M)[AC]
editor_large_test:1061: FAIL editor_get_stats(e).submitted_sequence >= sequence
```
(phase 11 = view P_FINISH; the resident bridge had no window requested: the wait is view's exact line query.)
No-hold trace before the fix (real index build, 100 ms samples):
```
edit-91c t=100 ms ... windows=0/0 view_busy=1 phase=11 query_pos=5712896 index_complete=0 prefix=837
edit-91c t=1305 ms ... windows=0/0 view_busy=1 phase=11 query_pos=61619200 index_complete=0 prefix=15808
edit-91c hold=0 ctrl_end_submit_ns=1387489787 worst_page_submit_ns=11238417 (M)[AC] ... index_complete=0->1
```
(an earlier identical run: ctrl_end_submit_ns=774241454).

## Green runs (verbatim)
```
scroll_test: edit-91c in-core window while bulk held: rc=0 wait_ns=3819937 (M)
scroll_test: edit-91c evicted window kept the bulk lane PASS
scroll_test: edit-91c resident lane choice PASS
scroll_test: PASS                                   (full suite, exit 0)
edit-91c hold=1 after_index=0 ctrl_end_submit_ns=2093952 worst_page_submit_ns=0 (M)[AC]=submit-t0 budget_ns=6000000/9000000 (G)=G5 warm A p50/p99, 64 KiB read + full frame, PB 1.025 ms (E) index_complete=0->0 pages=79 gate=TRACK
edit-91c bulk-held during build follows outside laid-out rows: ok
edit-91c hold=1 after_index=1 ctrl_end_submit_ns=608721 worst_page_submit_ns=11699674 (M)[AC] ... index_complete=1->1 pages=79 gate=TRACK
edit-91c bulk-held after index follows outside laid-out rows: ok
edit-91c hold=0 after_index=0 ctrl_end_submit_ns=5844186 worst_page_submit_ns=1627438236 (M)[AC] ... index_complete=0->1 pages=79 gate=TRACK
edit-91c index-building follows outside laid-out rows: ok
```
(first green pass of the three variants: 2 745 444 / 963 581 / 7 903 686 ns respectively.)

## Measurements, stamps
All ASan/UBSan, Xvfb :99 under tools/with_display_lock.sh, BAT0 "Not charging" (tagged [AC] by the fixture), load1 16-24 (sibling Codex workers). Budget derivation: G5 warm A = 64 KiB read + full frame, PB 1.025 ms (E), 6 / 9 ms (G) p50/p99 (perf/01-perf-target.md section 0.2); tracked, not enforced (law 1). Ctrl+End during the build: 774 / 1 387 ms -> 7.9 / 5.8 ms (M). Bulk held for the whole build: never -> 2.7 / 2.1 ms (M). Index complete + bulk held: 0.96 / 0.61 ms (M); 158 page motions crossing the EOF window boundary, worst 6.7 / 11.7 ms (M). `make all` (gcc 13, -Werror) exit 0.

## Suite results
- `build/san/tests/scroll_test`: PASS (exit 0).
- `build/san/tests/editor_large_test`: all edit-91c and edit-2ty cases ok; the suite exits 1 at `mapped_startup_ipc` (`editor_large_test:233`) because `/tmp/edit-corpus/sparse_10g.bin` is absent from the corpus regenerated at 23:26 (directory listing: no sparse_10g.bin). Environmental, not touched by this bead; expect the same on main.
- Full `make check`: not run (coordinator budget cut).

## Quota readings
1. start: 5-hour 12.0 %, 7-day weekly 79.0 %, 7-day Fable 69.0 %.
2. mid: 5-hour 17.0 %, weekly 81.0 %, Fable 72.0 %.
3. before final suites: 5-hour 19.0 %, weekly 81.0 %, Fable 73.0 %.
4. final (after report): 5-hour 19.0 %, weekly 81.0 %, Fable 73.0 %.

## Remains / out of scope noticed, not fixed
- PageUp/PageDown during the index build with the cursor outside the built prefix still wait on view's exact `line_at(cursor)` (1.08 / 1.63 s (M) first PageUp after Ctrl+End in the no-hold runs). Needs a relative-motion path with estimated ordinals or layout-row-carried cursors; conflicts with view's "never approximate counts" cache invariant. Separate bead.
- `editor_jump_line` prioritizes a copy-mode index build onto the foreground lane; a foreground window would then queue behind that build on the same lane (copy mode only, IPC/test path).
- `large_doc_end` sets hscroll 0 like the byte jump; a last line longer than the viewport leaves the caret off-screen horizontally (same limitation as the byte jump).
- Corpus fixture `sparse_10g.bin` missing after the 23:26 regeneration.
- The cx8 bench active_index row stall (5 s settle, view_busy=1) is the same view-exact-query family as the PageUp remainder.
