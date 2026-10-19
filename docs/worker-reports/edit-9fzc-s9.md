# edit-9fzc worker report (s9)

Worktree: `/home/tobias/Projects/sublimite/.wt/edit-9fzc-s9`.
Decision: [edit-9fzc](../decisions/edit-9fzc.md).

## Changes and files touched

- `src/editor/buffers.c`: Save As peer replacement defers its checkpoint to
  the existing publish job; explicit Reload and automatic refresh keep their
  checkpoint/rollback behavior.
- `src/editor/commands.c`, `src/editor/private.h`: name the Save As-specific
  reload helper and describe joint peer/owner publication.
- `src/editor/retain.c`: return under every save journal lease, after polling
  publish completion; no retention submission or state transition during a lease.
- `tests/dataloss_test.c`: peer checkpoint thread ownership and real Ctrl+S
  prepare-lease regressions, including bytes, tab identity, modified state and
  retention resumption; both run by default and by the selectors below.
- `docs/decisions/edit-9fzc.md`, this report.

No typing-path allocation was added. Errors keep the existing return-code
handling. No `bd` or `git` commands were run. Repository edits were confined
to this worktree.

## Red run

The regression tests were written and run before source changes.
`make -j3 build/tests/dataloss_test` built with gcc and the required warnings,
exit `0` (P: command success). Each test's exit `1` is its failed assertion
(P: the suite's failure return). Rotation counts are actual rename callback
events (M)[AC]; flags are boolean state (P). Fixture lengths derive from the
literal text and tab indexes from insertion order. No latency threshold was
introduced; waits reuse `EDITOR_IDLE_NS` (E: existing watchdog).

```text
$ cat /sys/class/power_supply/BAT0/status
Charging
$ uptime
 03:24:26 up  1:49,  1 user,  load average: 26.11, 21.74, 19.98
$ DISPLAY=:99 build/tests/dataloss_test --9fzc-peer
dataloss_test:1238: FAIL atomic_load(&io->ui_rotations) == 0 && atomic_load(&io->worker_rotations) > 0
dataloss_test: edit-9fzc Save As peer: UI rotations=1 worker rotations=1 peer clean=1
--9fzc-peer exit=1
$ cat /sys/class/power_supply/BAT0/status
Charging
$ uptime
 03:24:27 up  1:49,  1 user,  load average: 26.11, 21.74, 19.98
$ DISPLAY=:99 build/tests/dataloss_test --9fzc-lease
dataloss_test:1269: FAIL idle
dataloss_test: edit-9fzc Ctrl+S prepare: leased=1 retention idle=0
--9fzc-lease exit=1
```

Clock, uptime and load values are captured system readings (M)[AC]; display
and timeout values below come from the worker brief (E: orchestration settings).
Line numbers identify the actual assertion locations (P: source line ordering).

## Green run

The peer fix was implemented and tested first, then the retention gate.
Each `make -j3 build/tests/dataloss_test` and test below exited `0`
(P: successful build/assertions). The intermediate peer build initially
exited `2` (P: make failure) because an internal reload call needed its new
policy argument; that call was corrected before the successful build.

```text
$ cat /sys/class/power_supply/BAT0/status
Charging
$ uptime
 03:25:29 up  1:50,  1 user,  load average: 30.10, 23.54, 20.71
$ DISPLAY=:99 build/tests/dataloss_test --9fzc-peer
dataloss_test: edit-9fzc Save As peer: UI rotations=0 worker rotations=1 peer clean=1
exit=0
$ cat /sys/class/power_supply/BAT0/status
Charging
$ uptime
 03:25:45 up  1:50,  1 user,  load average: 29.85, 23.81, 20.85
$ DISPLAY=:99 build/tests/dataloss_test --9fzc-lease
dataloss_test: edit-9fzc Ctrl+S prepare: leased=1 retention idle=1
dataloss_test: edit-9fzc retention resumed after the save lease
test exit=0
```

After both source changes and the final release build, both selectors were
rerun against the final release binary: exit `0` for each (P: successful
assertions), with the same rotation and lease observations (M)[AC].

## Verification

Build commands (exit `0`, P: success), using gcc and clang as configured by
the existing Makefile, with the required C warning flags:

```sh
make -j3 all
make -j3 build/san/tests/editor_test build/san/tests/editor_session_test build/san/tests/editor_checkpoint_test build/san/tests/dataloss_test build/san/tests/journal_test build/san/tests/savectl_test
```

Build logs: `build/9fzc/make-all.log`, `build/9fzc/san-build.log`.
Toolchain version strings (M)[AC]: gcc `13.3.0`, clang `18.1.3`.
The suite runs use this wrapper, with a fresh runtime directory and cleanup
on shell exit, and power/uptime/disk stamps before and after each command:

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh <binary> [selector]
```

The default sanitizer `editor_test` was attempted under that wrapper, then
interrupted after native-display setup failures: exit `130` (P: shell
interruption, `128 + SIGINT`, whose Linux signal value is `2`). A direct local
socket probe established the sandbox limitation:

```text
DISPLAY :99 socket unreachable: [Errno 1] Operation not permitted
```

Here errno `1` means `EPERM` (P: Linux ABI). The coordinator must rerun the
full native suite on the requested display with socket access, as the brief
directs. The existing `--headless` selector was then run; no test was edited
to bypass display requirements.

Every sanitizer run below exited `1` (P: failure), with LeakSanitizer's fatal
ptrace diagnostic. No assertion failure, AddressSanitizer error or UBSan
runtime-error diagnostic was observed in these logs before that failure.
These are **not passing sanitizer results**; leak verification requires the
coordinator's rerun in a runtime where LeakSanitizer can finish.

| Command under the wrapper above | Exit | Result |
| --- | --- | --- |
| `build/san/tests/editor_test --headless` | `1` | LeakSanitizer fatal error |
| `build/san/tests/editor_session_test` | `1` | LeakSanitizer fatal error |
| `build/san/tests/editor_checkpoint_test` | `1` | LeakSanitizer fatal error |
| `build/san/tests/dataloss_test` | `1` | LeakSanitizer fatal error |
| `build/san/tests/journal_test` | `1` | LeakSanitizer fatal error |
| `build/san/tests/savectl_test` | `1` | LeakSanitizer fatal error |
| `EDITOR_ALLOC_ONLY=null build/tests/editor_test` | `0` | Release malloc guard passed |

For the last row, `EDITOR_ALLOC_ONLY=null` was exported to the wrapper and
inherited by the binary. Logs are `build/9fzc/san-<suite>.log`, with
`san-editor_test-headless.log` for the alternate editor run and
`release-editor_test-alloc.log` for the release guard. Exact repeated failure:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

Release guard evidence (M)[AC]; key count is the existing allocator-test
workload, allocation count is the allocator hook's observed result:

```text
Full
 03:48:54 up  2:14,  1 user,  load average: 26.55, 29.09, 26.70
$ EDITOR_ALLOC_ONLY=null ... build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
exit=0
Full
 03:48:55 up  2:14,  1 user,  load average: 27.55, 29.25, 26.77
```

The module benchmark was also attempted under the same wrapper:

```sh
build/bench/editor_bench --track --no-idle --no-p4 --serial-only --keys=64 --file=build/9fzc/bench-fixture
```

The flags and key count reuse `bench/editor_bench.args` (E: existing tracked
workload policy); the temporary fixture was an exact copy of `retain.c`
(P: byte length derives from that file), removed after the command. Exit `1`:
the null row completed with `structural=OK` and no allocations, then native
backend setup failed at `editor_open` under the denied display socket.
`build/9fzc/editor-bench.log` contains the tracked measurements, power and
uptime stamps. No performance gate or budget was added, and no comparative
performance claim is made.

Disk evidence before suites and after all runs (M)[AC], pasted `df -h /`.
The added regressions use small literal fixtures; existing large-suite
fixtures use their existing sparse setup and cleanup. The post-run worktree
scan found no GB-scale fixture remaining. Temporary baseline copies and the
small fixture from the interrupted launcher case were removed.

```text
$ cat /sys/class/power_supply/BAT0/status
Charging
$ uptime
 03:35:49 up  2:01,  1 user,  load average: 16.69, 22.36, 22.92
$ df -h /
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  395G   61G  87% /

$ cat /sys/class/power_supply/BAT0/status
Full
$ uptime
 03:50:13 up  2:15,  1 user,  load average: 27.30, 28.74, 26.78
$ df -h /
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  393G   63G  87% /
```

## Remaining and outside scope

The exit path intentionally stays synchronous, as the bead allows:
`editor_flush` can write an owed Save As checkpoint on the UI thread after
settling saves and flushing records. Storage stalls can delay shutdown.
The decision document records this choice.

Implementation is complete within the bead. Required sanitizer passes and
native-display coverage remain unverified because of the runtime limitations
above. The coordinator needs to rerun the required sanitizer binaries with
leaks on, full `editor_test` on the requested display, and the native benchmark
rows. The release build, red/green regressions and release malloc guard passed.

No unrelated code was changed. The restricted default `editor_test` attempt
also reported the following existing-case failures (P: source line locations;
`4` is the existing `IPC_IO` enum value):

```text
reload launcher result=4
editor_test:2530: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0
editor_test:1768: FAIL editor_open(&e, &cfg, &b) == 0
editor_test:1794: FAIL editor_open(&e, &cfg, &b) == 0
```

The native opens require the denied display socket. The launcher failure's
cause was not established here; it needs the coordinator's unrestricted rerun.
No fixes or extra review pass were made for these outside-scope observations.
The small fixture left by that failed launcher case was removed; no GB-scale
fixture remained in this worktree after interruption.
