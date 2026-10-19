# edit-3620 (s9) landing report

Stamp: BAT0 status `Full`; `uptime` at start 04:51:32, load 11.69/9.30/9.70; disk `/` 480G, 400G used, 56G free (88 %).

## Rebase
- Worker commit: `628c4f1` "edit-3620: Keep keeps its prompt with a notice when the save target cannot be examined, ..." (src/editor/buffers.c, tests/editor_session_test.c, docs/decisions/edit-3620.md, docs/worker-reports/edit-3620-s9.md).
- `build/briefs/land9.sh 3620 ...`: rebased `628c4f1` onto main `5c1e112` (main had not moved since the tree branched after 6yom; the rebase was a no-op). No conflicts, no resolutions.
- The script's `touch src/*/*.c src/*/*.h tests/*.c` + `make -j3 all` (gcc 13, -Werror): rc=0.

## Commands (worktree .wt/edit-3620-s9, display :97, leaks on, outside the sandbox)
| command | rc | uptime |
|---|---|---|
| land9.sh (commit, rebase main, touch, make -j3 all) | 0 | before 04:51 |
| make -j3 build/san/tests/{editor_session_test,dataloss_test,savectl_test,editor_test} | 0 | 04:51:32 load 11.69 |
| build/san/tests/editor_session_test (full; `edit-3620: Keep with unreadable target stat=1 stale=yes notice=Keep failed: R reload / K keep`) | 0 | 04:54:55 load 13.31/12.40/10.92 |
| build/san/tests/dataloss_test | 0 | 04:55:34 load 13.90/12.63/11.06 |
| build/san/tests/savectl_test | 0 | 04:55:38 load 13.99/12.67/11.08 |
| build/san/tests/editor_test | 0 | 04:58:50 load 11.63/12.89/11.52 |
| make -j3 build/tests/editor_test (release) | 0 | |
| EDITOR_ALLOC_ONLY=null build/tests/editor_test: `null 10000 keys mallocs=0 guard=active` | 0 | 04:58:58 load 11.26 |
| EDITOR_ALLOC_ONLY=raster build/tests/editor_test: `raster 10000 keys mallocs=0 guard=active` | 0 | 04:59:03 load 11.08 |

Each sanitizer suite ran as `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:97 EDIT_DISPLAY=:97 EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/<name>`. No reruns needed. `git status` clean after the runs (no fixtures left in the tree).

Tracked number noted by dataloss_test (not a gate): edit-ze59 Save As 100000000 B sparse, input->submit 5.583 ms, G1 p99 2.000 ms (M) [power=Full load1=13.90 build=sanitized].

## Unresolved
Nothing for this bead. The worker's out-of-scope observation (undo history lost after Ctrl+S in a Save-As'd COPY buffer without a journal) stands in docs/worker-reports/edit-3620-s9.md for the coordinator.
