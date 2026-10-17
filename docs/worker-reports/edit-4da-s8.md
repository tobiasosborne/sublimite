# edit-4da — worker s8

Implemented eager recovery of differences-only sessions on mapped BASEs.
Whole-text and configured COPY recovery still charge the complete BASE and
remain deferred when over JOURNAL_STARTUP_SCAN_BYTES. The constant and its
existing derivation in src/journal/journal.h are unchanged.

Files touched:

- src/editor/recovery.c — read-only per-buffer cost scan; op-only mapped replay
  and install; bounded line/view setup; no whole-BASE fallback on that path.
- tests/editor_session_test.c — sparse large BASE regression, whole-text and
  forced-COPY deferral, consecutive eager launches; sparse writes/copy for the
  inherited fm4 journal fixture and its set-aside.
- docs/decisions/edit-4da.md — install-path findings and the accounting decision.
- docs/worker-reports/edit-4da-s8.md — this report.

The brief's BASE size is (P) one GiB = 2^30 bytes, created with ftruncate.
All timings and observed sizes in the transcripts below are (M)[AC], with BAT0
and uptime/load stamps included. G4a is the existing tracked fm4 target, not a
new gate. The single-pass cost model remains the existing (E) model; mapped
BASE acquisition touches FILE_PREFIX_MAX rather than every kept BASE byte,
and DELETE lengths bound bytes piece_delete can copy. The fixture wrappers
remove their trees after both success and failure; the GB journal writer and
set-aside copy preserve zero-page holes. The sparse-journal stat assertion
uses (P) POSIX st_blocks units of 512 bytes. Cleanup verification found no
editor-fm4-*, editor-qct-* or editor-c26-* fixture directories left in build/.

## Red

Written and run before production changes. Command: sanitizer target built by
`make -j3 build/san/tests/editor_session_test` (exit 0), then:

```sh
task_runtime=$(mktemp -d)
trap 'rmdir "$task_runtime"' EXIT
XDG_RUNTIME_DIR="$task_runtime" DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_session_test
```

The expected regression is visible: a closed placeholder, no live tab, no dirty
text, and the source not retired. Whole-text and forced-COPY cases pass.

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
Not charging
 08:49:51 up 6 days, 13:37,  1 user,  load average: 70.61, 61.95, 50.66
editor_session_test:1090: FAIL rc == 0 && eager && text && dirty && mapped && small && retired
editor_session_test:1365: FAIL large_restore() == 0
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 1 session journals=1 [session-GThWcY .session-GThWcY.lock ]
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 2 session journals=1 [.session-Bpedyp.lock session-Bpedyp ]
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 3 session journals=1 [session-3ZCgWO .session-3ZCgWO.lock ]
clean_tab_set: saved clean tab set restored on two relaunches; one session journal
large_restore: closed tab title=corpus path=/home/tobias/Projects/sublimite/.wt/edit-4da-s8/build/editor-qct-JPgRDL/corpus
large_restore: whole-text journal bytes=4096 deferred=1
large_restore: closed tab title=corpus path=/home/tobias/Projects/sublimite/.wt/edit-4da-s8/build/editor-qct-JPgRDL/corpus
large_restore: differences with COPY acquisition deferred
large_restore: differences launch=0 tabs=0 closed=1 deferred=1 text=no dirty=no mapped=no journal_small=yes retired=no launch_ms=288.419 (M) G4a_p99=40 bound=tracked power=Not charging load1=70.80
red test exit=1
Not charging
 08:49:53 up 6 days, 13:37,  1 user,  load average: 70.80, 62.13, 50.78
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
```

The earlier sandbox-only attempt also removed its sparse fixture tree. Its
LeakSanitizer ptrace diagnostic is an execution-environment restriction:

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
Not charging
 08:49:15 up 6 days, 13:37,  1 user,  load average: 69.19, 60.71, 49.85
waiting for display :99 (held by unknown)
editor_session_test:1090: FAIL rc == 0 && eager && text && dirty && mapped && small && retired
editor_session_test:1365: FAIL large_restore() == 0
==28==LeakSanitizer has encountered a fatal error.
==28==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==28==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
red test exit=1
Not charging
 08:49:27 up 6 days, 13:37,  1 user,  load average: 68.44, 60.96, 50.10
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
```

## Green

The same session command after implementation, with leak detection enabled:

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  375G   81G  83% /
Not charging
 08:51:41 up 6 days, 13:39,  1 user,  load average: 66.65, 63.12, 52.40
waiting for display :99 (held by 2055467)
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 1 session journals=1 [session-bh57Xx .session-bh57Xx.lock ]
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 2 session journals=1 [.session-ufnmRo.lock session-ufnmRo ]
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 3 session journals=1 [session-9tTFuH .session-9tTFuH.lock ]
clean_tab_set: saved clean tab set restored on two relaunches; one session journal
large_restore: closed tab title=corpus path=/home/tobias/Projects/sublimite/.wt/edit-4da-s8/build/editor-qct-4pUh0g/corpus
large_restore: whole-text journal bytes=4096 deferred=1
large_restore: closed tab title=corpus path=/home/tobias/Projects/sublimite/.wt/edit-4da-s8/build/editor-qct-4pUh0g/corpus
large_restore: differences with COPY acquisition deferred
large_restore: differences launch=0 tabs=1 closed=0 deferred=0 text=yes dirty=yes mapped=yes journal_small=yes retired=yes launch_ms=434.277 (M) G4a_p99=40 bound=tracked power=Not charging load1=69.66
large_restore: differences launch=1 tabs=1 closed=0 deferred=0 text=yes dirty=yes mapped=yes journal_small=yes retired=yes launch_ms=498.438 (M) G4a_p99=40 bound=tracked power=Not charging load1=69.66
large_restore: whole-text deferred; differences restore eagerly across hot exits
large_orphans: launch_ms=95.810 (M) G4a_p99=40 bound=tracked power=Not load1=67.65
large_orphans: conflict reopen rc=0 disk_text=yes notice=yes restore_ms=1435.621 (M)
c26: first launch mode=copy
c26: hot-exit journal bytes=8192 (M) base_bytes=67108864
c26: relaunch editor_open=0 tabs=1 text=yes dirty=yes notice=none restore_ms=1075.442 (M) bound=tracked power=Not charging load1=67.77
c26: relaunch editor_open=0 tabs=1 text=yes dirty=yes notice=none restore_ms=1072.706 (M) bound=tracked power=Not charging load1=67.77
c26: fixture record INSERT offset=0 size=9
c26: fixture record INSERT offset=1 size=9
c26: fixture records=9 bases=1 targets=1 inserted=2 base=/tmp/edit-corpus/dense_short.txt head=xy
c26: relaunch editor_open=0 tabs=1 text=yes dirty=yes notice=none restore_ms=676.441 (M) bound=tracked power=Not charging load1=67.77
c26: dense64 hot-exit session and the drive-5 task-37 journal relaunch with the dirty tab restored
editor_session_test: three launches leave one nonempty 0600 journal; changed base opens and sets the source aside
editor_session_test: headless save/repeated/new-target, recoverable failure retry, cutoff edits, shared targets, inactive failure, evicted saving tab, checkpoint during evicted save and crash after it, interrupted-save startup, GB-scale orphan launch, Saving acknowledgement, allocation guard passed
green session test exit=0
Not charging
 08:53:16 up 6 days, 13:41,  1 user,  load average: 67.15, 64.52, 53.91
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  376G   80G  83% /
```

The functional regression passes, including relaunch of the installed
checkpoint. Both this new eager path and large_orphans missed the existing
tracked G4a time target under the stamped shared load. No time target was
relaxed or new budget invented; the byte-bound accounting and source-retirement
assertions pass. Timing misses are reported per CLAUDE.md law 1.

## Final rebuilt session run

The final identifier-only GCC correction was rebuilt into the named sanitizer
targets. A normal-helper retry timed out waiting for the shared display lease,
before executing any test. The completed retry queued a blocking flock on the
same lease and passed its descriptor as EDIT_DISPLAY_LOCK_FD to the required
with_display_lock.sh helper. The helper validated the inherited lease and ran
build/san/tests/editor_session_test with ASAN_OPTIONS=detect_leaks=1, DISPLAY=:99
and a private mktemp XDG_RUNTIME_DIR. No display protection was bypassed.

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   75G  84% /
Not charging
 09:14:04 up 6 days, 14:02,  1 user,  load average: 39.06, 37.45, 45.38
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 1 session journals=1 [session-DZcMM8 .session-DZcMM8.lock ]
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 2 session journals=1 [.session-WVOynF.lock session-WVOynF ]
clean_tab_set: launch tabs=1 title=source clean=yes
clean_tab_set: after launch 3 session journals=1 [.session-7kdCiB.lock session-7kdCiB ]
clean_tab_set: saved clean tab set restored on two relaunches; one session journal
large_restore: closed tab title=corpus path=/home/tobias/Projects/sublimite/.wt/edit-4da-s8/build/editor-qct-JnWLeZ/corpus
large_restore: whole-text journal bytes=4096 deferred=1
large_restore: closed tab title=corpus path=/home/tobias/Projects/sublimite/.wt/edit-4da-s8/build/editor-qct-JnWLeZ/corpus
large_restore: differences with COPY acquisition deferred
large_restore: differences launch=0 tabs=1 closed=0 deferred=0 text=yes dirty=yes mapped=yes journal_small=yes retired=yes launch_ms=288.535 (M) G4a_p99=40 bound=tracked power=Not charging load1=39.93
large_restore: differences launch=1 tabs=1 closed=0 deferred=0 text=yes dirty=yes mapped=yes journal_small=yes retired=yes launch_ms=275.006 (M) G4a_p99=40 bound=tracked power=Not charging load1=39.93
large_restore: whole-text deferred; differences restore eagerly across hot exits
large_orphans: launch_ms=204.351 (M) G4a_p99=40 bound=tracked power=Not load1=39.38
large_orphans: conflict reopen rc=0 disk_text=yes notice=yes restore_ms=2016.334 (M)
c26: first launch mode=copy
c26: hot-exit journal bytes=8192 (M) base_bytes=67108864
c26: relaunch editor_open=0 tabs=1 text=yes dirty=yes notice=none restore_ms=964.681 (M) bound=tracked power=Not charging load1=38.62
c26: relaunch editor_open=0 tabs=1 text=yes dirty=yes notice=none restore_ms=1389.064 (M) bound=tracked power=Not charging load1=38.62
c26: fixture record INSERT offset=0 size=9
c26: fixture record INSERT offset=1 size=9
c26: fixture records=9 bases=1 targets=1 inserted=2 base=/tmp/edit-corpus/dense_short.txt head=xy
c26: relaunch editor_open=0 tabs=1 text=yes dirty=yes notice=none restore_ms=627.667 (M) bound=tracked power=Not charging load1=38.62
c26: dense64 hot-exit session and the drive-5 task-37 journal relaunch with the dirty tab restored
editor_session_test: three launches leave one nonempty 0600 journal; changed base opens and sets the source aside
editor_session_test: headless save/repeated/new-target, recoverable failure retry, cutoff edits, shared targets, inactive failure, evicted saving tab, checkpoint during evicted save and crash after it, interrupted-save startup, GB-scale orphan launch, Saving acknowledgement, allocation guard passed
final session retry exit=0
Not charging
 09:18:50 up 6 days, 14:06,  1 user,  load average: 37.98, 40.61, 44.87
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  382G   74G  84% /
```

## Verification

- Initial test compilation: exit 2 (treated void journal_close as a return
  value); corrected before the runtime red run.
- Final `make -j3 build/san/tests/editor_session_test build/san/tests/editor_hot_exit_test build/san/tests/journal_test build/san/tests/journal_retire_test`: exit 0, using the Makefile's clang ASan/UBSan flags.
- Initial sandboxed red run: exit 1 with the expected assertion, plus a
  LeakSanitizer ptrace restriction. Required leak-enabled runs were then made
  outside that sandbox restriction; the canonical red/green above have no
  sanitizer diagnostics.
- Initial `make -j3 all`: exit 2, GCC -Wshadow rejected a new local named view
  because of the existing view typedef. Renamed it saved_view; no behavior
  change. Final `make -j3 all`: exit 0 (GCC, -Werror).
- `ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_test`: exit 0.
- `ASAN_OPTIONS=detect_leaks=1 build/san/tests/journal_retire_test`: exit 0.
- Final rebuilt session retry: exit 0 (transcript above).
- `XDG_RUNTIME_DIR="$task_runtime" DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_hot_exit_test`: exit 0.
- `XDG_RUNTIME_DIR="$task_runtime" DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test`: exit 0; release guards active, all allocation counts zero. The completed retry used the same blocking inherited-lease method as the final session retry.

## Completed verification transcripts

Final headless sanitizer checks, selected result lines with the run's stamps:

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  380G   76G  84% /
Not charging
 09:08:07 up 6 days, 13:56,  1 user,  load average: 28.68, 43.91, 51.11
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
journal_test exit=0
(M)[AC] BAT0=Not charging empty_fixture_count=32 journal_bytes=4096 base_bytes=4 discovery_before_ns=1021139 discovery_after_ns=28514 load1=28.39
journal_retire_test: passed
journal_retire_test exit=0
Not charging
 09:08:36 up 6 days, 13:56,  1 user,  load average: 28.39, 42.64, 50.50
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  380G   76G  84% /
```

The normal-helper combined display command exited 1 because its session and
release acquisitions timed out before execution. Hot-exit passed here. Both
blocked suites then passed using the helper's validated inherited lease; the
final session transcript is above and release guard output is below.

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  380G   77G  84% /
Not charging
 09:01:15 up 6 days, 13:49,  1 user,  load average: 53.21, 60.25, 56.79
waiting for display :99 (held by 2061509)
editor_session_test: display lock timeout: display :99 (held by 2076245)
editor_session_test exit=1
Not charging
 09:11:15 up 6 days, 13:59,  1 user,  load average: 27.65, 35.46, 46.46
Not charging
 09:11:15 up 6 days, 13:59,  1 user,  load average: 27.65, 35.46, 46.46
waiting for display :99 (held by 2076245)
editor_hot_exit_test: ok trials=100 (G: bead acceptance), seeded random typing kill points; text/tabs/order/active/cursor/selection/scroll/window restored before first frame
editor_hot_exit_test exit=0
Not charging
 09:12:30 up 6 days, 14:00,  1 user,  load average: 36.71, 36.50, 45.96
Not charging
 09:12:30 up 6 days, 14:00,  1 user,  load average: 36.71, 36.50, 45.96
waiting for display :99 (held by 2087994)
editor_test: display lock timeout: display :99 (held by 2104116)
release editor_test exit=1
Not charging
 09:22:30 up 6 days, 14:10,  1 user,  load average: 51.57, 42.21, 44.31
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   73G  85% /
```

Final release run, selected allocation-guard lines and final status with the
run's power/load and disk stamps. Its command was the release helper command
listed above, with the same blocking inherited-lease prelude used for the final
session. All reported malloc guards were active.

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  383G   73G  85% /
Not charging
 09:22:55 up 6 days, 14:11,  1 user,  load average: 52.11, 43.08, 44.54
edit-g11 null typing: keys=95 frames=97 orange_frames=0 stale_frames=96 changed_pending=0 chrome_markers=0 mallocs=0 fill_max_ns=1033563
clipboard malloc guard: 0 allocations (active); expected zero by law
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
clipboard PRIMARY: queued selection/collapse/middle paste waits for latest capture, mallocs=0 passed
review 21: native dequeue-to-translation mallocs=0; mutation-to-submit=0; guard=active (M)[AC]
P1-1 section 6: inactive exact counts/jump, bulk cancellation/retry, mallocs=0 guard=active passed (M)[AC]
editor_test: requested=gl actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: requested=raster actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: null hl-on 10000 keys mallocs=0 guard=active
editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: all passed
final release editor_test exit=0
Not charging
 09:27:07 up 6 days, 14:15,  1 user,  load average: 65.14, 56.53, 49.80
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  384G   72G  85% /
```

Final fixture cleanup verification: exit 0. No editor-fm4-*, editor-qct-* or
editor-c26-* directories remained, and a stat pass over rg's complete build/
file list found no GB-scale regular files. The comparison used (P) the brief's
2^30-byte fixture size. The before/after df output above includes all large
fixture runs; build output and other workers changed the filesystem totals.

## Remaining and outside scope

No functional work remains for edit-4da. The existing tracked G4a timing goal
was missed in the stamped loaded runs, as reported above. No changes to restore.c; edit-601 and
edit-aq9 remain with their assigned work. Explicit existing-journal recovery
keeps its strict flat replay contract. Mapped recovery conservatively marks a
nonempty op log dirty without scanning BASE to detect a net no-op, matching
lazy recovery. Outside-scope execution issues observed: LeakSanitizer cannot run under the
initial ptrace sandbox, and the shared display helper's polling acquisitions
can time out under contention while owners change. Leak-enabled runs completed
outside that sandbox restriction; inherited-lease retries completed under the
same display lock. No changes were made to the display helper or other workers.
No additional review or unrelated fixes were performed.
No bd or git commands ran, and no protected handoff/worklog/plan/rules files
were edited.
