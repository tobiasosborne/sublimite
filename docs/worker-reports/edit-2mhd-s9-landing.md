# edit-2mhd landing report (s9)

Lander: Claude Opus subagent. Worktree `.wt/edit-2mhd-s9`, branch `wt/edit-2mhd-s9`.
Main was not touched; the coordinator fast-forwards it.

## Pre-check

The worker report `docs/worker-reports/edit-2mhd-s9.md` has two red selectors
(`--minimap-grab-inside` and `--minimap-grab-outside`, exit 1 before the production change).
Both turned green (exit 0). Every other verification row has exit 0, so the landing proceeded.
The decision `docs/decisions/edit-2mhd.md` was read.

## Commit and rebase

- `build/briefs/land9.sh 2mhd "edit-2mhd: minimap viewport grab keeps the pointer offset; outside press centres (bd-2mhd)"`
  exited 0. The commit adds the decision and the worker report, and modifies
  `src/editor/paint.c`, `src/editor/private.h` and `tests/editor_test.c`.
- Rebase onto main `df277ff` (Session 19 worklog, after edit-uzrb) was clean, with **no conflicts**.
  Rebased commit: `0713a18`.
- Inside land9.sh: `touch src/*/*.c src/*/*.h tests/*.c` then `make -j3 all` (gcc, -Werror) exited 0.

## Verification

Each display run used a fresh `XDG_RUNTIME_DIR=$(mktemp -d)` plus `DISPLAY=:95 EDIT_DISPLAY=:95
EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh <bin>`.
Runs happened outside the sandbox so LeakSanitizer could inspect threads.

| Command | Exit |
| --- | --- |
| `make -j3 build/san/tests/editor_test build/san/tests/editor_scroll_test` | 0 |
| `make -j3 build/tests/editor_test` | 0 |
| `build/san/tests/editor_test` (full suite) | 0 |
| `build/san/tests/editor_scroll_test` (full suite) | 0 |
| `EDITOR_ALLOC_ONLY=null build/tests/editor_test` | 0 |
| `EDITOR_ALLOC_ONLY=raster build/tests/editor_test` | 0 |

No case failed, so nothing needed a rerun.

Output excerpts (M)[AC]:

```text
san editor_test:
edit-2mhd inside: press top=7 expected=7; motion top=10 expected=10; mallocs=0 guard=ASan-inert
edit-2mhd outside: press top=14 expected=14; motion top=17 expected=17; mallocs=0 guard=ASan-inert
editor_test: all passed
san editor_scroll_test: 13 PASS lines, 0 lines matching fail|error
release null:   editor_test: null 10000 keys mallocs=0 guard=active
release raster: editor_test: raster 10000 keys mallocs=0 guard=active
```

The 10000 key count comes from the existing `allocation_case` loop. The expectation of 0 mallocs
comes from the no-malloc law (CLAUDE.md law 2).

## Power and load stamps

```text
start of san editor_test: Charging; 02:58:25 up 1:23, load average: 30.27, 24.87, 22.29
end of release raster:    Charging; 03:06:25 up 1:31, load average: 10.55, 15.33, 18.91
```

## Unresolved

Nothing is unresolved.
