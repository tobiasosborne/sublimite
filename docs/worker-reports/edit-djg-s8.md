# edit-djg worker report

Work was confined to this worktree. No `bd` or `git` commands were run, and
the protected handoff, worklog, plan, requirements and rules files were not
edited.

Provenance: command flags/toolchain requirements are (P), from the brief and
Makefile. Exit statuses are (P), from the command/test contracts: success is
zero, assertion failure is one, and make reports build errors as two. Captured
counts and system snapshots below are (M)[AC], direct program output with
BAT0 reporting `Not charging`. No new budget, duration or count was introduced.
The allocation workload is the existing `allocation_case` fixture.

## Files touched

- `src/main.c`: block termination signals before workers start, create a
  nonblocking close-on-exec signalfd, and retain the mask through teardown.
- `src/editor/editor.h`: optional borrowed termination descriptor.
- `src/editor/open.c`: register that descriptor in the existing epoll set.
- `src/editor/buffers.c`: consume readiness on the UI thread and request close.
- `src/editor/session.c`: hide recovery notices while panel rows exist.
- `tests/cli_test.c`: native typing followed by SIGTERM/SIGINT; observe the
  main function's final flush, assert lease ownership/release, validate the
  entire journal prefix, and relaunch to verify text/view/window recovery.
- `tests/editor_test.c`: compare every find-panel cell with/without the notice;
  also verify that closing the panel restores the notice and editing retires it.
- `tests/editor_session_test.c`: explicit changed-BASE refusal preserves the
  source bytes/inode and disk text; focused recovery selection for this bead.
- `docs/decisions/edit-djg.md` and this report.

## Decisions

The signalfd uses the existing UI epoll path and existing flush/close teardown.
There is no new handler, mutable process global, allocation or timer. Only
SIGTERM/SIGINT are blocked; `src/file` and its SIGBUS service were untouched.

Explicit-path recovery stays strict, as allowed by the brief. Its source is
also its live destination; renaming that artifact during installation would
require a different destination/lease transition. Main creates a fresh leased
destination and already uses tolerant discovery. `tools/journal_replay` uses
`journal_manual_replay`, independently of the editor recovery path. The
preservation regression passed before and after production changes; no failing
test was manufactured for this intentionally unchanged contract.

## Red runs

Built with:

```sh
make -j3 build/san/tests/cli_test build/san/tests/editor_test build/san/tests/editor_session_test
```

Build exit: zero (P). Display runs used `ASAN_OPTIONS=detect_leaks=1`, a fresh
`XDG_RUNTIME_DIR`, `DISPLAY=:99`, and `sh tools/with_display_lock.sh`.

Signal run stamp (M)[AC]:

```text
Not charging
08:53:42 up 6 days, 13:41, 1 user, load average: 68.11, 64.96, 54.40
```

Commands and output (M)[AC]:

```text
build/san/tests/cli_test --sigterm
cli_test:389: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
edit-djg: SIGTERM exit=143 clean_flush=no lease_released=yes accepted=0 written=0 durable=0
red-sigterm exit=1

build/san/tests/cli_test --sigint
cli_test:389: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
edit-djg: SIGINT exit=130 clean_flush=no lease_released=yes accepted=0 written=0 durable=0
red-sigint exit=1
```

The shell status derivation is (P): `128 + SIGTERM` / `128 + SIGINT`.
Process death releases flock leases automatically, hence the red
`lease_released=yes`; claiming a persistent held lease would be incorrect.
The zero sequence fields are the unfilled flush-observation struct: the main
function's final flush never ran. They do not describe an empty journal or
prove its durable state. Native typing had already been observed in its
replayable journal before the signal.

Panel run stamp and output (M)[AC]:

```text
Not charging
09:12:30 up 6 days, 14:00, 1 user, load average: 36.71, 36.50, 45.96
build/san/tests/editor_test --recovery-findpanel
editor_test:4158: FAIL intact
edit-djg: recovery notice with find panel: panel_cells_intact=no
red-panel exit=1
```

An earlier panel attempt failed in test setup because directly setting
`dirty` did not begin a valid frame. The test was corrected to use
`editor_full_layout` before the recorded grid failure; the overlay had not
been changed. A polling display-lock attempt also timed out, which was not
treated as a product red. The recorded grid run queued on the same flock and
passed its inherited descriptor to the unmodified display-lock wrapper.

## Green runs

All sanitizer display runs kept `ASAN_OPTIONS=detect_leaks=1` and the required
display wrapper. Captured values in the following blocks are (M)[AC].

```text
Not charging
09:18:50 up 6 days, 14:06, 1 user, load average: 37.98, 40.61, 44.87
build/san/tests/editor_test --recovery-findpanel
edit-djg: recovery notice with find panel: panel_cells_intact=yes
green-panel exit=0

Not charging
09:18:51 up 6 days, 14:06, 1 user, load average: 37.98, 40.61, 44.87
build/san/tests/cli_test
edit-djg: SIGTERM exit=0 clean_flush=yes lease_released=yes accepted=13 written=13 durable=13
edit-djg: SIGINT exit=0 clean_flush=yes lease_released=yes accepted=13 written=13 durable=13
cli_test: termination after native typing; production relaunch restores marker/cursor/selection/scroll and actual X window geometry (G: bead display trial)
green-cli exit=0

Not charging
09:19:03 up 6 days, 14:07, 1 user, load average: 36.82, 40.22, 44.68
build/san/tests/editor_session_test --edit-djg
edit-djg: explicit changed BASE rejected with EDITOR_ERR_IO; source journal and disk text intact
editor_session_test: three launches leave one nonempty 0600 journal; changed base opens and sets the source aside
green-session exit=0

Not charging
09:19:06 up 6 days, 14:07, 1 user, load average: 36.82, 40.22, 44.68
build/san/tests/editor_test
editor_test: all passed
green-editor exit=0
```

The green signal cases also asserted accepted/written/durable equality, no
pending bytes or journal error, a complete uncorrupted replay prefix, exact
typed text, and production relaunch recovery. The default CLI suite retained
its existing SIGKILL recovery case. Its forced startup-timeout diagnostics are
an expected negative control; the overall CLI exit above is successful.

## Verification

Exit statuses in this table are (P), direct command results.

The display commands ran outside the network sandbox with this setup (the
inherited flock is the same shared lock the wrapper validates):

```sh
task_runtime=$(mktemp -d)
trap 'rmdir "$task_runtime"' EXIT
exec {task_display_lock}<>/tmp/edit-xvfb-99.lock
flock "$task_display_lock"
export EDIT_DISPLAY_LOCK_FD="$task_display_lock" XDG_RUNTIME_DIR="$task_runtime" DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1
sh tools/with_display_lock.sh build/san/tests/editor_test --recovery-findpanel
sh tools/with_display_lock.sh build/san/tests/cli_test
sh tools/with_display_lock.sh build/san/tests/editor_session_test --edit-djg
sh tools/with_display_lock.sh build/san/tests/editor_test
EDITOR_ALLOC_ONLY=raster sh tools/with_display_lock.sh build/tests/editor_test
EDITOR_ALLOC_ONLY=null sh tools/with_display_lock.sh build/tests/editor_test
```

| Command | Exit |
| --- | --- |
| `make -j3 all` (final tree) | 0 |
| `make -j3 build/san/tests/cli_test build/san/tests/editor_test build/san/tests/editor_session_test build/san/tests/journal_test` | 0 |
| `python3 src/file/check_signal_globals.py build/rel/src/file/file.o` | 0 |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_test` (outside sandbox) | 0 |
| Sanitizer `cli_test`, under display lock | 0 |
| Sanitizer `editor_test --recovery-findpanel`, under display lock | 0 |
| Sanitizer `editor_session_test --edit-djg`, under display lock | 0 |
| Sanitizer full `editor_test`, under display lock | 0 |
| `EDITOR_ALLOC_ONLY=raster` release `editor_test`, under display lock | 0 |
| `EDITOR_ALLOC_ONLY=null` release `editor_test`, under display lock | 0 |

The signal checker output (M)[AC] was:

```text
finding10: ok mutable globals=2 file_bus,file_watches
```

Release guard stamp and results (M)[AC]:

```text
Not charging
09:17:58 up 6 days, 14:06, 1 user, load average: 40.93, 41.47, 45.37
editor_test: raster 10000 keys mallocs=0 guard=active
release-malloc-raster exit=0
editor_test: null 10000 keys mallocs=0 guard=active
release-malloc-null exit=0
```

An initial GCC build exited two (P) for the missing `<unistd.h>` declaration
of `read`; the include was added before the successful builds. Initial
sandboxed `xdpyinfo` exited one (P) because its Unix socket was inaccessible;
the outside-sandbox preflight succeeded. Initial sandboxed journal execution
exited one (P) with LeakSanitizer's ptrace restriction; the recorded successful
run was outside the sandbox with leak detection still enabled.

Detailed build/test logs are in `build/edit-djg/`.

## Disk snapshots and remaining work

No GB-scale fixture was introduced or run for this bead. Before/after the
release guards, `df -h /` reported (M)[AC]:

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
```

Before/after the sanitizer display runs, the shared filesystem snapshots were
(M)[AC]:

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   73G  85% /
```

The full no-argument `editor_session_test` remains unrun: its existing
`large_orphans_body` starts from a sparse source but intentionally writes a
dense GB-scale legacy orphan journal and another copy. Changing that fixture
would be outside this bead; running it would conflict with the brief's sparse
fixture rule. The focused selection covers explicit changed-BASE refusal,
leased/discovered changed bases, identical-byte rewrites, replacement-session
artifacts, and normal explicit-path recovery. The coordinator can arrange the
full suite after resolving that fixture constraint. This is the only
out-of-scope issue found; it was not fixed.

The identical-byte-rewrite reapply idea requiring a full-file BASE hash is
explicitly outside scope and remains unimplemented.
