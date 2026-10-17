# edit-y5x worker report (s8)

## Change and files touched

- `src/editor/open.c`: when the failed startup hands off to edit-btj's retry,
  clear `session_lock_owned` before its inner `editor_close`. The failed
  attempt closes its descriptors but preserves the empty launch journal and
  the named lease inode. The retry takes ownership of final-close cleanup.
- `tests/editor_session_test.c`: add `leased_install_failure`, exercising an
  actual install error with and without a lease. Check an empty tab and the
  main-style leased `start_empty` case, the exact set-aside notice, preserved
  source identity/size/mtime and marker, unchanged pre-existing profile
  entries, lock identity, and close consistency. The main-style case also
  checks that the original empty launch journal inode survives the retry.
  Keep the existing non-leased `install_failure` test and run it in the full
  suite.
- `docs/decisions/edit-y5x.md`: cleanup handoff and fixture choice.
- `docs/worker-reports/edit-y5x-s8.md`: this report.
- Generated evidence in `build/y5x-evidence/`: build/run logs, stamped run
  wrapper and the before-fix copy of `open.c`.

The existing `install_failure` now tests edit-rzj capacity deferral, rather
than reaching an install error. The added fixture writes an unsaved missing
TARGET and then makes that TARGET a directory: the empty BASE still replays,
and the file module rejects the TARGET during install. No allocation fault
hook, capacity budget or large fixture was added.

Numeric evidence below is (M)[AC], obtained from the named commands. Return
code meanings are (P), the editor API and shell exit conventions:
`EDITOR_ERR_IO = -103`, success is `0`, and the failing assertion exits `1`.
Fixture counts and sizes are (P), derived from the marker, named entries and
empty/single-buffer structure, as commented in the test. Build parallelism
and display selection are the supplied brief's constraints. The toolchains
reported `13.3.0` from `gcc -dumpfullversion` and Ubuntu clang `18.1.3` from
`clang --version`; project code built with the Makefile's C11 warning flags.

## Red, before the production change

Build: `make -j3 build/san/tests/editor_session_test`, exit `0`.
Run with a fresh `test_runtime=$(mktemp -d)`:

```sh
XDG_RUNTIME_DIR="$test_runtime" DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 \
  sh tools/with_display_lock.sh build/san/tests/editor_session_test
```

Exit `1`. Exact output (`build/y5x-evidence/red-session.log`):

```text
Not charging
 08:50:31 up 6 days, 13:38,  1 user,  load average: 69.17, 62.71, 51.45
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
edit-y5x: leased=no start_empty=no editor_open=0
edit-y5x: leased=yes start_empty=no editor_open=-103
editor_session_test:1351: FAIL rc == 0 && launch->e
editor_session_test:1410: FAIL leased_install_failure(true, false) == 0
edit-y5x: empty buffer, set-aside notice, profile preservation and close consistency passed
editor_session_test red exit=1
Not charging
 08:50:33 up 6 days, 13:38,  1 user,  load average: 69.17, 62.71, 51.45
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
```

An earlier sandboxed attempt reached the same leased assertion, then
LeakSanitizer reported its ptrace restriction; buffered stdout was lost.
The diagnostic was moved to stderr, and the red run above was repeated
outside the sandbox with leak detection still enabled. That sandbox attempt
is retained in `build/y5x-evidence/red-sandbox-session.log`.

## Green, after the production change

Built the requested sanitizer binaries with:

```sh
make -j3 build/san/tests/editor_session_test build/san/tests/editor_hot_exit_test build/san/tests/journal_test
```

Exit `0`; log `build/y5x-evidence/green-san-build.log`. The session suite used
the same display-locked command as red, with a fresh runtime directory and
leak detection enabled, and exited `0`. Excerpts from the complete
`build/y5x-evidence/green-session.log` (the omitted legacy suite output is
available in that log):

```text
Not charging
1
 08:51:29 up 6 days, 13:39,  1 user,  load average: 66.04, 62.82, 52.13
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
edit-y5x: leased=no start_empty=no editor_open=0
edit-y5x: leased=yes start_empty=no editor_open=0
edit-y5x: leased=yes start_empty=yes editor_open=0
edit-y5x: empty buffer, set-aside notice, profile preservation and close consistency passed
edit-y5x: empty buffer, set-aside notice, profile preservation and close consistency passed
edit-y5x: empty buffer, set-aside notice, profile preservation and close consistency passed
editor_session_test: headless save/repeated/new-target, recoverable failure retry, cutoff edits, shared targets, inactive failure, evicted saving tab, checkpoint during evicted save and crash after it, interrupted-save startup, GB-scale orphan launch, Saving acknowledgement, allocation guard passed
build/san/tests/editor_session_test exit=0
fixture cleanup: build/editor-session-hYLCVR
Not charging
1
 08:52:26 up 6 days, 13:40,  1 user,  load average: 69.66, 64.40, 53.35
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  376G   81G  83% /
```

The standalone `1` lines are `cat /sys/class/power_supply/AC/online`: external
power was connected. Each run captures `cat /sys/class/power_supply/BAT0/status`,
`uptime` and `df -h /` before and after the suite. No new timing limit or
performance claim is introduced.

## Verification commands completed

| Command | Exit | Evidence |
|---|---|---|
| `make -j3 build/san/tests/editor_session_test` before source fix | `0` | `build/y5x-evidence/red-build.log` |
| Initial sandboxed red session run | `1` (assertion and LeakSanitizer ptrace restriction) | `build/y5x-evidence/red-sandbox-session.log` |
| Red sanitizer session run shown above | `1` (expected) | `build/y5x-evidence/red-session.log` |
| Sanitizer target build shown above | `0` | `build/y5x-evidence/green-san-build.log` |
| `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_session_test` | `0` | `build/y5x-evidence/green-session.log` |
| `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_hot_exit_test` | `0` | `build/y5x-evidence/green-hot-exit.log` |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_test` | `0` | `build/y5x-evidence/green-journal.log` |
| `make -j3 all` (GCC release, project warnings as errors) | `0` | `build/y5x-evidence/green-release-build.log` |
| `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/tests/editor_test` (release malloc guard) | `0` | `build/y5x-evidence/green-editor-release.log` |

Run wrapper `build/y5x-evidence/run-suite.sh` stamps power/load/disk, creates
and removes the runtime directory, and removes only fixture directories newly
created under this worktree's `build/` during its suite. Runs are sequential.
Sanitizer execution uses the approved sandbox escape for LeakSanitizer's
thread inspection; leak detection stays enabled.

The release editor suite acquired the shared display lock and completed with
the malloc guard active. Selected output, with its power/load/disk stamps
(M)[AC]; the key counts are the existing suite's fixture loops, unchanged by
this bead:

```text
Not charging
1
 09:04:01 up 6 days, 13:52,  1 user,  load average: 46.79, 55.57, 55.66
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  381G   75G  84% /
waiting for display :99 (held by 2061509)
clipboard malloc guard: 0 allocations (active); expected zero by law
review 21: native dequeue-to-translation mallocs=0; mutation-to-submit=0; guard=active (M)[AC]
editor_test: requested=gl actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: requested=raster actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: null hl-on 10000 keys mallocs=0 guard=active
editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: all passed
build/tests/editor_test exit=0
Not charging
1
 09:11:34 up 6 days, 13:59,  1 user,  load average: 31.63, 35.87, 46.35
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  381G   75G  84% /
```

Xvfb was reachable; both required sanitizer display suites and the release
editor suite completed under the shared display lock. Verification was the
brief's named suites, rather than a new review pass or a full `make check`.

## What remains

Nothing remains for edit-y5x. All required build and verification commands
passed after the source change. No further bead was taken.

## Outside scope, unchanged

- edit-601 (`restore.c`), edit-4da (the `recovery.c` deferral bound), and
  edit-aq9 (the whole-text fallback ceiling), as identified in the brief.
- The existing `large_orphans` fixture uses a sparse source but writes a
  whole-text journal and its copy densely. This inherited fixture behavior
  was left unchanged; the added regression uses only marker-sized data.
  The existing large-fixture wrappers and the outer runner remove generated
  fixtures on return; disk snapshots are pasted above.
- The full session suite again left a private BASE generation in a generated
  `editor-session-*` directory, as already reported by edit-c26. The runner
  removed that generated fixture directory; no unrelated test code changed.
