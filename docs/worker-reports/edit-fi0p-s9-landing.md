# edit-fi0p landing report (s9)

Lander: worktree `.wt/edit-fi0p-s9`, branch `wt/edit-fi0p-s9`. Main was not
touched or fast-forwarded. No `bd` used.

Power: `cat /sys/class/power_supply/BAT0/status` = `Not charging`, AC
`online` = `1` (M)[AC], before and after the runs.

## Rebase

- `build/briefs/land9.sh fi0p "<message>"` committed the worker tree
  (916385b) and reported `REBASE CONFLICT` (`UU tests/dataloss_test.c`), rc=3.
- `git rebase main` by hand onto main 1620d96. Auto-merged without conflict:
  `src/editor/commands.c`, `src/editor/editor.c`, `src/editor/private.h`
  (and retain.c, restore.c, recovery.c, journal, tools: no overlap). The
  merged hunks were read against main: F8's closed-peer refusal sits before
  utin's Save As path and leaves the peer-reload loop untouched; F4's
  `journal_retry` repair in `editor_flush` precedes y315/ze59's owed-publish
  wait and only adds `repaired` to the checkpoint condition; F9's revision
  field and FAILED->IDLE condition sit beside 7cj8/9fzc retention states.
  Both behaviours are kept everywhere.
- Only conflict, `tests/dataloss_test.c`:
  - Region 1 (utin/e3z5 cases + nlz selector table vs fi0p's
    `lazy_active_tab`): kept every HEAD case, inserted `lazy_active_tab`
    after them, added `select_fi0p_lazy_active(void)` (returns
    `lazy_active_tab(false) || lazy_active_tab(true)`, as the ladder did) and
    folded fi0p's three selectors into the table as one row each, in the
    worker's order at the head of the table:
    `--fi0p-lazy-active` -> `select_fi0p_lazy_active`,
    `--fi0p-retain-retry` -> `retain_after_failure`,
    `--fi0p-closed-peer` -> `closed_peer_save_as`.
  - Region 2 (`main`): kept nlz's `TEST_SELECT(selectors, ...)`; dropped
    fi0p's if/strcmp ladder (its old-selector lines were main's pre-nlz set).
  - Default-run list auto-merged with both sides' calls: utin's three and
    fi0p's `lazy_active_tab(false/true)`, `retain_after_failure()`,
    `closed_peer_save_as()`.
- `editor_checkpoint_test` keeps its existing `wanted("...")` mechanism
  (nlz did not convert it); fi0p's `fi0p-barrier` uses that.
- `git rebase --continue`: rebased commit 2801e50 on 1620d96.

## Build

| Command | Exit |
|---|---|
| `touch src/*/*.c src/*/*.h tests/*.c tools/*.c; make -j3 all` (gcc, -Werror) | 0 |
| `make -j3 build/san/tests/{editor_checkpoint,dataloss,journal,journal_retire,editor_session,editor_hot_exit,editor,editor_recovery,journal_ack}_test` | 0 |
| `make -j3 build/san/tests/replay_test` | 0 |

## Suites

Each run as `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:97 EDIT_DISPLAY=:97
EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 sh
tools/with_display_lock.sh build/san/tests/<name>`, outside the sandbox.
Exit codes and stamps are observed (M)[AC]; raw log `build-land-suites.log`.
No ASan/UBSan/LSan report in the log.

| Suite | Exit | `uptime` at end |
|---|---|---|
| editor_checkpoint_test | 0 | 06:31:09 load 5,02 4,03 4,15 |
| dataloss_test | 0 | 06:31:58 load 3,49 3,76 4,05 |
| journal_test | 0 | 06:32:06 load 3,41 3,73 4,04 |
| journal_retire_test | 0 | 06:32:07 load 3,41 3,73 4,04 |
| editor_session_test | 0 | 06:32:33 load 3,13 3,64 4,00 |
| editor_hot_exit_test | 0 | 06:32:43 load 2,96 3,59 3,98 |
| editor_test | 0 | 06:35:29 load 1,45 2,79 3,62 |
| editor_recovery_test (new) | 0 | 06:35:29 load 1,45 2,79 3,62 |
| journal_ack_test (new; covers `journal_replay --acknowledge`) | 0 | 06:35:29 load 1,45 2,79 3,62 |
| dataloss_test --fi0p-lazy-active / --fi0p-retain-retry / --fi0p-closed-peer (table rows) | 0 / 0 / 0 | ~06:36 |
| `sh tools/test_replay_cli.sh build` (first run) | 1 | 06:35:29 |
| `sh tools/test_replay_cli.sh build` (after building replay_test) | 0 | 06:35:49 load 1,25 2,66 3,55 |
| release `build/tests/editor_test`, `EDITOR_ALLOC_ONLY=null` | 0, `mallocs=0 guard=active` | 06:35:29 |
| release `build/tests/editor_test`, `EDITOR_ALLOC_ONLY=raster` | 0, `mallocs=0 guard=active` | 06:35:35 |

The first `test_replay_cli` run failed only because its dump generator
`build/san/tests/replay_test` had not been built in this worktree
("not found"); the script exercises `tools/replay`, not
`tools/journal_replay`. After building it the script passed.

`df -h /` before and after dataloss_test (the suite with the sparse large
fixtures): `480G 396G used, 60G avail, 87%` both times; unchanged at the end.

## Unresolved

- `cli_test:581` was not run: it fails on main independently (edit-3oew,
  another lane).
- The recovery-3 coverage campaigns listed in `edit-fi0p-s9.md` "Remaining
  scope" stay in the bead.
