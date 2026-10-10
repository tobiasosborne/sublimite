# edit-og2 worker report — session 9

Status: complete. Both findings are fixed and all requested worker checks passed.
Git was read-only. No `bd`, HANDOFF, worklog, Makefile, or editor-internal edits.
The CLI entry point `src/main.c` is the only production file changed outside
`src/ipc`; that is the bead's explicitly requested CLI startup adjustment.

## Findings and work completed

**Cross-suite primary collision.** Before changing implementation, the CLI test
created a decoy primary in a private runtime shared with its editor child. The
parent drained the decoy and captured successful handoff followed by child exit
before any window existed. The shared isolation helper now creates a private
runtime with mode `0700`, sets it before children start, and gives abstract
fallback tests a unique inherited namespace. Process-owned exit cleanup removes
the directory without following fixture symlinks or letting forked clients
remove their parent's endpoint. No new globals were added. Helper allocations
are test/benchmark setup only; production changes are startup only.

Audit coverage: `tests/cli_test.c`, `tests/ipc_test.c`,
`tests/editor_p4_test.c`, `bench/ipc_bench.c`, `bench/editor_bench.c`, and
`tools/test_runtime_identity.sh`. The shell launcher creates a private runtime
under its existing trapped temporary directory. Remaining editor API tests and
benchmarks create local editor objects without binding an IPC primary; no
endpoint isolation is required for those objects. Deliberately shared decoys
remain inside a runtime private to their test process.

**Cross-display handoff.** The second red run initialized the decoy while its
process declared `DISPLAY=:98`, restored the launcher to `:99`, and reproduced
the same successful handoff without a child window. The CLI now chooses its
effective safe display before arbitration. Display-specific IPC entry points
encode that display losslessly into socket and lock names, including abstract
and filesystem fallback endpoints. Different display strings cannot select the
same primary. The ordinary IPC APIs and wire protocol retain their existing
behavior. No changes inside the editor loop were needed.

Both regressions run in the normal CLI suite. The different-display regression
intentionally shares its runtime, so it proves the production fix independently
of the isolation helper. The suite also proves that same-display CLI handoff
still succeeds. IPC unit coverage checks filesystem and abstract display
routing, same-display arbitration/handoff, independent different-display
primaries, and invalid/oversized display bounds. Only the actual editor child
opens windows, always on Xvfb `:99`; the `:98` decoy binds IPC only.

## Red runs pasted (before either fix)

Each command below exited with status 1 (M)[AC]. The initial sandboxed attempts
failed at socket initialization, so the real red runs used approved local Unix
sockets; they reached the intended regression rather than a sandbox error.

```text
$ env DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/cli_test
cli_test: decoy display=:99 launcher=:99 shared runtime regression
cli_test: editor exited or stopped before window (status=0)
cli_test: decoy handoffs=1 window=0 child_exit=0
cli_test:87: FAIL waited == 0
cli_test:155: FAIL ran == 0
exit=1
```

```text
$ env DISPLAY=:99 EDIT_DISPLAY=:99 CLI_TEST_DECOY_DISPLAY=:98 ./build/tests/cli_test
cli_test: decoy display=:98 launcher=:99 shared runtime regression
cli_test: editor exited or stopped before window (status=0)
cli_test: decoy handoffs=1 window=0 child_exit=0
cli_test:87: FAIL waited == 0
cli_test:155: FAIL ran == 0
exit=1
```

## Green runs pasted

Both exact command forms now exit with status 0 (M)[AC]. Temporary journal
paths below are replaced by `<private-test-directory>` for this public report;
all other output is pasted. The dump-error message is the existing deliberate
error-path test, which expects that child to fail while the suite passes.

```text
$ env DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/cli_test
cli_test: decoy display=:99 launcher=:99 shared runtime regression
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: unsaved session journal: <private-test-directory>/sublimite/session-CgjPAr
cli_test: different-display decoy, shared runtime regression
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: unsaved session journal: <private-test-directory>/sublimite/session-azgxtN
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: cannot write trace dump: <private-test-directory>
sublimite: error -103; journal: <private-test-directory>/sublimite/session-DS1vGh
cli_test: same-display launcher handoff preserved; child_exit=0 handoffs=1
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
exit=0
```

```text
$ env DISPLAY=:99 EDIT_DISPLAY=:99 CLI_TEST_DECOY_DISPLAY=:98 ./build/tests/cli_test
cli_test: decoy display=:98 launcher=:99 shared runtime regression
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: unsaved session journal: <private-test-directory>/sublimite/session-wPxOtn
cli_test: different-display decoy, shared runtime regression
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: unsaved session journal: <private-test-directory>/sublimite/session-1wlmBO
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
sublimite: cannot write trace dump: <private-test-directory>
sublimite: error -103; journal: <private-test-directory>/sublimite/session-v3ZDUW
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: decoy bypassed; child window created; handoffs=0
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
exit=0
```

```text
$ env DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/ipc_test
ipc_test: exact display routing, same-display handoff, distinct primaries and bounds passed
ipc_test: parser, wire, roundtrip, --wait, stdin, stale, race, fragments, tokens, endpoints, review-fix suites ok
exit=0
```

## Acceptance evidence

- GCC 13.3.0 release `make -j4 all`: exit 0 (M)[AC], with the repository's
  C11 `-Wall -Wextra -Werror -Wshadow -Wconversion` flags. Final incremental
  build is also green.
- Final clang 18.1.3 ASan/UBSan `make -j4 check`: exit 0 (M)[AC], with
  all 60 test binaries and the replay CLI checks passing. The initial full
  suite also passed; it began before the final helper/CLI test edits, so the
  full suite was rerun after those edits. `ASAN_OPTIONS=detect_leaks=0` is
  required in this sandbox. LeakSanitizer was not run; the coordinator must
  repeat with leak detection enabled.

```text
$ env DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all
make: Nothing to be done for 'all'.
exit=0
$ env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check
check: 60 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
exit=0
```
- IPC fuzzer built and ran clean with the same sanitizer configuration:

```text
$ env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/ipc_fuzz -max_total_time=60 -max_len=4096
Done 2675845 runs in 61 second(s)
exit=0
```

  Fuzz duration and run count above are (M)[AC]; the requested duration is
  60 seconds (G). No crashes or ASan/UBSan findings. No persistent corpus was
  written and no corpus fixtures were changed.
- Final release repetitions, all on `DISPLAY=:99 EDIT_DISPLAY=:99`, pasted
  summary (M)[AC]:

```text
serial summary (M)[AC]: cli_test=10 runs/0 failures; ipc_test=10 runs/0 failures; skips=0
cli concurrent round 1: exits=[0, 0]
cli concurrent round 2: exits=[0, 0]
cli concurrent round 3: exits=[0, 0]
cli concurrent round 4: exits=[0, 0]
cli concurrent round 5: exits=[0, 0]
concurrent summary (M)[AC]: 5 rounds x 2 cli_test processes=10 runs/0 failures; total skips=0
exit=0
```

- Shell launcher syntax and `git diff --check`: exit 0 (M)[AC].
- Power was AC, confirmed by supply online; BAT0 reported `Not charging`.
  Observed load1 snapshots were 1.66, 3.62, and 6.77 (M)[AC]. These are
  correctness checks under current load, not a timing gate judgment. No module
  benchmark was rerun because no typing/hot path changed.

## Decisions, limits, and remaining work

Design decision: `docs/decisions/edit-og2.md`. Display matching is exact;
lexical aliases may select separate primaries. Empty names return
`IPC_INVALID`, and oversized encoded endpoint names return `IPC_LIMIT` before
binding/sending. Updated CLIs do not discover older unsuffixed CLI primaries.
This conservative routing avoids handing off to a mismatched display.

No worker implementation or requested verification is missing. The coordinator's
LeakSanitizer rerun is the remaining external check. Implementation and the
mandatory report were completed before the scope-stop checkpoint.

Out of scope, reported without fixing: `tools/test_runtime_identity.sh` still
expects the obsolete no-argument usage error and the old single-file usage
text. Current CLI parsing accepts an empty file list; executing that script's
real launcher can therefore wait for an editor window instead of returning the
expected usage error. Only its launch isolation was changed. The script was
syntax-checked; it and the desktop installer were not executed.

Build/run logs are under `build/edit-og2-evidence` (untracked build artifacts).
