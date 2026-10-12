# edit-37a worker report

Implemented the saved-marker fix. A new undo branch remains dirty when it
discards the saved state, so external replacement preserves the unsaved text
and presents the existing source-change choices. A saved state at the branch
point remains clean when reached by undo.

## Files touched

- `src/editor/input.c`: set `saved_lost` before truncating redo metadata
  when `saved_cursor` is beyond `history_cursor`. No allocation, storage,
  numeric constant or error-path change.
- `tests/editor_undo_branch_test.c`: inherited from the interrupted worker;
  read, confirmed red unchanged, and retained unchanged for this bead.
- `docs/decisions/edit-37a.md`: strict-comparison rationale and saved-state
  identity behavior.
- `docs/worker-reports/edit-37a-s6.md`: this report.
- Generated validation logs are under `build/edit-37a/`. The inherited
  `build-edit-37a-red-build.log` was left unchanged.

No other production source or protected coordination document was edited.
No `bd` or `git` command was run.

## Red run

Built the inherited regression with the sanitizer configuration before editing
production code. Power stamp: (M)[AC], `BAT0/status` read `Not charging`.
Numbers in the following captured runs are (M)[AC]: diagnostic line numbers
come from `__LINE__`, and exit values come from the command's process status.
The configuration value in `detect_leaks=0` is (P): disable LeakSanitizer;
ASan and UBSan remain enabled.

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_undo_branch_test
discarded saved branch: dirty_before=false dirty_after=false owner_preserved=false source_stale=false
editor_undo_branch_test:83: FAIL dirty_before_replacement
editor_undo_branch_test:103: FAIL branch(true) == EXIT_SUCCESS
exit=1
```

This exercised the automatic-reload mechanism: the unsaved branch was clean
and the original buffer owner was replaced. Full output:
`build/edit-37a/build-edit-37a-red-run.log`.

## Green run

Rebuilt after the local `remember()` change, then ran the same regression.

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_undo_branch_test
discarded saved branch: dirty_before=true dirty_after=true owner_preserved=true source_stale=true
editor_undo_branch_test: discarded saved branch stays dirty and preserves unsaved text on external replacement passed
editor_undo_branch_test: retained saved branch point stays clean on undo and dirty on redo passed
exit=0
```

The test asserts exact unsaved bytes after external replacement and verifies
the retained saved branch point through undo and redo. The GCC release
regression also passed. Logs: `build/edit-37a/build-edit-37a-green-run.log`
and `build/edit-37a/build-edit-37a-release-regression.log`.

## Validation

- `make all`: PASS with the required GCC release toolchain and project warning
  flags. Log: `build/edit-37a/build-edit-37a-all.log`.
- Full sanitizer binary build: PASS with the required Clang ASan/UBSan toolchain
  and project warning flags. Log: `build/edit-37a/build-edit-37a-check-build.log`.
- Serial targeted runs: PASS for `editor_test --headless`,
  `editor_session_test`, `editor_p4_test --73j-null`, `unicode_render_test`,
  and `journal_test`. Logs: `build/edit-37a/build-edit-37a-targeted-run.log`
  and `build/edit-37a/build-edit-37a-targeted-rest.log`.
- Broader module suites ran serially through selected `make check` invocations,
  resuming after each failing binary. All other selected module binaries
  passed, except the reproducible `journal_kill_test` failure below.
  `work_foreground_test` initially failed its exact seek-result assertion,
  then passed an isolated serial rerun; no source was changed for it.
  `editor_close_test --headless`, the socket-free X input/protocol unit suites,
  and `FILE_KILL_CANCEL_ONLY` also passed. Evidence: the
  `build/edit-37a/build-edit-37a-nondisplay-*.log` files and
  `build/edit-37a/build-edit-37a-final-suites.log`.
- `tools/test_replay_cli.sh build`: PASS.
- Existing null-renderer benchmark path:
  `build/bench/editor_bench --track --font-baseline='DejaVu Sans Mono'`: PASS
  structurally, with no typing or tab-switch allocations. Timing misses remain
  tracked notes. Its (M)[AC] output derives latency from recorded ingress/frame
  timestamps, percentiles from the existing sample population, and allocation
  counts from the release guard. It records shared-box load; builds were
  running during the measurement. No performance comparison is claimed.
  Log: `build/edit-37a/build-edit-37a-editor-bench-headless.log`.

The default leak-enabled regression run encountered:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

Subsequent sanitizer runs disabled only leak detection. Leak-enabled checks
need the coordinator's environment.

## What remains and outside-scope findings

The implementation and regression are complete. A blanket `make check` pass
cannot be reported from this sandbox:

- The canonical `make check` stopped at the shared display lock before
  `cli_test`. The existing display-lock shell contract and display benchmark
  were also blocked by that lock; the waiting benchmark was interrupted.
  `EDIT_DISPLAY_LOCK_TIMEOUT=0` was used for later checks, (P): do not wait for
  an already-owned display resource. Logs: `build/edit-37a/build-edit-37a-check.log`,
  `build/edit-37a/build-edit-37a-shell-checks.log`, and
  `build/edit-37a/build-edit-37a-editor-bench.log`.
- Native display tests fail inside this sandbox for lack of access to the X
  socket. `refproto_test` reported a connection error. `file_kill_test` failed
  native `view_init()` and queued viewport children; its implementation calls
  `plat_init()` through `bench/file_bench.c`. Unlike the other native suites,
  it is absent from the Makefile's display-test classification. Reported here,
  not fixed. The coordinator must rerun native suites on Xvfb `:99` (the
  display identifier supplied in the worker brief).
- Listening Unix sockets are denied: a direct socket probe reported
  `Operation not permitted`; `ipc_test` confirmed that error at initialization.
  `editor_p4_test --headless` and `editor_large_test` failed at
  `ipc_server_init()`. The preceding large-editor non-IPC checks passed.
  Their complete socket-dependent suites need a coordinator rerun.
- `journal_kill_test` failed both the broad run and an isolated serial rerun
  with `JOURNAL_CALLBACK`. Its replay callback requires
  `JOURNAL_WINDOW_LEGACY_BYTES`, while `journal_set_window()` emits
  `JOURNAL_WINDOW_BYTES` including flags. See `tests/journal_kill_test.c`'s
  window-record callback and `src/journal/journal.c`'s window encoder/setter.
  Neither was changed; this is outside edit-37a. Evidence:
  `build/edit-37a/build-edit-37a-journal-kill-rerun.log`.

No additional product defect was found within the saved-marker fix. Stopping
after this bead; coordinator reruns and unrelated test fixes remain separate.
