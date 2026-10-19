# edit-ut0m landing report (s9)

Lander: Opus subagent. Worktree `.wt/edit-ut0m-s9`, branch `wt/edit-ut0m-s9`.
Main was not touched or fast-forwarded.

## Pre-check

`docs/worker-reports/edit-ut0m-s9.md` shows the red run (`error=5`,
FILE_ERR_CHANGED, journal on/off) turned green (`error=0`), and every listed
verification exit is `0`. Nothing was left red.

## Commit and rebase

- `build/briefs/land9.sh ut0m "edit-ut0m: Keep after an external rename-over refreshes the save controller's file source so the next save is admitted (bd-ut0m)"`: exit `0`.
  It committed the worker tree (src/editor/buffers.c, src/file/file.h,
  src/savectl/savectl.c, src/savectl/savectl.h, tests/editor_session_test.c,
  docs/decisions/edit-ut0m.md, docs/worker-reports/edit-ut0m-s9.md) and rebased it onto main `df277ff`.
- Rebased worker commit: `3543970`, parent `df277ff` (main at the time, after edit-uzrb landed).
- Conflicts: **none**. The rebase applied cleanly over the edit-uzrb changes.
- The script ran `touch src/*/*.c src/*/*.h tests/*.c` and then `make -j3 all` (gcc release, -Werror): exit `0`.

## Verification (M)[AC]

`make -j3 build/san/tests/editor_test build/san/tests/editor_session_test build/san/tests/file_test build/san/tests/savectl_test build/san/tests/dataloss_test build/tests/editor_test`: exit `0`.

Each suite ran outside the sandbox with a fresh runtime dir:
`XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 EDIT_DISPLAY=:95 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/<name>`

| Binary | Exit | Final line |
| --- | --- | --- |
| san `editor_session_test` | `0` | `... allocation guard passed`; includes `edit-ut0m: save after mapped rename-over/Keep error=0 journal=on` and `journal=off` |
| san `savectl_test` | `0` | `savectl_test: ok` |
| san `file_test` | `0` | `file_test: ok` |
| san `dataloss_test` | `0` | `dataloss_test: ... passed` |
| san `editor_test` | `0` | `editor_test: all passed` |
| release `editor_test`, `EDITOR_ALLOC_ONLY=null` | `0` | `editor_test: null 10000 keys mallocs=0 guard=active` |
| release `editor_test`, `EDITOR_ALLOC_ONLY=raster` | `0` | `editor_test: raster 10000 keys mallocs=0 guard=active` |

No suite failed, so none needed a rerun. No sanitizer reports (LeakSanitizer, ASan, UBSan runtime error) appear in any log.
The logs are in `build/land-runs/` (untracked).

## Stamps

```text
start: Charging | 02:59:18 up 1:24, load average: 26.74, 24.89, 22.45 | / 386G used, 70G avail
end:   Charging | 03:12:10 up 1:37, load average: 24.89, 19.63, 19.45 | / 387G used, 69G avail
```

Other landers were sharing :95 and the machine during these runs. These are pass/fail runs, and no timings are claimed.

## Unresolved

None for this bead. The worker's out-of-scope note still stands: when `file_id_stat_path(b->save_path, ...)` fails in the Keep branch, the code skips the rebase and dismisses the prompt. It is not reproduced and not changed here.
