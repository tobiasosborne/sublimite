# edit-dr9 worker report — s8

Implemented scope: preserve possibly newer rotation checkpoints, admit shared directory locks without waiting, and retry BUSY close checkpoints without `fail()`. Design: [edit-dr9](../decisions/edit-dr9.md).

Status: implementation and final release build complete; final non-display sanitizer verification passed. Final display verification is incomplete because the shared display lock remained unavailable. Coordinator rerun commands are below.

## Files touched

- `src/journal/journal.c`, `src/journal/journal.h`, `src/journal/retire.c`: explicit clean/failed basename flag; conservative older-name handling; nonblocking flock with EINTR retry and BUSY return contract.
- `src/editor/editor.c`, `src/editor/private.h`: retain a busy close and retry through the existing journal tick/poll deadline. On this tree, `editor.c` calls `editor_checkpoint` from flush fallback; all editor-step close exits now share that continuation.
- `tests/journal_test.c`: held exclusive guard, bounded BUSY response, unchanged original/stats, another accepted edit, and successful later rotation.
- `tests/journal_retire_test.c`: kill at the rename seam after append failure or independent worker writeback loss; preserve and replay the checkpoint under a pinned set-aside. Includes an older unflagged failed checkpoint and the existing clean/missing-original outcomes.
- `tests/editor_checkpoint_test.c`: BUSY close retains text and the open journal, then completes after guard release.
- `fuzz/journal_fuzz.c`: failed and older-unflagged matching-original retirement oracles.
- `docs/decisions/edit-dr9.md`, this report. Generated objects, binaries, seeds and verification logs are under `build/`, with bead logs under `build/edit-dr9/`.

Numeric derivations: (P) source positions, enum case ordinals, exit statuses, pipe endpoints, empty/single states and schema/literal widths are structural values. (E) the lock watchdog and close retry reuse `JOURNAL_DEFAULT_SYNC_NS`; the editor test watchdog reuses `EDITOR_IDLE_NS`. No new budget or retry count was introduced. Captured timing/load/disk/counter output is (M)[AC], with literal BAT0 status and uptime stamps. Fuzz selectors cover every value of an octet, (P) two raised to its eight bits; the extra byte repeats that selector for malformed names. Existing fixture counts and sizes retain their source derivations.

## Red runs

The source implementation was unchanged for the first retirement and blocking-lock failures. The editor red run used the nonblocking journal fix with the old close error handling. The compatibility red run removed the state flag from an actual killed failed checkpoint to reproduce the older build's spelling.

### Failed original loses its checkpoint

```text
Not charging
 08:49:51 up 6 days, 13:37,  1 user,  load average: 70.80, 62.13, 50.78
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_retire_test
rotation_temps: case=2 set-asides=0 expected=1
rotation_temps: case=3 set-asides=0 expected=1
journal_retire_test:545 FAIL rotations==0
journal_retire_test: rotation case=0 swept
journal_retire_test: rotation case=1 set aside and pinned
exit=1
Not charging
 08:49:52 up 6 days, 13:37,  1 user,  load average: 70.80, 62.13, 50.78
```

### Held directory guard blocks rotation

```text
Not charging
 08:50:09 up 6 days, 13:38,  1 user,  load average: 69.31, 62.22, 50.99
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_test --rotate-busy
journal_test: rotation waited for the held directory guard beyond JOURNAL_DEFAULT_SYNC_NS (E watchdog)
journal_test: full VIEW roundtrip/rotation, legacy defaults, invalid lengths/flags passed
exit=1
Not charging
 08:50:10 up 6 days, 13:38,  1 user,  load average: 69.31, 62.22, 50.99
```

### BUSY close reaches the editor error path

```text
Not charging
 08:52:49 up 6 days, 13:40,  1 user,  load average: 68.40, 64.49, 53.61
$ ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_checkpoint_test busy
waiting for display :99 (held by 2055644)
editor_checkpoint_test:70 FAIL rc==EDITOR_MORE || rc==EDITOR_OK
editor_checkpoint_test: 1 case(s) failed
exit=1
Not charging
 08:53:16 up 6 days, 13:41,  1 user,  load average: 66.66, 64.46, 53.95
```

### Older unflagged failed checkpoint is also lost

```text
Not charging
 09:14:58 up 6 days, 14:03,  1 user,  load average: 41.54, 38.56, 45.31
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_retire_test
rotation_temps: case=4 set-asides=0 expected=1
journal_retire_test:556 FAIL rotations==0
journal_retire_test: rotation case=0 swept
journal_retire_test: rotation case=1 set aside and pinned
journal_retire_test: rotation case=2 set aside and pinned
journal_retire_test: rotation case=3 set aside and pinned
exit=1
Not charging
 09:14:58 up 6 days, 14:03,  1 user,  load average: 41.54, 38.56, 45.31
```

An initial sandbox attempt aborted LeakSanitizer under ptrace; untraced reruns kept leak detection enabled. An initial test-wiring rebuild failed because the optional selector was placed in the wrong helper; its placement was corrected before the behavioral editor red run.

## Green runs

Final journal suite excerpt (full output and stamps in `build/edit-dr9/final-journal_test.log`):

```text
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_test
journal_test: directory guard contention returns BUSY; open journal accepts edits and rotation retries after release
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
exit=0
```

Final retirement suite, including the older unflagged name:

```text
Not charging
 09:17:20 up 6 days, 14:05,  1 user,  load average: 46.94, 42.37, 45.82
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_retire_test
journal_retire_test: rotation case=0 swept
journal_retire_test: rotation case=1 set aside and pinned
journal_retire_test: rotation case=2 set aside and pinned
journal_retire_test: rotation case=3 set aside and pinned
journal_retire_test: rotation case=4 set aside and pinned
(M)[AC] BAT0=Not charging empty_fixture_count=32 journal_bytes=4096 base_bytes=4 discovery_before_ns=1070933 discovery_after_ns=57734 load1=46.71
journal_retire_test: passed
exit=0
Not charging
 09:17:22 up 6 days, 14:05,  1 user,  load average: 46.71, 42.40, 45.81
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
```

The editor BUSY-close regression was green before the later filename-compatibility adjustment; the editor implementation was unchanged by that adjustment. This earlier complete checkpoint-suite run is preserved below. The final rerun was blocked before binary execution.

```text
Not charging
 09:02:42 up 6 days, 13:50,  1 user,  load average: 56.77, 59.47, 56.82
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  380G   76G  84% /
$ ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_checkpoint_test
waiting for display :99 (held by 2061509)
editor_checkpoint_test: directory inventory defers close checkpoint; current text and open journal survive until retry
editor_checkpoint_test: inactive pending COPY recovered=1048590 expected=1048590 clean=yes disk_intact=yes
editor_checkpoint_test: active pending COPY recovered=1048590 expected=1048590 clean=yes disk_intact=yes
editor_checkpoint_test: adopted deletion to empty survives checkpoint/crash/recovery
editor_checkpoint_test: (a) 1 GiB unchanged checkpoint 1: journal=4096 B bound=4096 B (P) time=18.076 ms (M) allocations=uncounted:0 power=Not load1=26.14
editor_checkpoint_test: (a) 1 GiB unchanged checkpoint 2: journal=4096 B growth=0 B time=23.666 ms (M) allocations=uncounted:0 power=Not load1=26.14
editor_checkpoint_test: (b) 1 GiB + 10 B checkpoint: journal=4096 B bound=4096 B (P) pieces=16385 deletes=0 inserts=1 inserted=10 B time=40.391 ms (M) allocations=uncounted:0 power=Not load1=26.14
editor_checkpoint_test: (b) replay onto base: edit at middle, base bytes at head/quarter/after edit/tail
editor_checkpoint_test: (a)+(b) wall=2267.039 ms (M) power=Not load1=26.14
editor_checkpoint_test: (b2) 2 MiB edited checkpoint: journal=4096 B deletes=1 deleted=3 inserts=2 inserted=4 B
editor_checkpoint_test: (b2) referencing checkpoint of an edited COPY file recovers the edited text
editor_checkpoint_test: f3i (c) recovered 2 MiB buffer, install checkpoint: journal before=4096 B after=4292 B bound=4292 B (P) deletes=1 deleted=3 inserts=2 inserted=4 B
editor_checkpoint_test: (c) changed BASE refuses the referencing journal; disk text opens, journal set aside
editor_checkpoint_test: (d) legacy whole-text checkpoint journal still recovers
editor_checkpoint_test: (e) saved+unchanged checkpoint: journal=4096 B deletes=0 inserts=0
editor_checkpoint_test: (e) saved revision checkpoints as BASE only and recovers the saved text clean
editor_checkpoint_test: f3i (a) 1 GiB, save, +1 B checkpoint: journal=4096 B bound=4096 B (P) pieces=16385 deletes=0 deleted=0 inserts=1 inserted=1 B save=3980.000 ms checkpoint=22.905 ms (M) allocations=uncounted:0 power=Not load1=27.57
editor_checkpoint_test: f3i (a) replay onto saved file: xy at middle, head/tail intact
editor_checkpoint_test: f3i (b) step 1: journal=4096 B bound=4096 B (P) bases=1 deletes=0 deleted=0 inserts=2 inserted=4 B
editor_checkpoint_test: f3i (b) step 2: journal=4096 B bound=4096 B (P) bases=1 deletes=0 deleted=0 inserts=1 inserted=2 B
editor_checkpoint_test: f3i (b) step 3: journal=4096 B bound=4096 B (P) bases=1 deletes=0 deleted=0 inserts=2 inserted=4 B
editor_checkpoint_test: f3i (b) save in flight: pre-save BASE while held, saved file after; recovered at each step
editor_checkpoint_test: pending COPY checkpoint/crash/recovery/save, edit-28u referencing checkpoints, edit-f3i post-save checkpoints passed
exit=0
Not charging
 09:09:47 up 6 days, 13:57,  1 user,  load average: 26.08, 38.67, 48.49
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  380G   76G  84% /
```

Final release target verification:

```text
$ make -j3 all
make: Nothing to be done for 'all'.
exit=0
gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
Ubuntu clang version 18.1.3 (1ubuntu1)
```

## Verification commands and exit codes

The final release/sanitizer build command was:

```sh
make -j3 all build/san/tests/journal_test build/san/tests/journal_retire_test build/san/tests/journal_kill_test build/san/tests/dataloss_test build/san/tests/editor_checkpoint_test build/san/tests/editor_session_test build/fuzz/fuzz/journal_fuzz.o
```

It exited successfully; compilation retained the Makefile's C11 warning/error flags, gcc release configuration, and clang address/undefined sanitizer configuration. The Makefile and build flags were unchanged. Overlapping earlier release/sanitizer builds split the user's job ceiling; no more than that ceiling was built concurrently.

| Command | Exit | Evidence |
| --- | --- | --- |
| Initial sanitizer test build | 0 | `red-build.log` |
| Initial test-selector wiring rebuild | 2 | `red-rebuild.log`; corrected before behavioral red |
| Corrected editor test build | 0 | `red-editor-build.log` |
| Journal-fix sanitizer build | 0 | `core-build.log` |
| Earlier complete sanitizer build | 0 | `sanitizer-build.log` |
| Release parser prebuild / release prebuild | 0 / 0 | `release-parser-build.log`, `release-build.log` |
| Older-name regression build | 0 | `compat-red-build.log` |
| Final release/sanitizer build command above | 0 | `final-compat-build.log` |
| `make -j3 all` | 0 | `all-final.log` |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_test` | 0 | `final-journal_test.log` |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_retire_test` | 0 | `final-journal_retire_test.log` |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_kill_test` | 0 | `final-journal_kill_test.log` |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/dataloss_test` | 0 | `final-dataloss_test.log` |
| Final `dataloss_test` first attempt | 1 | `final-dataloss-first-attempt.log`; known `edit-1wc`, authorized single rerun passed |
| `ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_checkpoint_test` | 1 — lock timeout; not run | `final-editor_checkpoint_test.log` |
| `ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_session_test` | 143 — queued wrapper cancelled; not run | `final-editor_session_test.log` |
| `EDITOR_ALLOC_ONLY=headless XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test` | 1 — lock timeout; not run | `final-alloc-headless.log` |
| `EDITOR_ALLOC_ONLY=raster XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test` | 1 — lock timeout; not run | `release-alloc-raster.log` |
| Link final journal fuzz object with clang fuzzer/address/undefined runtimes and the sanitizer archive | 0 | `fuzz-link-final.log` |
| `ASAN_OPTIONS=detect_leaks=1 build/edit-dr9/journal_fuzz -runs=0 build/edit-dr9/retirement-seeds` | 0 | `final-fuzz-seeds.log`; supplied corpus initialization only |
| `build/bench/journal_bench --track` | 0 | `final-journal-bench.log`; correctness passed, tracked timing misses retained |

Log filenames in the table are relative to `build/edit-dr9/`. All sanitizer executions enabled leaks outside ptrace. Display suites use private runtime directories removed after the wrapper returns.

## Remaining and outside scope

The final display checks did not execute: checkpoint and malloc-guard wrappers exhausted the existing display-lock wait; the earlier session wrapper also exhausted it. After those failures, only this worktree's idle final session wrapper was cancelled. It was verified to be Python, have no children, and have this worktree as its cwd before signalling it through a pidfd. No display owner or running test binary was signalled. (P) Its shell exit status is 143 = 128 + SIGTERM (15), the host shell's signal-exit encoding. All worker verification processes have returned.

The coordinator still needs to run these commands when the shared display lock is available:

```sh
ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_checkpoint_test
ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_session_test
EDITOR_ALLOC_ONLY=headless XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test
EDITOR_ALLOC_ONLY=raster XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test
```

Final display-wrapper evidence, including disk snapshots for the requested display suites:

`final-editor_checkpoint_test.log`:

```text
Not charging
 09:19:37 up 6 days, 14:07,  1 user,  load average: 35.93, 39.62, 44.31
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   73G  85% /
$ ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_checkpoint_test
waiting for display :99 (held by 2104116)
editor_checkpoint_test: display lock timeout: display :99 (held by 2118134)
exit=1
Not charging
 09:29:37 up 6 days, 14:17,  1 user,  load average: 64.76, 60.66, 52.45
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
```

`final-editor_session_test.log`:

```text
Not charging
 09:29:37 up 6 days, 14:17,  1 user,  load average: 64.76, 60.66, 52.45
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
$ ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_session_test
waiting for display :99 (held by 2118134)
Terminated
exit=143
Not charging
 09:31:37 up 6 days, 14:19,  1 user,  load average: 71.58, 64.62, 54.90
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
```

`final-alloc-headless.log`:

```text
Not charging
 09:20:25 up 6 days, 14:08,  1 user,  load average: 34.10, 38.68, 43.77
$ EDITOR_ALLOC_ONLY=headless XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test
waiting for display :99 (held by 2104116)
editor_test: display lock timeout: display :99 (held by 2118134)
exit=1
Not charging
 09:30:25 up 6 days, 14:18,  1 user,  load average: 69.16, 62.36, 53.41
```

`release-alloc-raster.log`:

```text
Not charging
 09:19:20 up 6 days, 14:07,  1 user,  load average: 36.28, 39.93, 44.51
$ EDITOR_ALLOC_ONLY=raster XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test
waiting for display :99 (held by 2104116)
editor_test: display lock timeout: display :99 (held by 2118134)
exit=1
Not charging
 09:29:20 up 6 days, 14:17,  1 user,  load average: 64.33, 60.30, 52.16
```

The final data-loss run reproduced the brief's known `edit-1wc` two-process timing race and passed the authorized single rerun. Its first-attempt failure is retained in `final-dataloss-first-attempt.log`; no change was made for it. The journal benchmark passed correctness and recorded tracking misses under its stamped load; no performance budgets were changed.

`edit-07g`, `edit-601` (including `restore.c`), and `edit-4da` remain untouched. There is no flock hook in `journal_io`; production retries EINTR without adding a syscall seam solely for this test. No follow-up review or unrelated fix was performed. No `git` or `bd` command was run; protected handoff/plan/worklog files were not edited.

No GB fixture was added for this bead. Existing GB source inputs use sparse `ftruncate` fixtures; the existing session test intentionally produces a dense legacy whole-text journal as program output. That fixture code was not changed. The earlier successful checkpoint-suite run above includes its before/after disk snapshots. Final display attempts never reached fixture creation. After verification, the small private fixture left by the known data-loss race was removed. The completed-test fixture scan found no files under the private editor/dataloss/retirement-fuzz roots.

Cleanup evidence:

```text
Not charging
 09:32:26 up 6 days, 14:20,  1 user,  load average: 70.22, 65.26, 55.58
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
Removing completed-test fixture: build/dataloss-zEXt1a
Completed-test fixture cleanup finished.
exit=0
Not charging
 09:32:27 up 6 days, 14:20,  1 user,  load average: 70.21, 65.33, 55.65
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
```

Fixture cleanup and the post-cleanup scan both exited successfully. Verification logs, seeds and build outputs remain under `build/` for the coordinator.
