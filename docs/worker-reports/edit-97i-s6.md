# edit-97i worker report

Implemented resolved-path tab reuse in this worktree. CLI hand-off and Ctrl+O
activate the live tab for the same canonical path or current device/inode,
including Unicode, relative spellings, symlinks and hard links. Reuse preserves
its ID, text, undo history, modified state and retained view, works at full
capacity, and creates no duplicate journal association. Requests deduplicate
matching prepared paths; independent wait clients share distinct tab-ID
membership and finish on close. Non-origin CLI coordinates reposition the
existing tab. Reused coordinates are staged before publication so a failed
coordinate in a mixed request cannot publish its earlier new paths. Origin coordinates preserve it, including an explicitly supplied
origin because IPC does not distinguish that from omission.

Files touched:

- `src/editor/buffers.c`
- `src/editor/private.h`
- `src/editor/editor.h`
- `tests/editor_test.c`
- `tests/cli_test.c`
- `tests/editor_session_test.c`
- `bench/editor_bench.c`
- `docs/decisions/edit-97i.md`
- `docs/worker-reports/edit-97i-s6.md`

The session suite keeps its legacy shared-target save assertions and directly
seeds that state in fixture setup. The CLI benchmark now expects the distinct
path count rather than an extra tab for every invocation. No input, find-panel
or scroll-control implementation changed. No bd or git command was run; no
coordinator-owned handoff, worklog, plan, requirements or rules file was edited.

Evidence conventions: numbers in the pasted diagnostics are observed run
output (M)[AC], with BAT0 reporting `Not charging`. Source-line numbers derive
from compiler/test instrumentation; process exit statuses derive from the
operating system return codes (P). Benchmark sample counts and existing
aspirations are emitted by the unchanged fixture configuration; timings derive
from its recorded monotonic samples (M)[AC]. No budget, duration or count was
introduced. Compiler modes and warning flags follow the requested toolchain
specification (P); tool output identified gcc release and clang ASan/UBSan.

Red, before implementation:

```text
$ build/san/tests/editor_test --duplicate-path
editor_test:4029: FAIL editor_add_buffer(e, paths[i], NULL, 0, &reused) == 0
exit=1

$ build/san/tests/cli_test --duplicate-path
sublimite: handoff failed: request or editor capacity limit exceeded
cli_test:499: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
exit=1
```

Green, after implementation:

```text
$ build/san/tests/editor_test --duplicate-path
edit-97i: resolved-path reuse, full capacity, Ctrl+O, retained text/view/undo and CLI coordinates passed
exit=0
$ build/san/tests/cli_test --duplicate-path
edit-97i: production CLI Unicode/alias hand-off reuses tab; independent --wait clients finish on its close
exit=0
$ build/tests/editor_test --duplicate-path
edit-97i: resolved-path reuse, full capacity, Ctrl+O, retained text/view/undo and CLI coordinates passed
exit=0
$ build/tests/cli_test --duplicate-path
edit-97i: production CLI Unicode/alias hand-off reuses tab; independent --wait clients finish on its close
exit=0
```

The pre-existing hand-off benchmark also required a behavior assertion update.
Its red and green runs were:

```text
$ DISPLAY=:99 build/bench/editor_bench --ipc-only --track
STAMP (M)[AC] BAT0=Not charging load1=5.73 shared box
editor_bench:764 failed: editor_get_stats(e).tabs == (size_t)i + 1 && editor_view(e).selection.cursor == 5
exit=1
```

```text
$ build/bench/editor_bench --ipc-only --track
STAMP (M)[AC] BAT0=Not charging load1=6.60 shared box
BENCH name=editor_second_invocation_exec_open_ACK_exit n=64 required_n=10000 p50=4386844 p99=5109622 ci95_p50=[4335549,4428048] ci95_p99=[4687561,5109622] gate_p50=0 gate_p99=10000000 dropped=0 (M)[AC] power=Not charging load1=6.60 verdict=TRACK
IPC (M)[AC] load1=6.60 invocations=64 opened_tabs=1 includes=exec,parse,open,ACK,exit,reap mode=TRACK
exit=0
```

A further mixed-request rollback regression rejected an oversized coordinate
following a new path. Before staging the coordinate work it failed as follows
(excerpt; complete output is `build/edit-97i/rollback-red.log`):

```text
$ build/san/tests/editor_test --duplicate-path
editor_test:4073: FAIL tabs_count(&e->tabs) == tabs && e->tabs.next_id == next
SUMMARY: AddressSanitizer: 187822 byte(s) leaked in 9 allocation(s).
exit=1
```

After staging, the targeted sanitizer and release regressions passed. The final
affected-suite runs are recorded in `build/edit-97i/rollback-green.log`.

Validation:

- `make -j"$(nproc)" all`: passed with the required C standard and warnings.
  Build concurrency derives from the available processor query (M)[AC], as in
  the existing CI script. Final build log: `build/edit-97i/rollback-green-build.log`.
- Targeted editor and production CLI regressions: passed under sanitizers with
  LeakSanitizer enabled, and in the release build. They cover retained edits,
  undo/view, aliases, full capacity, Ctrl+O, repeated batch paths, rejected
  requests, coordinates and independent shared-tab wait clients.
- Full headless editor, editor-p4, close, scroll, session, hot-exit and large-file
  suites: passed. Final affected-suite rechecks are in `rollback-green.log` and
  `final-rerun.log`; earlier checks are in `editor-headless.log`,
  `final-additional.log` and `final-headless-extra.log` under `build/edit-97i/`.
  The intermittent large-file observation below is retained explicitly.
- Release typing allocation guard: passed with the guard active and no mallocs.
  Its keystroke count derives from the existing fixture; the observed output is
  in `build/edit-97i/final-headless-extra.log` (M)[AC].
- Targeted IPC capacity, editor-tabs, tokens, callbacks, disconnect and launcher
  suites: passed. Headless editor benchmark self-check and IPC tracking row:
  passed.
- All other non-display binaries were executed. They passed except the
  unrelated failures below. Filtered `make check` logs and the remaining serial
  runs are `check-headless.log`, `check-nondisplay-final.log`,
  `check-nondisplay-remaining.log` and `check-rest.log` under `build/edit-97i/`.
  After the final implementation, `final-check.log` records every selected
  non-display binary passing, including pure GL and display-policy cases,
  plus the replay CLI shell contract. Its recorded banner is:

  ```text
  check: 62 test binaries passed
  test_replay_cli: all passed
  ```

  This count derives from the selected test list and Make's emitted banner
  (M)[AC]. The documented failing suites were excluded from that final filtered
  run, rather than represented as passing. The display-lock shell post-check
  failed because the filtered run's DISPLAY_TEST_SAN override propagated into
  its nested make. A separate rerun without that override waited for another
  worker's display lane; this worker's own pending test was stopped without
  interrupting the lane owner. The coordinator should run that contract with
  normal make settings. Log: `build/edit-97i/final-lock-contract.log`.

Full `make check` is **not green**. Inside the sandbox LeakSanitizer cannot
inspect threads, and local IPC socket creation is blocked. Re-running with leak
inspection disabled reaches the blocked CLI socket fixture. Headless tests
were therefore run with approved local-socket and sanitizer access, retaining
leak checking. The display suites cannot access the X socket inside this
sandbox: the raster suite fails at `plat_init`. The coordinator must re-run the
display suites on Xvfb :99 (P: the assigned test display). Logs:
`build/edit-97i/check-sandbox.log`, `check-sandbox-no-lsan.log` and
`display-sandbox.log`.

Outside this bead's scope, reported without fixes:

- `dataloss_test` failed in its two-process save fixture with `EDITOR_ERR_IO` in
  the child's acquisition, followed by SIGPIPE in the parent. The parent saves
  before signalling its child, while the child acquires the source before
  waiting for that signal; there is no acquisition barrier against the parent's
  atomic replacement. This race explanation is an inference from the fixture
  ordering. The same failure recurred serially. Logs: `check-headless.log` and
  `editor-headless.log`.
- `ipc_test` reaches its many-open fixture but fails its expectation that a
  clean append leaves `source_stale` set. Existing clean-buffer reload behavior
  clears that state; the fixture needs coordinator triage. Leak reports follow
  its early assertion exit. Log: `check-nondisplay-final.log`.
- `journal_kill_test` fails replay with `JOURNAL_CALLBACK`. Its callback requires
  the legacy WINDOW payload shape while `journal_set_window` emits the current
  shape. Log: `check-nondisplay-remaining.log`.
- Attempting to seed the legacy shared-target save fixture through Save As
  exposed a separate failure: after the original tab saves, the peer's Ctrl+S
  leaves the original's bytes on disk instead of the peer's edits. The unchanged
  legacy fixture passes after direct setup. The Save As failure is recorded in
  `editor-headless.log`; it was not fixed here.
- `editor_large_test` passed initially, then an affected-suite rerun failed its
  `!s.top_estimated` assertion after index/find completion. Its final serial
  rerun passed. The assertion lies in the existing byte-jump/index flow; no
  change was made there. Logs: `rollback-green.log` and `final-rerun.log`.
- The referenced Astra drive report is absent from this worktree; the supplied
  bead reproduction was used.

What remains: coordinator display verification, the display-lock shell contract
with normal make settings, and triage of the untouched non-display failures and
intermittent large-file assertion. No additional duplicate-path implementation
work was identified. This is the only bead handled; the worker stops here.
