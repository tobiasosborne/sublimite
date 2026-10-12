# edit-379 (P1): ipc_test fails at :752 "accepted" on fresh sanitizer builds

Worker: Claude Opus investigation worker, 2026-10-13 03:00-03:25 AEST. Power: BAT0 "Not charging"
(on AC, battery full); load average 3.8-5.2 on 12 threads at start. No timing numbers are claimed.

## Verdict: (b) code/test drift, not the environment. Two stale test expectations, both deterministic.

The failure is not tonight's environment: it reproduces with a fresh private XDG_RUNTIME_DIR
(/tmp/claude-1000/edit-379/rt), DISPLAY=:99, with no `sublimite` process running (`pgrep -a
sublimite` empty), and also with the system XDG_RUNTIME_DIR. The suite isolates itself under
mkdtemp `/tmp/sublimite-ipc-XXXXXX`, so leftover sockets elsewhere cannot reach it. It also fails at
faa7baa (session 11 end), so none of the session-12 candidates (91c, 2cs, 2b8, u82) caused it.

Why "old binaries pass": main's `build/san/tests/ipc_test` was linked 2026-10-11 14:00:44
(`ls --full-time`), from d8593c8 (edit-08z, committed 10-11 13:49), i.e. before both causes below.
Most of main's `build/san/tests/*` date from 10-11 13:47-13:48: `make check` has not been run in the
main checkout since, so that "passing" binary was stale, not evidence about the environment.

## Cause 1: hot exit (69278e0, edit-457.8) vs t_main_launcher, line 752

`git bisect` d8593c8 (good, verified) .. faa7baa (bad): first bad 69278e0. The bisect log:
30aa0bf bad, 9a58341 good, 06dbc22 good, ae042e8 good, 08b17ef bad, 69278e0 bad.

Mechanism: t_main_launcher runs three launch modes with the same XDG_DATA_HOME (= rt). Mode 0
closes both tabs and quits; mode 1 kills the UI (`_exit(17)`) with both tabs open, leaving
`rt/sublimite/session-*` unlocked; mode 2's UI runs `editor_session_restore`
(src/editor/recovery.c), which adopts the newest unlocked `session-*` sibling and restores its 2 tabs,
then opens the 2 requested ones: tabs = 2 + 2 = 4 instead of 2 (log: `split=0 token=0 live=0 tabs=4`).
Restoring a crashed session is intended hot-exit behaviour; the test predates it.

Fix (test only): each mode gets its own `rt/data-<mode>` as XDG_DATA_HOME.

## Cause 2 (was masked by cause 1): edit-ret (20d0806) vs t_astra_many_open, line 926

With cause 1 fixed the suite failed next at `CHECK(editor_get_stats(e).source_stale)`, also
deterministic, both `astra_many_open` and `astra_many_open_local`. Bisect d8593c8..c62d11c with
only `astra_many_open_local`: first bad 20d0806 (edit-ret: clean buffers reload silently on external
change). The test appends "changed\n" to a clean buffer and waited for source_stale, which edit-ret
by design no longer raises for clean buffers.

Fix (test only): wait (same 5000 x 1 ms watchdog as before) for the silent reload, i.e. for
`editor_length(e)` = 5 B "text\n" + 8 B "changed\n" = 13 B, stepping with `editor_poll_sources` and
`editor_step(e, 0)`, and assert `!source_stale`. Delivery through the shared epoll after retirement
is still what is proved: the reload only happens if the watch event arrives.

## Red / green (worktree .wt/edit-379-s6 from main c62d11c, branch wt/edit-379-s6)

- Red: c62d11c, `build/san/tests/ipc_test`, rc=1, `main launcher primary: split=0 token=0 live=0
  tabs=4 ... FAIL tests/ipc_test.c:752: accepted`. After fix 1 only: rc=1, `FAIL
  tests/ipc_test.c:926: editor_get_stats(e).source_stale` (plus LSan leak report from the abort).
- Green: full sanitizer suite 3/3 runs rc=0 with the private runtime dir, 1 run rc=0 with the system
  XDG_RUNTIME_DIR, release `build/tests/ipc_test` rc=0; both main launchers (primary and isolated)
  and both astra variants print their pass lines. `make all` exit 0 (gcc, -Werror).
- Diff: tests/ipc_test.c only, +17 / -5. No src change.

## Not fixed, reported

- journal_kill_test still fails on c62d11c with this change and in main's own binary rebuilt at
  03:08: `FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0` (rc from
  `journal_replay_file`). Pure journal test, unrelated to IPC or hot-exit restore. Journal commits
  since d8593c8: 69278e0 (457.8), 84685d6 (pgr, BEGIN/COMMIT framing), 5145c0c (rfq). Not bisected;
  needs its own bead. Likely the same kind of drift (test predates a format change), unverified.
- 7 `/tmp/sublimite-ipc-*` directories exist; some are from failing runs (mine and others): a CHECK
  abort skips `rm_rf`. Not deleted (outside my dirs). Backlog-grade.
- Workers outside a socket-capable sandbox should no longer see ipc_test fail; inside the Codex
  sandbox it still fails because sockets are blocked (environmental, expected).

Scratch bisect worktree .wt/edit-379-bisect removed. No commits made; bd not run.
