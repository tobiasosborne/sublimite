# edit-mdv session 9 worker report

Scope: P4.7b continuation, docs/reviews/P4-modules-2.md §§9–20, in order.
Session 8's torn-reload fix is retained. This session completes scoped code and
regressions for §§9–17. §§18–20 remain incomplete. Design choices are recorded
in docs/decisions/edit-mdv.md. No coordinator-owned tracking, HANDOFF.md,
docs/worklog or Makefile changes were made. Git was used read-only; bd was not run.

## Per-finding result

| Finding | Result |
| --- | --- |
| §9 FIFO acquisition | Nonblocking open, descriptor regular-file rejection, existing canonical-entry validation retained. A held bulk worker synchronizes replacement of the regular target by a FIFO; error returns without opening a writer. |
| §10 journal BASE conflict | JOURNAL_BASE_CHANGED invokes external-conflict classification without losing the diagnostic/token. Journal-backed notified and unnotified changes expose keep/reload; keep succeeds without discarding edits. |
| §11 undo cleanliness | Host current/saved content identities are independent of monotonic revision. Tests cover undo to clean, branch identities, evicted saved history, later edits/undo during save, and subsequent silent reload. Save identity cutoff survives journal finish. |
| §12 unrelated reused slot | Destruction uses work_handle_finished on the original handle. The regression holds an unrelated job in the reused slot while controller destruction succeeds. |
| §13 reload anonymous memory | Validated immutable file-backed snapshot, reflink or bounded streamed copy, retained independently by old/new trees and snapshots. The ownership regression rejects file-size anonymous growth. Temporary backing requires writable parent, O_TMPFILE support and space; failures preserve old content. |
| §14 UI metadata construction | New owner-thread piece construction continuation, capped chunk batches, constant-size per-level state, no file-size pointer array. UI take returns BUSY with untouched outputs between slices and atomically installs only a complete tree. Allocation counts bound each slice; measured timings are descriptive, not a timing verdict. |
| §15 failed installation | Every construction allocation is faulted in turn. FAILED/FILE_ERR_NOMEM is exposed; staged metadata is destroyed, an exclusive arena mark is reset, backing is retired and both recovery actions return. Retrying succeeds in the same tab. Non-reclaiming allocators must supply paired rollback callbacks. |
| §16 actual mapped identity/guard | File-owned retained lease captures actual opened baseline and original descriptor/mapping/fault service. Controller acquires its own reference. Tests close the file object before save, exercise large mapped content, detached originals, restored-mtime rewrites and sticky SIGBUS after regrowth. Detached metadata changes are conservatively refused; repeated mapped saves after detachment may require reload. |
| §17 mailbox-only completion | Separate sealed result/token lease transfers solely through a generation-validated terminal mailbox message. Saturation retries as a cooperative FIFO continuation; a blocked unrelated job still starts. Bound receiver protects shared-pool delivery. No completion polling fallback remains. |
| §18 G8s scope | Incomplete, no red/fix claimed. Existing controller is standalone; editor has no savectl save/status-frame wiring. Required bounded session request/checkpoint preparation and actual submitted-frame acknowledgement test remain. |
| §19 long journal lease | Not started, preserving strict order after §18. Whole-session journal lease still spans file writing. Prepare handoff, finish reacquisition and crash regression remain. |
| §20 finish retry recovery | Not started. Old journal_retry guidance remains; fresh-inode complete-session rotation, retained-token reconciliation and its regression remain. |

Supporting piece/file header and implementation changes are the minimal APIs
required by §§14/16. No new module globals were introduced. Content/view/idle
notifications remain allocation-free; replacement construction uses the supplied
allocator reservation. Controller/result allocation is setup-only; scratch and
filesystem work are on workers. Original allocator/I/O harness findings outside
§§9–20 were not fixed.

## Pasted red evidence, before each fix

All behavioral commands use `DISPLAY=:99 build/tests/savectl_test OPTION` after
`DISPLAY=:99 make -j4 build/tests/savectl_test`. Each behavioral red terminated
with an assertion failure. Line numbers are from the test version at that run.

§9, `--fifo`:

```text
tests/savectl_test.c:459: !savectl_get_model(f.s).busy
```

§10, `--journal-conflict`:

```text
tests/savectl_test.c:467: m.banner && m.modified && m.can_keep && m.can_reload
```

§11: the new contract/test first failed to link on the absent API. A temporary
adapter to the original conservative modified behavior then produced the
behavioral red, `--undo-clean`:

```text
tests/savectl_test.c:451: !savectl_get_model(f.s).modified
```

§12, `--reused-slot`:

```text
tests/savectl_test.c:461: savectl_destroy(f.s)==SAVECTL_OK
```

§13, `--reload-memory`:

```text
reload anonymous ownership delta=16785408 bytes (M)[AC], bound=2097152 bytes (G)
tests/savectl_test.c:475: after<=before+2u*FILE_PREFIX_MAX
```

§14, `--reload-slices`:

```text
reload first UI slice=29322 ns (M)[AC], allocations=44 (M)[AC]
tests/savectl_test.c:478: rc==SAVECTL_BUSY && !replacement
```

§15, `--reload-failure`:

```text
tests/savectl_test.c:484: m.state==SAVECTL_FAILED && m.file_error==FILE_ERR_NOMEM
```

§16: the public integration regression first failed to link, before providing
file accessors:

```text
undefined reference to `file_source_acquire'
undefined reference to `file_source_identity'
undefined reference to `file_source_release'
```

With those file accessors supplied and the controller still unwired,
`--file-source` then failed behaviorally:

```text
tests/savectl_test.c:489: savectl_save(f.s,f.tree,NULL,NULL,0)==0
```

§17, `--mailbox-protocol`:

```text
tests/savectl_test.c:377: savectl_get_model(f.s).state==SAVECTL_SAVING && savectl_get_model(f.s).busy
```

## Pasted green evidence

Individual release regression runs after their respective fixes exited zero:

```text
savectl reload regular-to-FIFO rejects without writer: ok
savectl journal BASE_CHANGED exposes external recovery: ok
savectl journal BASE_CHANGED exposes external recovery: ok
savectl undo clean identity/branch/eviction/save cutoff: ok
savectl destroy ignores unrelated reused work slot: ok
reload anonymous ownership delta=12288 bytes (M)[AC], bound=2097152 bytes (G)
savectl reload mapping budget/overlapping snapshots: ok
reload first UI slice=11003 ns (M)[AC], allocations=11 (M)[AC]
reload UI slices=16, max=11003 ns (M)[AC]
savectl reload UI construction yields bounded slices: ok
savectl every reload construction allocation: rollback/retry: ok
savectl retained actual mapped identity/guard: ok
savectl terminal completion requires mailbox and yields on saturation: ok
```

These are ownership/work-admission checks, not a noisy single-timing gate
verdict. Power was AC (Charging initially, later Not charging). Red/green logs
are build/edit-mdv-s9-N-red.log and build/edit-mdv-s9-N-green.log, with an
additional behavioral red log for §16. Required final repository validation is
recorded below when it completes.

## Required final validation

Scope additions stopped at the requested cutoff. The initial release build
exited zero with GCC 13.3.0 and the repository warning flags. Clang 18.1.3 builds
and both freshly rebuilt module sanitizer suites exited zero. The initial
sandbox full-suite attempt failed at cli_test's unavailable display; the
approved run with access to private :99 passed the complete repository suite
after the scoped G10f correction below. A backward session-clock
jump caused make's clock-skew warning, so a forced rebuild was used to avoid
trusting cached object timestamps.

Final module commands (each exit zero):

```
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_test
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/savectl_reload_test
```

Fresh-build green excerpt:

```text
savectl terminal completion requires mailbox and yields on saturation: ok
savectl every reload construction allocation: rollback/retry: ok
reload first UI slice=78770 ns (M)[AC], allocations=11 (M)[AC]
reload UI slices=16, max=78770 ns (M)[AC]
savectl reload UI construction yields bounded slices: ok
reload anonymous ownership delta=1318912 bytes (M)[AC], bound=2097152 bytes (G)
savectl reload mapping budget/overlapping snapshots: ok
savectl destroy ignores unrelated reused work slot: ok
savectl undo clean identity/branch/eviction/save cutoff: ok
savectl reload regular-to-FIFO rejects without writer: ok
savectl journal BASE_CHANGED exposes external recovery: ok
savectl_test: ok
restored-mtime reload: install=-1 file_error=5 first=o last=s modified=1
savectl restored-mtime check/keep identity: ok
savectl_reload_test: ok
```

Required fuzz command on the rebuilt binary, exit zero:

```
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/savectl_fuzz -max_total_time=60 -max_len=64
Done 24306 runs in 61 second(s)
```

Fuzz execution count and duration are (M)[AC], Charging; shared load1 was 23.13
(M)[AC]. An earlier run also exited zero: 21470 runs in 61 seconds (M)[AC]. No
ASan/UBSan diagnostics were emitted. Final module and fuzz logs are
build/edit-mdv-s9-final-module-san.log and build/edit-mdv-s9-complete-fuzz.log.
LeakSanitizer cannot run in this sandbox; all sanitizer and fuzz runs use
ASAN_OPTIONS=detect_leaks=0. Coordinator must rerun with leak detection enabled.
Only DISPLAY=:99 / EDIT_DISPLAY=:99 is used. Final standalone `DISPLAY=:99 EDIT_DISPLAY=:99 make -j8 all` exited zero:

```text
make: Nothing to be done for 'all'.
```

Log: build/edit-mdv-s9-complete-all.log. The final full check exited zero.
The command is `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j8 check`,
with approved access to private :99. Log: build/edit-mdv-s9-final-check.log.

## Notified-conflict diagnostic follow-up

The stronger §10 assertion also requires the original journal diagnostic after
an automatically queued file check. Before preserving the journal diagnostic
channel across file-only operations, `--journal-conflict` failed:

```text
tests/savectl_test.c:748: m.journal_error==JOURNAL_BASE_CHANGED && m.file_error==FILE_OK
```

Logs: build/edit-mdv-s9-10-notified-red.log and
build/edit-mdv-s9-10-notified-green.log. A fresh save or finish updates the journal
diagnostic; checks, keep and reload retain it. This is a completion of §10, not
an added finding.

## Full-suite regression caught and corrected

The forced full-suite run caught a §14 regression introduced by placing all
construction state in every tree: checkpoint copies also grew. That run exited
nonzero at the existing piece_mem_test. Its pasted red (live bytes are (M)[AC],
bounds are (G)):

```text
abort with earlier owner: live=69784 bound=69729.25
FAIL checkpoint_old_owner:167: (double)atomic_load(&m.live) <= limit
piece_mem_test: FAILED
```

The correction moves construction state into a temporary allocator-owned object;
a normal tree carries only a pointer. The object is freed on commit or rollback.
The existing memory test and savectl sanitizer suite then exited zero:

```text
abort with earlier owner: live=69496 bound=69729.25
abort with earlier owner: ok (transaction reservations reclaimed immediately)
piece_mem_test: ok
savectl every reload construction allocation: rollback/retry: ok
savectl full-mailbox terminal publication retry: ok
savectl_test: ok
```

Green logs: build/edit-mdv-s9-piece-memory-green.log and
build/edit-mdv-s9-post-memory-module-san.log. The build after this correction,
including the all target, exited zero; log:
build/edit-mdv-s9-memory-fix-build.log. The final full check on private :99 exited zero; see the final validation
result below. No unrelated failure was fixed.

## Remaining and limits

Do not close the entire bead: §§18–20 remain, and all required validation must
be assessed below. Resume at §18. Actual editor acknowledgement must include
bounded immutable session capture, off-path checkpoint construction and the
submitted saving status under large dirty sessions and busy backends. Then
implement §19's journal ownership stage transfer and §20's fresh-inode recovery.
No G8s end-to-end pass is claimed. This report contains no timing gate waiver.

## Final validation status

Release all and the final savectl sanitizer/reload suites exit zero. The final
post-diagnostic-fix fuzz run exits zero, with 24306 executions over 61 seconds
(M)[AC]. The optional existing UI-I/O/publication interposition configuration
also exited zero before the final construction-layout/diagnostic follow-ups;
log: build/edit-mdv-s9-io-test.log. `git diff --check` exits zero.

The final full suite exited zero, with the following pasted green:

```text
check: 59 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

The test count is (M)[AC]. Required completed-slice release, sanitizer and fuzz
checks are green. Coordinator must rerun with leak detection enabled. The entire
bead remains incomplete because §§18–20 have no red/fix yet; resume at §18. No Git writes, bd, HANDOFF.md, docs/worklog or Makefile
content changes were made.
