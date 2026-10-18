# edit-uzrb — s8 worker report

Implemented: journalled Ctrl+S is admitted while another tab is `pending_open`.
The save graph uses the same eligible population for its live count and compact
ranks. Adoption republishes ranks, including for an inactive loading tab.
The focused regression saves the dirty small file with the loading tab before
and after it, checks disk bytes and clean state, then edits/saves again after
adoption. Explicit checkpoint recovery of pending tabs remains intact.

Files touched:

- `src/editor/session.c`
- `tests/editor_session_test.c`
- `docs/decisions/edit-uzrb.md`
- `docs/worker-reports/edit-uzrb-s8.md`

Design choices are in [edit-uzrb.md](../decisions/edit-uzrb.md).
No forbidden tracking/version-control command or protected document was changed.
No new review pass was performed.

Numeric provenance: (P) the sparse fixture reuses `FM4_GIB`, the brief's GiB
length; its COPY threshold is the length plus the next byte. Tab indices are
ordinals of the small/large pair. Geometry follows the existing status width
and tab/text/status rows. (E) watchdogs reuse `EDITOR_IDLE_NS`. Build concurrency,
display number, lock timeout, sanitizer options, and compiler requirements are
supplied by the brief. Exit codes, Boolean values, API error codes and source
line diagnostics below follow their existing C/POSIX/test contracts (P).
The allocation guard workload is the existing editor test loop (P), unchanged.
All captured system/test observations below are (M)[AC]; external power was
confirmed by `/sys/class/power_supply/AC/online` returning its connected value.
Battery status and uptime bracket the runs; no new timing target is introduced.
The compilers reported gcc 13.3.0 and clang 18.1.3 (M)[AC].

**Red, before production changes**

Command (exit code is included in the captured output):

```sh
task_runtime=$(mktemp -d)
ASAN_OPTIONS=detect_leaks=1 EDIT_DISPLAY_LOCK_TIMEOUT=1800 \
  XDG_RUNTIME_DIR="$task_runtime" DISPLAY=:99 \
  sh tools/with_display_lock.sh build/san/tests/editor_session_test --edit-uzrb
rmdir "$task_runtime"
```

The initial sanitizer target build and its rebuild both exited successfully.
The clean red run, including `df -h /` before and after, was:

```text
Not charging
1
 12:40:13 up 6 days, 17:28,  1 user,  load average: 34.25, 35.20, 38.92
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  404G   52G  89% /
edit-uzrb: pending after small tab: pending=yes save_active=no save_error=2
editor_session_test:2084: FAIL pending->pending_open && editor_save_active(f.e) && !small->save_error && !small->operation_error
exit=1
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  404G   52G  89% /
Not charging
 12:40:13 up 6 days, 17:28,  1 user,  load average: 34.25, 35.20, 38.92
```

**Green, same test after the fix**

`make -j3 build/san/tests/editor_session_test` exited successfully. The same
run command, with leak detection enabled, produced:

```text
Charging
1
 12:40:45 up 6 days, 17:28,  1 user,  load average: 33.42, 34.94, 38.70
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  404G   52G  89% /
edit-uzrb: pending after small tab: pending=yes save_active=yes save_error=0
edit-uzrb: pending before small tab: pending=yes save_active=yes save_error=0
edit-uzrb: Ctrl+S admitted during pending open; saved bytes and clean tab verified
edit-uzrb: Ctrl+S admitted during pending open; saved bytes and clean tab verified
exit=0
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  404G   52G  89% /
Charging
 12:40:52 up 6 days, 17:28,  1 user,  load average: 33.47, 34.93, 38.68
```

**Verification commands and exit codes**

`make -j3 all` — exit `0` (gcc release, required C11 warnings with `-Werror`).

The sanitizer binaries were built through the Makefile's check-style targets:

```sh
make -j3 build/san/tests/editor_test build/san/tests/editor_session_test \
  build/san/tests/editor_checkpoint_test build/san/tests/dataloss_test \
  build/san/tests/journal_test
```

Build exit: `0`. The Makefile used clang with AddressSanitizer and UBSan.
Each sanitizer run below used the red/green command's environment and display
lock, substituting the listed binary/arguments, with a fresh temporary runtime
directory removed afterwards. Leak detection remained enabled. These runs
executed outside the sandbox so LeakSanitizer could inspect threads.

| Binary and arguments | Exit |
| --- | --- |
| `build/san/tests/editor_session_test --edit-uzrb` | `0` |
| `build/san/tests/editor_test` | `0` |
| `build/san/tests/editor_session_test` | `1` initially; `0` on unchanged full rerun |
| `build/san/tests/editor_checkpoint_test` | `0` |
| `build/san/tests/dataloss_test` | `0` |
| `build/san/tests/journal_test` | `0` |
| `build/edit-uzrb-baseline/editor_session_test --edit-djg` | `0` (original session implementation diagnostic) |

The baseline diagnostic was built in `build/edit-uzrb-baseline/` by removing
only this bead's session changes from a copy, compiling with the same sanitizer
flags, and linking its object ahead of the existing library. Production sources
and the required sanitizer binaries were left intact. Its build exited `0`.

Final suite completion output (timing lines omitted; exact runs were bracketed
by the power/load stamps and disk snapshots):

```text
editor_test: all passed
editor_test exit=0
editor_session_test: headless save/repeated/new-target, recoverable failure retry, cutoff edits, shared targets, inactive failure, evicted saving tab, retired named buffers release slots and fds, checkpoint during evicted save and crash after it, interrupted-save startup, GB-scale orphan launch, Saving acknowledgement, allocation guard passed
editor_session_test retry exit=0
editor_checkpoint_test: pending COPY checkpoint/crash/recovery/save, edit-28u referencing checkpoints, edit-f3i post-save checkpoints passed
editor_checkpoint_test exit=0
dataloss_test: kill-after-append, kill at every save step, shorter base, reload/keep during held save, Ctrl+S under prompt, mixed hot exit with closed tab, reload vs keystroke, two-process save and orphan recovery, edit-fi2 recovery after rename-over/in-place rewrite and retained-copy lifecycle passed
dataloss_test exit=0
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
journal_test exit=0
```

Release checks used the same display lock, runtime directory and timeout;
`ASAN_OPTIONS` is unnecessary for the release binaries:

| Command after the shared environment prefix | Exit |
| --- | --- |
| `sh tools/with_display_lock.sh build/tests/editor_session_test --edit-uzrb` | `0` |
| `EDITOR_ALLOC_ONLY=null sh tools/with_display_lock.sh build/tests/editor_test` | `0` |
| `EDITOR_ALLOC_ONLY=raster sh tools/with_display_lock.sh build/tests/editor_test` | `0` |

The release regression's guard also asserts no request-path allocations while
the bulk worker is held. Captured release output:

```text
Not charging
1
 12:53:08 up 6 days, 17:41,  1 user,  load average: 29.58, 34.15, 37.21
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
edit-uzrb: pending after small tab: pending=yes save_active=yes save_error=0
edit-uzrb: pending before small tab: pending=yes save_active=yes save_error=0
edit-uzrb: Ctrl+S admitted during pending open; saved bytes and clean tab verified
edit-uzrb: Ctrl+S admitted during pending open; saved bytes and clean tab verified
release-pending exit=0
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
Not charging
 12:53:12 up 6 days, 17:41,  1 user,  load average: 29.53, 34.06, 37.16
Not charging
1
 12:53:12 up 6 days, 17:41,  1 user,  load average: 29.53, 34.06, 37.16
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
editor_test: null 10000 keys mallocs=0 guard=active
release-null exit=0
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
Not charging
 12:53:13 up 6 days, 17:41,  1 user,  load average: 29.53, 34.06, 37.16
Not charging
1
 12:53:13 up 6 days, 17:41,  1 user,  load average: 29.53, 34.06, 37.16
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
editor_test: raster 10000 keys mallocs=0 guard=active
release-raster exit=0
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
Not charging
 12:53:19 up 6 days, 17:41,  1 user,  load average: 29.41, 33.96, 37.11
```

Disk snapshots from every full sanitizer suite, including the failed session
run and its unchanged retry, follow. These are literal `df -h /` outputs (M)[AC].
The new GiB source is created only with `ftruncate`; its wrapper removes it and
its journal on success and assertion failure. The focused red/green and release
snapshots are pasted above. No `build/editor-uzrb-*` fixture remains.

```text
editor_test before
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  405G   51G  89% /
editor_test after
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  405G   51G  89% /
editor_session_test before
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  405G   51G  89% /
editor_session_test after
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   73G  85% /
editor_session_test-retry before
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   73G  85% /
editor_session_test-retry after
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
editor_checkpoint_test before
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   73G  85% /
editor_checkpoint_test after
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
dataloss_test before
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  385G   71G  85% /
dataloss_test after
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   73G  85% /
journal_test before
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   73G  85% /
journal_test after
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   73G  85% /
```

**Outside scope and remaining work**

- The first full `editor_session_test` failed at the final `rmdir(root)` in
  `existing_journal()`, after its save/disk assertions had succeeded. The test
  left `.sublimite-base-9FfPR3` in its temporary directory. Its length was the
  fixture's `strlen("base")` (P); the observed length was `4` bytes (M)[AC].
  The unchanged full suite passed on rerun. The original session implementation
  also passed the existing `--edit-djg` selection containing `existing_journal()`.
  The cause of the intermittent cleanup failure is unconfirmed; it was not
  fixed or subjected to a new review. The failed run's small fixture was removed
  after recording it. Original diagnostics:

```text
editor_session_test:572: FAIL rmdir(root) == 0
editor_session_test:2126: FAIL existing_journal() == 0
editor_session_test exit=1
```

- The cited `docs/worker-reports/edit-3f2k-s8.md` and
  `docs/decisions/edit-3f2k.md`, and `editor_recorded_rank`, are absent from this
  worktree. Existing checkpoint/publication code and the available placeholder
  and derived-tab decisions were used instead.
- The first sandboxed red run additionally hit LeakSanitizer's thread-inspection
  fatal error at teardown. Repeating outside the sandbox preserved
  `detect_leaks=1` and produced the clean red output above; green and all full
  required sanitizer runs used that working environment.

No edit-uzrb implementation work remains. The release build, focused regression,
required full sanitizer suites on their final runs, and release allocation
guards pass. The intermittent cleanup finding remains for coordinator triage.
One bead completed; stop here.
