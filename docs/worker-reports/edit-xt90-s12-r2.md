# edit-xt90 round 2 worker report

Verdict: **done**. All nine review findings are addressed. Windows release/
sanitizer checks and the Linux release build/full sanitizer check pass;
typing-path allocations remain zero. Integration skips are explicit below.
All repository reads, edits, builds and scratch reconstructions used this
worktree. Outside it, only the two authorized briefing files were read.
No state-changing git command or bd was run. HANDOFF.md, docs/worklog and
src/os/os.h were not edited. The pre-existing untracked
`docs/worker-reports/edit-xt90-review-final.md` was left alone.

## Changes and files

- `src/journal/retire.c`, `private.h`: preserve iterator errors across passes;
  abort an incomplete inventory before retirement; use the locked directory's
  identity for artifact stat, lease, scan, rename and unlink. Add caller-owned
  iterator/lock/time fault seams.
- `src/journal/journal.c`, `path.c`, `journal.h`: pin retained-copy namespace
  operations, verify the absolute retained BASE parent before publication,
  provide directory-relative exclusive temporary creation, separately reopen
  the Windows worker's synchronous file object and verify its identity, carry
  both generation handles through rotation/close, budget ASCII temp/aside
  names correctly, restore writable-directory errors, and replace opaque clone
  retries with the existing cancellable chunk copy. Add off-path retention and
  publication-boundary hooks.
- `tests/journal_os_test.c`: interrupted inventory, replaced directory and
  retained-copy parent, successful maximal/non-ASCII rotation, no-clobber
  sole-copy set-aside, writable-open classification, and existing allocation
  guard checks. Failure cleanup now handles the fixture's child directories.
- `tests/journal_cancel_test.c`: Linux cancellation before copying and between
  chunks, with an opaque interrupted-clone retry oracle.
- `tests/journal_safety_test.c`: portable journal-only short/zero/error writes,
  failed data/full/directory barriers, consumed writeback loss including a
  released unsynced batch, failed/ambiguous publication, retained-BASE
  prepare/finish/cancel and retry, exact multi-chunk copy bytes, independent
  legacy/current wire/CRC/PAD fixtures, large torn groups/damaged COMMITs,
  and PAINT append/clear/checkpoint/torn-clear behavior.
- `tests/journal_hot_exit_test.c`, `tests/win/journal_kill_test.c`: actual torn
  large INSERT and COMMIT writes; consumed writeback failure; long-owner sole
  checkpoints; deaths after replacement, after adoption before old-handle
  close, and during cleanup; restore Linux's exact VIEW, ordered TABS/active
  index and WINDOW prelude in the seeded Windows campaign.
- `tests/win/journal_transport_test.c`: independent synchronous objects and
  positions, concurrent actual FlushFileBuffers and allocation-counted append,
  separate-process sharing denial, explicit legacy FileDispositionInfo
  delete-pending state, identity/old-byte/retry checks and last-duplicate lock
  release on death.
- `mk/win.mk`: include the portable safety/native transport suites and explicit
  dependencies for all included test sources/CLI sources. Narrow the Windows
  skip summaries to integration that still needs the editor/file port.
- `docs/decisions/edit-xt90.md`: appended the Round 2 finding/change/test table
  and the durability, cancellation and name-budget choices.

## Red → green evidence

All exit codes and observed counts here are (M) [AC, desktop Spiral]. Battery
status is absent on this desktop. Initial `uptime` was `12:49:23 up 5:05,
load average: 0.61, 1.28, 3.29`; during final verification it was
`13:25:09 up 5:40, load average: 11.51, 5.52, 3.85`.
The full raw transcripts and baseline scratch sources are under
`build/worker-logs/r2/` (build output, not tracked source).

Each production regression was tested red before its fix. Later clean
reproductions of the inherited blockers use round-1 journal/retire sources
reconstructed by reversing `git diff` into build scratch, adding only the
original iterator/lock test seams. They do not substitute a new algorithm.
The reconstructed sources were not written over production files.

### Interrupted reference inventory (finding 1)

Red command: `DISPLAY=:97 build/worker-logs/r2/round1/journal_os_test`
(exit 1; `f1-round1-red.log`; original red is `f1-red.log`):

```text
journal_os_test:322 FAIL journal_check_base(&base)==0 (native=2)
journal_os_test:357 FAIL !rc && !cleaned (native=0)
```

Green command: `DISPLAY=:97 build/san/tests/journal_os_test` (exit 0):

```text
journal_os_test: interrupted reference inventory preserves recoverable BASE and journal
```

The iterator yields the over-cap BASE, then IO before its referencing session.
The green oracle checks the original BASE identity/bytes and actually replays
its recoverable session, rather than merely expecting an error code.

### Locked directory replacement and async copy (finding 2)

Red command: `DISPLAY=:97 build/worker-logs/r2/round1/journal_identity_test`
(exit 1; `f2-round1-red.log`; original red is `f2-red.log`):

```text
journal_os_test:261 FAIL journal_check_base(&base)==0 (native=2)
journal_os_test:358 FAIL !rc && !cleaned (native=0)
```

Async-copy red command: `DISPLAY=:97 build/san/tests/journal_os_test`, before
its namespace fix (exit 1; `f2-copy-red.log`):

```text
journal_os_test:247 FAIL retained.error==JOURNAL_BASE_CHANGED (native=22)
```

Green, same sanitizer suite (exit 0 on Linux and Windows):

```text
journal_os_test: renamed locked directory never mutates replacement recovery namespace
journal_os_test: retained copy refuses stale absolute parent and cleans the pinned directory
```

NTFS refuses to move a directory with a named open child in this fixture.
The copy fixture unlinks the open journal's name before starting retention,
which lets NTFS express the same parent-replacement race. It then runs the
full identity/cleanup assertions; it is not skipped.

The optional pre-migration-main scratch build was not attempted: the explicit
read boundary permits only the briefing files in that checkout. The preserved
pre-migration inventory and the review identify both blockers as inherited.

### Windows worker object ownership (finding 3)

Red command: `tools/winrun.sh build/win/san/tests/journal_transport_test.exe`
(exit 1; `f3-red.log`):

```text
journal_transport_test:32 FAIL observed.handle!=j->disk.fd.handle
```

Green, same command (exit 0; final Windows suite also executes release):

```text
journal_transport_test: independent synchronous file objects, concurrent append/FlushFileBuffers, allocations=0 passed
```

The additional file-position assertion excludes DuplicateHandle aliases.
The safety suite reads its modeled durable image after a real successful
worker-handle fence, covering writes already completed on the UI handle.
This is neither a disk-stall latency measurement nor a power-loss simulation.

### ASCII/UTF-8 rotation and possible sole copies (finding 4)

Red command: `tools/winrun.sh build/win/san/tests/journal_kill_test.exe`
(exit 1; `f4-red.log`):

```text
journal_hot_exit_test:80 FAIL journal_rotate(j,&r,1)==0 native=123
journal_hot_exit_test: child exited code=1 phase=10
```

A separate success-path regression against the reconstructed round-1
implementation, `tools/winrun.sh build/win/san/tests/journal_round1_test.exe
--long-only`, exits 1 (`f4-success-red.log`):

```text
journal_os_test:203 FAIL journal_rotate(j,&cp,1)==0 (native=123)
```

Green, the current Windows journal suites:

```text
journal_os_test: maximal ASCII and 200-byte non-ASCII owners rotate successfully
journal_hot_exit_test: cache/durable/group/PAINT/rotation/copy process-death recovery passed
```

(P) The ASCII success fixture has 255 bytes, the component limit. (G) The
non-ASCII fixture has the brief's 200 UTF-8 bytes: 100 copies of two-byte U+00E9.
The death cases preserve the complete checkpoint under a sole-copy set-aside.

### Linux behavior compatibility (findings 7 and 9)

Writable-directory red command: `DISPLAY=:97 build/san/tests/journal_os_test`
(exit 1; `f7-red.log`):

```text
journal_os_test:59 FAIL journal_open(&rejected,dir,&pool,NULL)==JOURNAL_IO && !rejected (native=21)
```

Green is the same suite, exit 0, checking open and open_checkpoint separately,
as well as manual replay's INVALID directory target. Acknowledgment's BUSY
nonregular source remains covered by the acknowledgment suite.
Pre-migration lines, preserved in `build/worker-logs/inventory-before.txt`:

```c
/* journal.c:520–521 */
int fd=openat(dfd,canonical+name_offset,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600); struct stat sb;
if(fd<0 || fstat(fd,&sb) || !S_ISREG(sb.st_mode) || sb.st_size!=0) rc=fd<0?JOURNAL_IO:JOURNAL_INVALID;
/* journal.c:534–535 */
int fd=openat(dfd,canonical+name_offset,O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK); struct stat sb;
if(fd<0 || fstat(fd,&sb) || !S_ISREG(sb.st_mode) || sb.st_size<=0) rc=fd<0?JOURNAL_IO:JOURNAL_INVALID;
```

Cancellation red command: `DISPLAY=:97 build/san/tests/journal_cancel_test`
(exit 1; `f9-red.log`):

```text
journal_cancel_test:40 FAIL clone_retries==0
```

Green, same command (exit 0; `f9-green.log`):

```text
journal_cancel_test: no opaque EINTR clone retries; cancellation before and between copy chunks passed
```

The old cancellation checkpoint is preserved verbatim in the inventory,
pre-migration `journal.c:1021`:

```c
do { cloned=ioctl(r->fd,FICLONE,source); } while(cloned<0 && errno==EINTR && !work_should_stop(ctx));
```

Using the bounded copy path deliberately removes the clone optimization.
Checking cancellation only after the frozen wrapper returns would not restore
that checkpoint. Other frozen-wrapper EINTR policies are unchanged.

### Included-source dependencies (finding 8)

Command: `python3 build/worker-logs/r2/dependencies.py`. For each release and
sanitizer kill/ack executable it runs `make TARGET=win -n -W <included> <exe>`
and asserts that the corresponding test link is scheduled. Red exit 1 after
bringing the binaries up to date (`f8-red.log`):

```text
build/win/rel/tests/journal_kill_test.exe: included tests/journal_hot_exit_test.c schedules relink: False
build/win/san/tests/journal_kill_test.exe: included tests/journal_hot_exit_test.c schedules relink: False
build/win/rel/tests/journal_ack_test.exe: included tools/journal_replay.c schedules relink: False
build/win/san/tests/journal_ack_test.exe: included tools/journal_replay.c schedules relink: False
```

Green exit 0 (`f8-green.log`): the same four lines report `True`.

### Missing Windows oracles (findings 5 and 6)

The metadata oracle was added first. With the old Windows writer,
`tools/winrun.sh build/win/san/tests/journal_kill_test.exe --metadata-only`
exits 1 (`f6-red.log`):

```text
journal_hot_exit_test:115 FAIL journal_replay_file(path,replay_random,&r,&rr)==0 native=258
```

After adding the prelude and adjusting edit/durable sequence accounting,
the same command exits 0 (`f6-green.log`):

```text
journal_kill_test: ok trials=1 script_edits=100000 issued=1 page_cache=1 replayed=1 acknowledged=0 (page_cache<=replayed<=issued; piece_tree==independent_byte_model_at_replayed; trial0=kill_before_pump)
```

The post-replacement death oracle was added before wiring its production
hook. Red command `tools/winrun.sh build/win/san/tests/journal_kill_test.exe
--boundary=15` exits 1 (`f5-death-red.log`):

```text
journal_hot_exit_test: child exited code=1 phase=15
journal_hot_exit_test:238 FAIL trial(self,root,at)==0 native=2
```

The same command exits 0 after the hook is wired (`f5-death-green.log`).
The default suite also runs adoption, cleanup, actual torn INSERT/COMMIT,
failed-writeback and retained-copy death cases.

The no-clobber set-aside test was added before the deterministic time seam
was wired. Red `DISPLAY=:97 build/san/tests/journal_os_test` exits 1
(`f5-aside-red.log`):

```text
journal_os_test:199 FAIL journal_retire_with_io(dir,NULL,&io)==JOURNAL_IO (native=0)
```

Green proves that a collision preserves both file identities/bytes, then a
retry moves the sole copy without overwriting an existing set-aside.

The remaining journal-only safety cases are coverage additions, not previously
incorrect journal algorithms. They already pass the reconstructed round-1
journal.c (`f5-round1.log`, exit 0). There is no fabricated production red for
those cases. The suite's deliberately corrupted-CRC negative control,
`DISPLAY=:97 build/san/tests/journal_safety_test --negative-control`, exits 1
(`f5-wire-red.log`):

```text
journal_safety_test:296 FAIL journal_replay_bytes(expected,sizeof expected,wire_visit,&w,&rr)==0 && !rr.corrupt && w.next==count && rr.records==count native=0
```

With the unmodified independent fixture, Linux and Windows safety suites
exit 0 and report all transport/transaction/torn-record/PAINT oracles passed.
Native sharing denial and explicit legacy delete-pending both refuse rotation,
preserve the old generation and retry correctly after the relevant handles
close. The lease-holder process retains only a duplicate before death; its
lock is unavailable before death and available afterwards.

## Acceptance and measurements

Linux release `make -j64 all`: exit 0 (`linux-all.log`).
Linux full `DISPLAY=:97 make -j64 check`: exit 0, LeakSanitizer enabled
(`linux-check.log`):

```text
check: 108 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

(M) The binary count is printed by make from its TEST_SAN target list. The
display-lock contract intentionally runs failing nested :99 fixtures; their
expected timeout/error lines are followed by PASS and the outer check exits
successfully. All ordinary display suites used the brief's DISPLAY=:97.

Linux release journal suites also exit 0 (`linux-release-journal.log`,
`linux-release-safety.log`), including the active counting allocator:

```text
journal_os_test: malloc_guard=active append_allocations=0
journal_os_test: guarded_appends=10000 append_allocations=0
```

(M) The append count is the unchanged journal_test campaign fixture; zero
allocations is read from the active guard, rather than inferred from ASan's
inactive counting shim.

Windows full `make TARGET=win -j64 check`: exit 0
(`windows-check-private.log`), after building all release/sanitizer targets in
the normal Linux environment. The check invocation was:

```sh
TMPDIR=/tmp TEMP="$isolated" TMP="$isolated" \
  WSLENV="TEMP:TMP${WSLENV:+:$WSLENV}" make TARGET=win -j64 check
```

`$isolated` was created as a native Windows process beneath the real Windows
TEMP; its path is saved in `windows-private-temp.txt`. Cleanup also exited 0.
Exact make summary:

```text
ASan fired: ok
Windows journal: transport, retained-BASE transactions, wire/CRC/PAD, PAINT, sharing states and process-death oracles passed
Windows journal: SKIP editor-dependent group/retire/session/recovery/dataloss integration and first-paint closed-tab restart (editor/platform not ported)
Windows journal: SKIP src/file save integration (file module not ported); journal_save_prepare/finish/cancel run in journal_safety_test
Windows check: 8 release + 8 sanitizer test binaries passed; ASan negative control passed
```

(M) The Windows counts derive from make's WIN_TEST_REL/WIN_TEST_SAN lists:
acknowledgment, kill, native transport, native OS white-box, smoke, portable OS,
portable journal OS and portable journal safety. Both seeded campaigns use
the unchanged (G) Linux fixture count of 50 trials; exact issued/cache/replay/
acknowledged counts are in the raw log. Release and sanitizer journal suites
include all added cases. Windows release counting reports
`guarded_appends=10000 append_allocations=0`; the native concurrent
append/flush test also reports allocations=0. The Windows OS white-box suite
reports one unavailable symlink fixture with native error 1314, requiring
Developer Mode/elevation; no Windows privilege change was attempted.

Final verification stamp: (M) [AC, desktop Spiral], `13:41:12 up 5:55,
load average: 5.64, 2.97, 3.00`. ASAN_OPTIONS and LSAN_OPTIONS were unset for
the Linux check, so the sanitizer's default LeakSanitizer remained enabled.
`git diff --check` passes. Abandoned native red-run synthetic journal fixtures
whose originating processes had exited were removed (`windows-red-fixture-cleanup.log`,
exit 0); active processes were excluded from that cleanup.

The Linux journal benchmark completed with exit 0 and
`journal_bench: correctness=ok mode=GATE gate_misses=0 status=0`.
Its raw output says power `unknown` because this desktop has no BAT0; the
worker's authoritative stamp is (M) [AC, desktop Spiral]. Before the run,
`uptime` was `13:19:29 up 5:34, load average: 2.86, 3.07, 3.07`; raw per-row
load1 ranged from 2.86 to 2.95. The existing literal one-byte campaign measured
append p50/p99 = 978/8310 ns, the existing 1024-byte campaign = 1173/11148 ns;
the paste campaign reported allocations=0. These are observations under the
concurrent check load, not new budgets or a comparison campaign.

## Found, not fixed / integration limits

- The first full Windows check passed all executed journal suites but ended
  with exit 2 in the sanitizer OS reparse case: the shared fixture
  `%TEMP%\\sublimite-test-reparse\\wof.txt` had disappeared after the release
  OS case passed. The fixture script uses a fixed shared directory. The
  unchanged check is rerun with TEMP/TMP forwarded through WSLENV to a private
  Windows-created scratch root (`TMPDIR=/tmp` keeps the Linux compiler
  scratch location valid). A preparatory isolated attempt exited 2 before
  running tests because Linux clang honored the Windows TMP path; prebuilding
  and setting TMPDIR=/tmp corrected that invocation. No OS code or fixture
  script was changed.
- Windows editor-dependent group/retire/session/recovery/data-loss integration,
  the actual first-paint restart/closed-tab behavior, and src/file save
  integration remain pending those ports. Journal-only transaction, group,
  wire, PAINT and process-death oracles run here; the skip summary says so.
- The existing acknowledgment suite still explicitly skips hard-link/symlink
  fixture creation because frozen os.h has no creation API. Native Win32 can
  express hard links; adding those native fixtures was not needed for the
  requested sharing-denial/delete-pending coverage. This is test coverage,
  not a claim that NTFS lacks links.
- The documented edit-dzk1 save-worker checkpoint omission of PAINT is
  unchanged. This work tests caller-supplied journal checkpoints and does not
  claim to repair the editor session graph's known omission.
- TerminateProcess preserves the kernel/cache boundary. Writable-handle
  successful sync is the file-data/size fence; completed replacement is the
  process-visible name boundary. No Windows directory-fsync or power-loss
  guarantee is invented.

## Coordinator decisions

No journal algorithm decision remains open. Carry the explicitly skipped
editor/file integration forward with their ports; the shared OS fixture's
fixed-name collision is outside this bead. The optional main pre-migration
scratch build would require broader read authorization and was not necessary
to demonstrate the inherited round-1 losses.

## Coordinator verification (session 22)

Rebased over W0.5, W1.2, W1.3 (mk/win.mk: `… font journal`, journal_os_test and journal_safety_test in WIN_TESTS, W2.2's WIN_COMPILE_MODULES line kept). On that tree, (M)[AC, desktop Spiral]:

```
make -j64 all                 → exit 0 (gcc 13)
DISPLAY=:95 make -j64 check   → exit 0, 109 suites, LeakSanitizer on (13:47 → 14:18)
make TARGET=win -j64 check    → 78 results exit=0 (every journal suite rel and san); two red: d3d11_test rel and san `first_frame == 0`, an already-landed W1.2 test that fails identically on main's own binary from 13:49 on (desktop foreground changed; environmental, edit-kv2g first item), not this bead
```
