# edit-6090 landing report (s9, 2026-10-20)

Stamp at writing: power `Full`; ` 04:23:29 up  2:48,  1 user,  load average: 11,87, 8,18, 10,95`.
Power at start (03:37): Charging, load 21.46 / 22.28 / 22.82.

## Pre-checks

- Read `docs/worker-reports/edit-6090-s9.md` and `docs/decisions/edit-6090.md`.
- `git diff main --stat` before landing: `src/editor/scrollctl.c` absent (the worker's
  instrumentation was restored byte-identical). Worker diff: `src/editor/input.c`,
  `src/view/view.c`, `tests/editor_test.c` and the two docs.

## Rebase

- `build/briefs/land9.sh 6090 "<msg>"`: exit 0; committed, rebased onto 454757e with no
  conflict (17bed6b), gcc `make -j3 all` rc=0. Load after: 29.60.
- Main then moved to d63fbc9 (ut0m landed). `git rebase main`: exit 0, no conflict.
  Fix commit is now **0a69b3c** on d63fbc9. `git diff main --stat` after: 5 files,
  +397/-2 (the worker's files only).
- `touch src/*/*.c src/*/*.h tests/*.c; make -j3 all`: rc=0 (-Werror). Load 32.07 at end.

## Suites (sanitizer binaries, clang ASan/UBSan, detect_leaks=1, :95 via with_display_lock.sh, unsandboxed)

| command | rc | load1 start -> end |
|---|---|---|
| `make -j3 build/san/tests/{view_test,editor_scroll_test,editor_test}` | 0 | 27.42 at end |
| `build/san/tests/view_test` | 0 ("view_test: all passed") | 26.44 -> 14.20 |
| `build/san/tests/editor_scroll_test` | 0 | 14.20 -> 15.11 |
| `build/san/tests/editor_test` (full) | 0 ("editor_test: all passed"; 6090 case passed in-suite) | 14.78 -> 10.37 |

## Settle-deadline reruns (`build/san/tests/editor_test --6090-typing`, alone)

The full suite did not hit the settle deadline; the three reruns were run anyway, since the
worker left the intermittent 5 s `settle` timeout open. Deadline not changed.

| run | rc | wall (incl. lock wait) | load1 start -> end |
|---|---|---|---|
| 1 | 0 | 270 s | 10.26 -> 9.52 |
| 2 | 0 | 387 s | 9.52 -> 7.32 |
| 3 | 0 | 71 s | 7.32 -> 4.45 |

3/3 passed at load1 4-10. The worker's timeouts were at load1 25-32. This run did not reach
that load, so the intermittent timeout is neither reproduced nor ruled out.

## Release malloc guard (`make -j3 build/tests/editor_test` rc=0, gcc)

| mode | rc | output | load1 start -> end |
|---|---|---|---|
| EDITOR_ALLOC_ONLY=null | 0 | `null 10000 keys mallocs=0 guard=active` | 3.97 -> 11.22 |
| EDITOR_ALLOC_ONLY=raster | 0 | `raster 10000 keys mallocs=0 guard=active` | 11.22 -> 11.36 |

G1 typing latency: `tests/editor_test.c` has no separate selector for it. It runs inside
the full suite, which passed above.

## Unresolved

- The settle deadline's intermittent timeout at high load (load1 25-32) is not reproduced here.
