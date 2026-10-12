# edit-rzj worker report

Implemented recovery of orphan sessions behind a newer checkpoint. Discovery
continues through unlocked journals, appending recovered tabs under fresh editor
IDs and publishing a durable merged checkpoint before retiring each source.
The newest recovered session keeps its active tab and window geometry. A source
that exceeds configured tab capacity remains discoverable with a recovery
notice. No typing-path work or allocation was added.

Files touched:

- `src/editor/recovery.c`: descending discovery cursor, source replay helper,
  append-safe session installation, capacity preflight and notice.
- `tests/dataloss_test.c`: promote stranded-orphan to default mode; synchronize
  the living instances, use relative timestamp ordering, verify repeated
  launches, capacity deferral and independent edits to a shared target. Add
  `--stranded-orphan` for focused verification.
- `tests/editor_session_test.c`: replace the old oversized-session set-aside
  expectation with discoverable retention and later successful recovery.
- `docs/decisions/edit-rzj.md`: design and integration notes for edit-fi2.
- `docs/worker-reports/edit-rzj-s6.md`: this report.

Generated binaries and run logs are under this worktree's `build/`. No forbidden
tracking commands were run; protected handoff, worklog and requirements files
were not edited. The worker stops after this bead.

## Evidence conventions and environment

All run observations below are (M)[AC], stamped by BAT0 `Not charging` before
runs. Compiler versions were read from `--version`: gcc release major thirteen
and clang sanitizer major eighteen (M)[AC]. Builds use the Makefile's C11 and
requested warning flags. Build parallelism uses `nproc`, deriving concurrency
from the available processor count rather than a chosen budget.

In pasted output, launch indices are (P) zero-based initial recovery, its
replacement checkpoint, and that checkpoint's replacement. Tab counts derive
from the crashed and surviving fixture instances (P); seen flags are Boolean
absence/presence (P). Assertion line numbers are (M)[AC] `__LINE__` from the
compiled test. Exit zero means success and exit one means failure (P: process
exit convention). File byte counts are (M)[AC] `fstatat` observations, not
storage limits. Source IDs, bead IDs and display names are identifiers.

## Red run

Before implementation, the supplied probe failed exactly on the stranded
journal. Command: `build/san/tests/dataloss_test --probe`. Exit one (P: failure).
Relevant exact output, also retained in `build/red-edit-rzj.log`:

```text
dataloss_test: stranded_orphan launch 0: tabs=1 seen_crashed=0 seen_survivor=1
dataloss_test: stranded_orphan launch 1: tabs=1 seen_crashed=0 seen_survivor=1
dataloss_test: stranded_orphan launch 2: tabs=1 seen_crashed=0 seen_survivor=1
dataloss_test: stranded_orphan leftover session-o703hU 8192 bytes
dataloss_test: stranded_orphan leftover session-9ZkLtf 541 bytes
dataloss_test:588: FAIL seen_a
dataloss_test --probe: changed_base(deleted)=ok torn_undo_group=ok stranded_orphan=FAIL
dataloss_test:610: FAIL !deleted && !torn && !stranded
```

Then the promoted, synchronized regression was compiled before implementation.
Command: `build/san/tests/dataloss_test --stranded-orphan`. Exit one (P: failure).
Exact assertion output in `build/red-regression-edit-rzj.log`:

```text
dataloss_test: stranded_orphan launch 0: tabs=1 seen_crashed=0 seen_survivor=1
dataloss_test:601: FAIL seen_a
dataloss_test:622: FAIL stranded_orphan(false) == 0
```

These runs also emitted LeakSanitizer's fatal ptrace diagnostic. It does not
change the demonstrated missing tab; subsequent runs disable leak scanning
because this sandbox denies the scanner's thread inspection. ASan and UBSan
remain enabled. `ASAN_OPTIONS=detect_leaks=0` uses zero (P: disabled), not a
changed editor policy. Leak scanning still needs an unrestricted coordinator
run.

## Green run

Command: `ASAN_OPTIONS=detect_leaks=0 build/san/tests/dataloss_test --stranded-orphan`.
Exit zero (P: success). Exact recovery output from
`build/green-regression-edit-rzj.log`; leftover-file lines are omitted here:

```text
dataloss_test: stranded_orphan launch 0: tabs=2 seen_crashed=1 seen_survivor=1
dataloss_test: stranded_orphan launch 1: tabs=2 seen_crashed=1 seen_survivor=1
dataloss_test: stranded_orphan launch 2: tabs=2 seen_crashed=1 seen_survivor=1
dataloss_test: stranded_orphan launch 0: tabs=1 seen_crashed=0 seen_survivor=1
dataloss_test: stranded_orphan launch 1: tabs=2 seen_crashed=1 seen_survivor=1
dataloss_test: stranded_orphan launch 2: tabs=2 seen_crashed=1 seen_survivor=1
dataloss_test: stranded_orphan launch 0: tabs=2 seen_crashed=1 seen_survivor=1
dataloss_test: stranded_orphan launch 1: tabs=2 seen_crashed=1 seen_survivor=1
dataloss_test: stranded_orphan launch 2: tabs=2 seen_crashed=1 seen_survivor=1
```

The blocks correspond to ordinary separate-target recovery, initial capacity
deferral followed by normal-capacity recovery, and shared-target recovery.
Assertions also verify modified flags, newest-session active tab, and source
retirement. The capacity fixture verifies both the notice and retained source.

Command: `ASAN_OPTIONS=detect_leaks=0 build/san/tests/dataloss_test --probe`.
Exit zero (P: success), `build/dataloss-probe-edit-rzj.log`:

```text
dataloss_test --probe: changed_base(deleted)=ok torn_undo_group=ok stranded_orphan=ok
dataloss_test: kill-after-append, kill at every save step, shorter base, reload/keep during held save, Ctrl+S under prompt, mixed hot exit with closed tab, reload vs keystroke, two-process save and orphan recovery passed
```

Command: `ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_session_test`.
Exit zero (P: success), `build/editor-session-edit-rzj.log`:

```text
editor_session_test: three launches leave one nonempty 0600 journal; changed base opens and sets the source aside
editor_session_test: headless save/repeated/new-target, cutoff edits, shared targets, inactive failure, interrupted-save startup, Saving acknowledgement, allocation guard passed
```

Here the launch generations are the existing campaign roles (P); POSIX octal
0600 grants only owner read/write (P), and the surviving journal follows the
existing checkpoint-retirement contract (P).

## Broader verification and remaining coordinator work

Final release `make -j"$(nproc)" all` passed, exit zero (P: success), recorded in
`build/make-all-edit-rzj.log`. Sanitizer `make check` built all suites, then could
not acquire the shared display lane at `cli_test`. The first attempt was stopped
at that wait; the rerun used `EDIT_DISPLAY_LOCK_TIMEOUT=0` (P: nonblocking lock
admission), and reported the lock timeout. `build/make-check-edit-rzj.log` records
that failure. No check success is claimed.

Direct connection to the coordinator's Xvfb socket failed with `Operation not
permitted` (M)[AC]. Display suites therefore cannot pass inside this sandbox;
the coordinator must rerun them on Xvfb :99 (user-specified identifier).
`editor_test` was run directly and failed its native backend initialization
checks; it also failed the existing launcher-wait child assertion. The latter
uses the external launch/display path. `file_kill_test`, despite being outside
the Makefile display filter, also creates a platform/render viewport and failed
those display-dependent cases. Their logs are `build/targeted-edit-rzj.log` and
`build/nondisplay-edit-rzj.log` respectively.

The specified `journal_test` and `journal_retire_test` passed under ASan/UBSan.
The specified `journal_kill_test` failed its existing initial replay trial;
exact output follows from `build/targeted-edit-rzj.log`:

```text
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
```

These are (M)[AC] test counters and a journal return code. The initial trial is
(P) kill-before-pump, its target is the first operation (P), flags/counters use
zero/one for none/single (P), and rc is the existing journal result enum. This
suite calls journal APIs directly, with no editor discovery calls; it was not
changed. The brief permits a sandbox failure for this suite; read-only inspection found a stale wire-size assertion: its replay visitor
requires the legacy WINDOW payload, whereas `journal_set_window` emits the
current flagged payload. (P) Legacy size is eight bytes, width32 + height32;
current size is twelve bytes, width32 + height32 + flags32. The visitor returns
failure, which replay maps to `JOURNAL_CALLBACK` (P: existing enum rc six).
This is an independent test compatibility issue, not established as a sandbox
problem; it was reported without changing the suite.

Outside this bead: the unchanged `two_process_save` fixture intermittently
fails simultaneous startup, before the orphan regression is reached. It prints
`editor_open returned -103` (P: `EDITOR_ERR_IO` enum), the child startup assertion,
and then a broken-pipe failure or SIGPIPE. This was observed before implementation
and again afterward. The focused fixture avoids simultaneous initialization
while retaining both live instances, and the full probe also completed green.
This unrelated startup race was reported, not fixed. Earlier failures remain
in `build/targeted-edit-rzj.log` and `build/dataloss-default-edit-rzj.log`.


The full default dataloss suite subsequently passed serially, exit zero
(P: success), in `build/dataloss-default-rerun-edit-rzj.log`:

```text
dataloss_test: kill-after-append, kill at every save step, shorter base, reload/keep during held save, Ctrl+S under prompt, mixed hot exit with closed tab, reload vs keystroke, two-process save and orphan recovery passed
```

All suites outside the Makefile's display filter were run individually with
ASan/UBSan so the early display-lock stop could not hide their results.
`build/nondisplay-status-edit-rzj.log` records every exit; the only failures in
that pass were `file_kill_test` (its embedded display viewport) and `ipc_test`
(Unix socket initialization denied in the restricted sandbox). All other
entries passed; full output is `build/nondisplay-edit-rzj.log`. This includes
journal groups/retirement, save and reload controllers, file, piece, layout,
render, work, configuration, Unicode, startup and window-size checks.

The headless editor hot-exit and scroll suites passed. The close suite passed
its headless case but failed its native case, consistent with the inaccessible
X socket. The large-editor suite initially failed Unix IPC initialization.
Logs: `build/headless-editor-edit-rzj.log`.

A permitted sandbox escalation reran `ipc_test`: socket initialization worked,
but its existing `main_launcher_primary` accepted-request assertion failed.
`build/ipc-unrestricted-edit-rzj.log` contains the launcher output and assertion.
This is outside the orphan-recovery bead and was not fixed. An escalated
large-editor run passed IPC admission but failed the existing
`cooperative_large_jobs` deadline while the benchmark was active; that log is
`build/editor-large-unrestricted-edit-rzj.log`. These additional failures mean
the complete non-display acceptance result cannot be claimed from this worker.

The existing release editor benchmark ran with its recorded `--track` argument.
Its null-backend workloads reported structurally valid input/journal accounting
and no typing allocations (M)[AC], but its raster stage failed opening the
native backend in the sandbox. No performance limit was added or changed.
Complete measured rows, power and load stamps are in
`build/editor-bench-edit-rzj.log`; completion is failure at the display stage,
not a successful full benchmark campaign.


A final serial large-editor rerun with local Unix IPC permitted passed,
exit zero (P: success), in `build/editor-large-serial-edit-rzj.log`:

```text
editor_large_test: ok
```

The outstanding broader verification failures are the display-dependent cases,
the independent IPC launcher assertion, and the stale journal-kill WINDOW
assertion described above. The large-editor deadline failure did not recur on
this serial rerun. LeakSanitizer remains unverified because of the sandbox's
thread-inspection restriction. The implementation and bead regression are
complete; unrestricted full-check verification and investigation of the
reported outside-scope failures remain with the coordinator.

The remaining non-display shell check,
`ASAN_OPTIONS=detect_leaks=0 sh tools/test_replay_cli.sh build`, passed, exit zero
(P: success), with output in `build/replay-cli-edit-rzj.log`. Its initial run
without that environment override failed only at LeakSanitizer's ptrace scan;
the successful run retains ASan/UBSan.
