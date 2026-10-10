# edit-pyo — session 9 worker report

## Scope and outcome

Both named fuzz mismatches are fixed. The one-byte red runs were reproduced
before editing; both green replays and the full-minute fuzz run pass. Release
make all and the final complete sanitizer make check exited 0 (M)[AC]. Earlier
out-of-scope display/test failures and the LeakSanitizer limitation are recorded
below. Updated only the save-controller fuzz model and this bead's documentation.
Read CLAUDE.md, the bead, docs/decisions/edit-mdv.md, edit-yqu.md and
edit-yqu3.md, src/savectl/savectl.h (including the bounded request, prepare
handoff and finish recovery contracts), src/savectl/STATUS.md and the named
P4-modules-2 review findings. Git was used read-only; no bd, HANDOFF.md or
worklog edits. No src/savectl changes, new globals or typing-path allocations.
All commands that could use a display ran with DISPLAY=:99 and EDIT_DISPLAY=:99.

### Reload allocation-failure mismatch (review §15)

Done: the existing one-byte `0a` regression was run red before the model edit.
The model now requires FAILED/FILE_ERR_NOMEM, modified state and a banner,
unchanged replacement/view outputs and the old tree/disk. Clearing the allocator
fault must still produce BUSY from take_reload. It drains backing retirement,
checks reload/keep availability, explicitly reacquires via savectl_reload,
then installs and checks the replacement bytes. Recovery keeps the same tab.

### Journal BASE_CHANGED mismatch (review §10)

Done: the existing one-byte `02` regression was run red before the model edit.
The model now requires SAVECTL_EXTERNAL_MODIFIED, a banner and both recovery
actions, while retaining JOURNAL_BASE_CHANGED. It verifies FILE_OK, no pending
finish, and an unprepared token without a retained previous path. Invalid
checkpoint and journal I/O failure scenarios continue requiring SAVECTL_FAILED.

### Schedule preservation (review §37)

Every existing edit-yqu schedule remains: journal prepare/invalid checkpoint,
BASE conflict, partial drains, prepare ownership handoff, paused file writes,
retained-generation validation, finish failure and fresh-inode recovery;
source validation faults, absent/nonregular targets, installation allocation
faults and unavailable-parent saves; actual mapped source truncation/sticky
faults after UI owner retirement, multi-chunk rewrites with restored mtime,
reused work slots/cancellation/logical close; and the byte/disk/view oracle.
No fuzz expectation was broadened to hide a new production failure.

## Red runs (pasted excerpts)

The bead's cited line numbers shifted in the landed tree: the actual red
assertions below are at lines 256 and 151. The deterministic one-byte input fixtures were written before editing and
replayed through existing fuzz operations as the failing tests; both ran before
the fix, so no duplicate unit regression was needed. Timing values in tool output are (M)[AC]; they are evidence of execution,
not performance-gate verdicts. Power is AC per the task; the status read returned
"Not charging". The box was loaded, and no benchmark comparison or timing gate
claim was made.

Input `0a` (existing fuzz regression, before any source edit), exit 77 (M)[AC]:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/savectl_fuzz build/edit-pyo-evidence/0a
build/fuzz/savectl_fuzz: Running 1 inputs 1 time(s) each.
Running: build/edit-pyo-evidence/0a
fuzz/savectl_fuzz.c:256: assertion failed: savectl_take_reload(s, &replacement, &view) == 0
==3== ERROR: libFuzzer: deadly signal
SUMMARY: libFuzzer: deadly signal
```

Input `02` (existing fuzz regression, before any source edit), exit 77 (M)[AC]:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/savectl_fuzz build/edit-pyo-evidence/02
build/fuzz/savectl_fuzz: Running 1 inputs 1 time(s) each.
Running: build/edit-pyo-evidence/02
fuzz/savectl_fuzz.c:151: assertion failed: model.state == SAVECTL_FAILED
==3== ERROR: libFuzzer: deadly signal
SUMMARY: libFuzzer: deadly signal
```

## Green one-byte replays (pasted output)

Input `0a`, exit 0 (M)[AC]:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/savectl_fuzz build/edit-pyo-evidence/0a
build/fuzz/savectl_fuzz: Running 1 inputs 1 time(s) each.
Running: build/edit-pyo-evidence/0a
Executed build/edit-pyo-evidence/0a in 32 ms
***
*** NOTE: fuzzing was not performed, you have only
***       executed the target code on a fixed set of inputs.
***
```

Input `02`, exit 0 (M)[AC]:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/savectl_fuzz build/edit-pyo-evidence/02
build/fuzz/savectl_fuzz: Running 1 inputs 1 time(s) each.
Running: build/edit-pyo-evidence/02
Executed build/edit-pyo-evidence/02 in 43 ms
***
*** NOTE: fuzzing was not performed, you have only
***       executed the target code on a fixed set of inputs.
***
```

## Build and full-minute validation

Release `DISPLAY=:99 EDIT_DISPLAY=:99 make all` exited 0 (M)[AC]:

```text
make: Nothing to be done for 'all'.
```

Compiler versions verified: gcc 13.3.0 release and clang 18.1.3 sanitizers.
The changed fuzzer was rebuilt with C11, -Wall -Wextra -Werror -Wshadow
-Wconversion and ASan/UBSan/libFuzzer, with no diagnostics.

The requested full-minute fuzz run exited 0 (M)[AC], completing 1921
executions in 61 seconds (M)[AC], with no assertion or sanitizer failure:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/savectl_fuzz -max_total_time=60 -max_len=64
INFO: Seed: 1763792180
#1921	DONE   cov: 6578 ft: 18627 corp: 303/874b lim: 4 exec/s: 31 rss: 296Mb
Done 1921 runs in 61 second(s)
```

The first sandboxed make check compiled the sanitizer suite successfully but
exited 2 (M)[AC] when cli_test could not connect to :99:

```text
== build/san/tests/cli_test
cli_test:61: FAIL c && !xcb_connection_has_error(c)
cli_test:137: FAIL ran == 0
make: *** [Makefile:101: check] Error 1
```

A direct Unix-socket connection probe returned EPERM (Operation not permitted).
The sandbox blocks access to the existing Xvfb socket. An automatically approved
rerun outside the sandbox used the same DISPLAY=:99, EDIT_DISPLAY=:99 and
ASAN_OPTIONS=detect_leaks=0. It passed through raster_test, then exited 2
(M)[AC] in the unchanged refwin_test:

```text
refwin_test: FAIL headless CSV completeness/monotonicity
make: *** [Makefile:101: check] Error 1
```

The immediate standalone refwin replay exited 0 (M)[AC]:

```text
refwin_test: PASS 12 editor/reference pairs, complete monotonic CSV, incomplete/wrong-frame runs rejected (Xvfb phase approximate)
```

This is an intermittent out-of-scope display/test failure; its cause has not
been established. No display environment, test or production source was changed.
The complete-suite retry exited 2 (M)[AC] earlier in cli_test:

```text
cli_test: editor exited or stopped before window (status=0)
cli_test:77: FAIL waited == 0
cli_test:137: FAIL ran == 0
make: *** [Makefile:101: check] Error 1
```

The final complete-suite attempt exited 0 (M)[AC]. All 60 sanitizer binaries
(M)[AC] and the replay CLI shell checks passed. Earlier intermittent failures
remain out of this bead's source scope and were neither repaired nor suppressed.

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
check: 60 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

Both module sanitizer binaries were also run directly, exiting 0 (M)[AC]:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test
savectl every reload construction allocation: rollback/retry: ok
savectl journal BASE_CHANGED exposes external recovery: ok
savectl journal BASE_CHANGED exposes external recovery: ok
savectl_test: ok
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_reload_test
restored-mtime reload: install=-1 file_error=5 first=o last=s modified=1
savectl restored-mtime check/keep identity: ok
savectl_reload_test: ok
```

## Decisions, limitations and missing work

The model uses explicit reacquisition rather than reviving a failed installation,
and classifies BASE_CHANGED as external conflict with preserved diagnostics.
Recorded in docs/decisions/edit-pyo.md; these follow existing landed contracts.
No genuine new save-controller module defect was observed; tests/savectl_test.c is
unchanged. The pre-existing opt-in slot-reuse regression is outside this bead.

LeakSanitizer cannot run inside this sandbox: all sanitizer runs use
ASAN_OPTIONS=detect_leaks=0. The coordinator must rerun with leaks enabled.
No in-scope work or requested validation remains missing apart from the
coordinator's LeakSanitizer rerun. No corpus fixture was needed for this
model-only change. No benchmark was rerun because no production hot path changed.
The complete suite also exercised the ordinary savectl allocation and journal
conflict regressions; no opt-in known-failure regression was added because the
updated model exposed no new savectl defect.
