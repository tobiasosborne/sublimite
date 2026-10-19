# edit-dxas (s9): landing report

Branch wt/edit-dxas-s9. Worker commit 749044b (tree committed by
build/briefs/land9.sh) rebased onto main 8da8b8b ("Session 19: worklog 05:25
(vp4m, y315 landed)") as 1612d4c. Main not touched; fast-forward is the
coordinator's.

Stamp: BAT0 status Full [AC]; uptime 05:16:15 load 6.18/7.08/9.09 at the
first suite, 05:20:45 load 6.23/6.54/8.33 at the last run.

## Conflicts and resolutions
- tests/editor_session_test.c, default run in `main`: main (edit-3620) added
  `T(kept_target_unreadable() == 0);`, this tree added `T(dxas_all() == 0);`
  at the same place. Kept both, 3620 first.
- In-scope test edit: the 3620 test's final Ctrl+S comment
  "a clean reload may replace b" is stale once dxas lands; reworded to
  "the save's own rename no longer reloads b (edit-dxas)".
- src/editor/buffers.c auto-merged; the dxas deferral sits at the top of the
  `editor_poll_sources` target-watch loop, whose body holds only the watch
  block, so its `continue` skips nothing else.
- Both selector options (`--edit-3620`, `--edit-dxas`) present, no clash.

## Commands
| command | exit |
|---|---|
| build/briefs/land9.sh dxas "<msg>" (commit 749044b; rebase conflicted, auto-aborted) | 3 |
| git rebase main (conflict in tests/editor_session_test.c) | 1 |
| resolve, git add, git rebase --continue -> 1612d4c | 0 |
| touch src/*/*.c src/*/*.h tests/*.c; make -j3 all (gcc, -Werror) | 0 |
| make -j3 build/san/tests/{editor_session_test,dataloss_test,savectl_test,undo_test,editor_test} build/tests/editor_test | 0 |
| build/san/tests/editor_session_test (05:16:15, load 6.18) | 0 |
| build/san/tests/dataloss_test (05:16:53, load 5.19) | 0 |
| build/san/tests/savectl_test (05:16:54, load 5.19) | 0 |
| build/san/tests/undo_test (05:16:57, load 5.10) | 0 |
| build/san/tests/editor_test (05:20:35, load 5.99) | 0 |
| EDITOR_ALLOC_ONLY=null build/tests/editor_test: "null 10000 keys mallocs=0 guard=active" (05:20:40, load 5.99) | 0 |
| EDITOR_ALLOC_ONLY=raster build/tests/editor_test: "raster 10000 keys mallocs=0 guard=active" (05:20:45, load 6.23) | 0 |

Sanitizer suites ran as `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99
EDIT_DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1
sh tools/with_display_lock.sh build/san/tests/<name>`, outside the sandbox
(LeakSanitizer on). No reruns needed.

editor_session_test printed, after the rebase, the 3620 line
"Keep with unreadable target stat=1 stale=yes notice=Keep failed: R reload / K keep"
and all eight dxas variants "replaced=no undone=yes".
dataloss_test ze59 lines (sanitized build, tracked only): keystroke
input->submit=0.207 ms, Save As Z input->submit=1.106 ms (M)[AC Full],
load1 5.38 / 5.19, against G1 p99 2.000 ms.

## Unresolved
None.
