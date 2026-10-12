# edit-cxo-s6: two_process_save flake (session placeholder retired before its lease)

Worker: Opus 5.5, 2026-10-13. Worktree `.wt/edit-cxo-s6` (branch
`wt/edit-cxo-s6`, base main ff9d7de). Not committed.
Usage at start: 5-hour 8.0 %, weekly 86.0 % (`claude-usage`).
Power: `/sys/class/power_supply/BAT0/status` = "Not charging" (AC attached)
before every measurement; load1 is stamped in each loop below.

## Root cause (confirmed, as edit-dlfix-s6 found with strace)

`src/main.c` and the dataloss fixture `mkstemp` the session journal; its lease
`.session-XXXXXX.lock` is flocked only later in `editor_session_restore`. A
concurrently starting instance's `journal_retire_directory` (edit-rfq) can
take that lease, judge the empty generated journal clean, and rename+unlink
it. The owner's `editor_open` then returns EDITOR_ERR_IO (-103).

## Fix (decision: docs/decisions/edit-cxo.md)

- `src/journal/session.c` (new) `journal_session_create(dir, path, cap, &lease)`:
  mkostemps the lock name, flock it, then `O_CREAT|O_EXCL` the
  `session-XXXXXX` journal. The visible name never exists without a held lease.
- `src/journal/journal.h`: declaration.
- `src/editor/editor.h`: `editor_config.session_lease` (borrowed `const int *`).
- `src/editor/recovery.c` `editor_session_restore`: with a supplied lease,
  `F_DUPFD_CLOEXEC` it (same open file description, so no self-conflict) and
  refuse it unless its inode is this journal's lock file; otherwise unchanged.
- `src/main.c`: uses `journal_session_create`, passes the lease, closes it
  after the empty-placeholder cleanup.
- `tests/dataloss_test.c`: two_process_save creates both sessions with
  `journal_session_create` and hands each lease to its editor (the fixture had
  the same window as main.c). Other single-process cases are unchanged.
- `tests/journal_retire_test.c`: new deterministic case `startup_window`.

Rejected: holding the directory flock across create+lock. The creating
process's own retirement would then fail on its own guard (flock is per open
file description), and the lock would span main's backend/display startup.

## Red (journal_session_create stubbed to the pre-fix order: mkstemp, lease -1)

```
display guard: real display :0 refused, using :99
journal_retire_test:298 FAIL access(path,F_OK)==0
journal_retire_test:320 FAIL startup_window()==0
exit=1
```

Pre-fix dataloss_test (sanitizer build, default mode, headless, private
XDG_RUNTIME_DIR), 20 sequential runs (M)[AC], load1 1.80 before, 2.43 after:
9 of 20 runs failed (M): runs 3, 5, 6, 7, 10, 12, 14, 16, 18. Every failing run
printed `dataloss_test: editor_open returned -103` once, at dataloss_test:501
(child, 7 runs) or :519 (parent, 2 runs). Exit 141 (6 runs) is SIGPIPE in the
surviving process after its peer died; exit 1 (3 runs) is the T() failure.
9/20 = 45 % here, against edit-dlfix-s6's 3 of 14 (M): the window depends on load.

## Green

```
journal_retire_test: passed
exit=0
```
Post-fix dataloss_test (same build flags and environment) (M)[AC], load1 2.32
before, 1.09 after: default mode 10 of 10 runs exit 0, `--probe` 10 of 10 runs
exit 0, 0 occurrences of `returned -103` or `FAIL` across all 20 logs.
Every run printed the two_process_save and stranded_orphan lines as passed.

Other suites (sanitizer build, `tools/with_display_lock.sh`, DISPLAY=:99,
EDIT_DISPLAY=:99, XDG_RUNTIME_DIR=/tmp/claude-1000/cxo-xdg):
- editor_session_test exit=0
- cli_test exit=0 (includes "XDG session journals", which drives src/main.c)
- journal_retire_test exit=0
- journal_test exit=0 (headless)
- `make -j12 all` exit 0
- `src/file/check_signal_globals.py build/rel/src/journal/session.o`:
  `mutable globals=0`.

## Contracts

No typing-path work: one mkostemps, flock, open, dup and two stats at startup.
Crash after lock creation leaves a dotted lock file, which retirement and
discovery already ignore. The edit-btj `recovery_skip` reopen now keeps the
lease across both attempts; before, it was released between them.

## Not done / notes

- Lock files remain permanent (edit-rfq: stable lease infrastructure). This
  bead does not change that; one lock file per launch accumulates as before.
- Other tests that `mkstemp` a session and open one editor are not racy (no
  second instance) and were left alone.

Usage at end: 5-hour 8.0 %, weekly 86.0 % (`claude-usage`).
