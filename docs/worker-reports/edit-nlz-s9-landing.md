# edit-nlz — landing report (s9)

Lander: Opus subagent. Worktree `.wt/edit-nlz-s9`, branch `wt/edit-nlz-s9`.
Bead commit rebased once onto main `d9627f7`; main was not fast-forwarded.

## Pre-check

Worker report (`docs/worker-reports/edit-nlz-s9.md`): select_test red then green;
make all and release malloc guards green. Its two reds (cli_test default hot-exit
comparison; intermittent dataloss `ivv_choice` directory-lock assertion) were
shown pre-existing / intermittent on the original sources, not bead-introduced.
The dataloss lock assertion is what edit-qzam (main `bae040c`) since fixed.

## Rebase

`build/briefs/land9.sh nlz "<msg>"` -> rc 3, REBASE CONFLICT in
`tests/dataloss_test.c`, `tests/editor_session_test.c`, `tests/editor_test.c`
(script aborted the rebase). Then `git rebase main` by hand; resolutions (main
since cf416ea: 9f63, e3z5, xdro, utin, qzam):

- `tests/editor_test.c`: took the table form; added row
  `{"--9f63-columns", minimap_text_columns}` after `--minimap-grab-outside`
  (same position as main's ladder line). Default call
  `T(minimap_text_columns() == 0)` merged cleanly.
- `tests/editor_session_test.c`: table form; added row
  `{"--edit-xdro", xdro_all}` after `--edit-ivv` (main's position). Default
  `T(xdro_all() == 0)` merged cleanly.
- `tests/dataloss_test.c`: kept main's utin case bodies (`utin_save_as`,
  `utin_deferred`) ahead of the nlz adapters; added adapters
  `select_edit_e3z5` / `select_edit_utin` (bodies verbatim from main's ladder,
  every case runs, `failed |=`), rows `--edit-e3z5`, `--edit-utin` after
  `--reload-held` (main's order). Defaults for e3z5/utin and qzam's settle code
  merged cleanly.
- `tests/cli_test.c`, `tests/editor_large_test.c`: no main changes since cf416ea.

`git rebase --continue` -> rc 0, commit `433777e` on `d9627f7`.

Evidence that no ladder remains (script compares every
`strcmp(argv[1], "--x")` selector of main's file with the table rows of the
landed file, and their relative order):

| file | main ladder selectors | table rows | only in main | order equal |
|---|---|---|---|---|
| editor_test.c | 104 | 103 | `--headless` (special path, kept) | yes |
| cli_test.c | 4 | 4 | — | yes |
| editor_session_test.c | 13 | 13 | — | yes |
| dataloss_test.c | 23 | 22 | `--probe` (default-run modifier, kept) | yes |

`grep -c 'strcmp(argv'`: editor_test 1 (`--headless`), dataloss_test 1
(`--probe`), cli_test 0, editor_session_test 0, editor_large_test 0. The
`--o20-` prefix stays a `strncmp` path as the decision doc says.
`git diff main` of session and dataloss tests touches no default-run line.

## Build

Stamps: `cat /sys/class/power_supply/BAT0/status` = `Not charging` (AC
connected, not charging) throughout; `uptime` load per row below.

| command | rc |
|---|---|
| `touch src/*/*.c src/*/*.h tests/*.c && make -j3 all` (gcc, -Werror) | 0 |
| `make -j3 build/san/tests/{select_test,editor_test,editor_session_test,dataloss_test,editor_large_test,cli_test}` | 0 |

## Suites

Each as `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:97 EDIT_DISPLAY=:97
EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 sh
tools/with_display_lock.sh <binary> [selector]`, sequential, outside sandbox.

| binary [selector] | start (uptime, load1/5/15) | rc |
|---|---|---|
| build/san/tests/select_test | 06:20:31, 4.06 4.12 4.86 | 0 |
| build/san/tests/editor_test | 06:20:31, 4.06 4.12 4.86 | 0 |
| build/san/tests/editor_session_test | 06:23:15, 1.86 3.15 4.37 | 0 |
| build/san/tests/dataloss_test | 06:23:41, 2.27 3.13 4.32 | 0 |
| build/san/tests/editor_large_test | 06:24:29, 2.34 3.02 4.23 | 0 |
| build/san/tests/cli_test | 06:24:39, 2.51 3.04 4.22 | 1 |
| build/san/tests/dataloss_test --edit-utin | 06:24:40, 2.51 3.04 4.22 | 0 |
| build/san/tests/editor_session_test --edit-dxas | 06:24:42, 2.55 3.03 4.21 | 0 |
| build/san/tests/editor_test --9f63-columns | 06:24:47, 2.59 3.03 4.20 | 0 |
| build/san/tests/editor_session_test --edit-xdro | 06:24:47, 2.59 3.03 4.20 | 0 |
| build/san/tests/dataloss_test --edit-e3z5 | 06:24:49, 2.59 3.03 4.20 | 0 |
| build/san/tests/cli_test (rerun alone) | 06:25:21, 2.42 2.95 4.13 | 1 |
| main's `tests/cli_test.c` (git show main:), same flags, same libedit.a and build/sublimite | 06:25:22, 2.42 2.95 4.13 | 1 |
| build/tests/editor_test, EDITOR_ALLOC_ONLY=null | 06:25:23, 2.42 2.95 4.13 | 0, `null 10000 keys mallocs=0 guard=active` |
| build/tests/editor_test, EDITOR_ALLOC_ONLY=raster | 06:25:23, 2.42 2.95 4.13 | 0, `raster 10000 keys mallocs=0 guard=active` |

Each focused selector printed its case lines (utin: both save_as reasons and
both deferral branches; dxas: journal/mode/target rows; 9f63: column line;
xdro and e3z5 cases), so the folded rows dispatch to the right functions.

## Unresolved (not this bead)

cli_test is red on main's own source too, in the hot-exit CLI block that the
bead does not touch (its cli_test.c diff is the selector only):

- first run: `cli_test:499 FAIL after.count == before.count && ...` then
  `cli_test:567 FAIL hot_exit_cli(...) == 0` (same as the worker's baseline);
- rerun alone: hot_exit_cli passed, then `cli_test:582 FAIL closed == 0 &&
  rmdir(dir) == 0` (journal directory not empty at cleanup);
- main's source: `cli_test:498` / `cli_test:566`, the same as the first run.

Logged before each: `sublimite: cannot write trace dump: ...` /
`error -103; journal: ...` (the expected dump-error case). The
`window not created within 0 s [forced test timeout]` line is the expected §36
control case. Load at the time was low (load1 2.4–2.5), so it is not load.
