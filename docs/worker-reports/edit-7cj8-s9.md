# edit-7cj8 worker report

Only this bead was worked. No `bd` or `git` commands were run; the main
checkout and other worktrees were not accessed. The forbidden project
documents were not edited. Design: [edit-7cj8](../decisions/edit-7cj8.md).

Exit codes below are (P) process statuses: zero means success, nonzero means
failure. Fixture sizes follow existing constants or literal lengths (P).
Battery `Charging` and `Full` identify the following measurements as [AC].

## Result

- An observed external BASE change can now trigger a complete checkpoint
  from the still-live original, using the existing off-thread session writer.
  It writes empty BASE + TARGET + full snapshot, then adopts the empty BASE
  only after durable rotation. Source leases validate mapped bytes before
  and after encoding; the file header documents this read-only use of the
  existing retired-inode validator.
- This covers originals above the retention limit after rename-over and
  private COPY originals rewritten before asynchronous retention finishes.
  The copy-window test holds the first copy write, rewrites the source, and
  lets retention fail validation before the independent checkpoint succeeds.
- Deferred recovery reserves the actual INSERT storage in its buffer arena
  and replays empty BASE independently of the current TARGET contents.
- Failed-save cancellation already uses a self-contained CURRENT checkpoint
  in this tree. The save-controller test now rewrites TARGET again after
  cancellation and verifies replay without another checkpoint. Failed saves
  retain their prepared token until reconciliation.
- Asynchronous copies use unjournalled staging names under a shared directory
  lease. Promotion validates and persists their final identity. Housekeeping
  removes dead staging files even below the existing artifact cap; live
  copies exclude housekeeping. Completed retained copies keep the existing
  reference and cap rules.

## Files touched

- `src/editor/retain.c`, `session.c`, `private.h`: detect an unprotected stale
  BASE, capture independent source leases, encode/adopt full checkpoints,
  retry staging admission after a housekeeping lease refusal, and yield
  automatic publication to a queued Reload/Keep.
- `src/editor/restore.c`, `buffers.c`: reserve replay INSERT storage and
  discard TARGET's current contents when replay begins from empty BASE.
- `src/file/file.h`: document retired-source validation for independent
  read-only recovery leases; save authorization is unchanged.
- `src/journal/journal.c`, `journal.h`, `retire.c`: asynchronous staging,
  directory lease, durable promotion, uncapped dead-staging cleanup.
- `tests/dataloss_test.c`: sparse above-bound rename-over/crash/recovery and
  pre-READY COPY in-place rewrite regression, evicted graph branch, held-save
  Reload/Keep race, and step-error diagnostics, with wrapper teardown.
- `tests/savectl_test.c`: replay after cancellation and another external rewrite.
- `tests/journal_retire_test.c`: actual copying-process crash below the cap,
  live-owner exclusion and dead-owner cleanup.
- `tests/editor_test.c`, `editor_session_test.c`: crash-fixture teardown
  includes the new unjournalled staging prefix.
- `docs/decisions/edit-7cj8.md`, this report.

## Red run

Built with `make -j3 build/san/tests/dataloss_test` (exit 0). Focused null
backend command: `ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99
build/san/tests/dataloss_test --detached-rewrite` (exit 1, before production
changes). The sparse original is the smallest excluded generation (P):
`JOURNAL_STARTUP_SCAN_BYTES + sizeof "x" - 1` bytes.

```text
Charging
 03:32:49 up  1:58,  1 user,  load average: 22.33, 26.80, 24.17
dataloss_test: edit-7cj8 detached checkpoint=missing
dataloss_test: edit-7cj8 recovered original=lost modified=0
dataloss_test:854: FAIL editor_length(e) == bytes + (sizeof "XY" - 1u)
```

Before changing asynchronous temp creation/retirement, built
`build/san/tests/journal_retire_test` and ran its `--partial-copy` selector
(exit 1). The actual first successful write is nonempty and no larger than
`JOURNAL_ARTIFACT_BYTES` (P, checked by the test).

```text
Charging
 03:40:41 up  2:05,  1 user,  load average: 27.39, 25.15, 23.85
journal_retire_test:598 FAIL removed && live==JOURNAL_BUSY
edit-7cj8: partial copy live=unleased dead=left behind (below artifact cap)
```

The copy-window regression was then extended to evict the sole leaf in a
sibling session branch (P, minimal binary graph). This exposed an error in
the new validator's initial leaf discriminator (exit 1):

```text
Full
 04:00:03 up  2:25,  1 user,  load average: 16.65, 18.31, 22.05
src/editor/session.c:386:49: runtime error: member access within null pointer of type 'editor_buffer' (aka 'struct editor_buffer')
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior src/editor/session.c:386:49
```

The traversal now uses the published leaf's buffer pointer, which also
handles internal branches emptied by eviction.

The first complete sanitizer editor run reached its startup crash-fixture
teardown and exited 1 after all recovered-text assertions passed:

```text
editor_test:3422: FAIL rmdir(root) == 0
editor_test:5669: FAIL ck4_startup() == 0
```

The directory held only the new staging file, with length `sizeof "base" - 1`
(P, the fixture's literal). Teardown now includes that prefix in both editor
fixture helpers. The failed fixture directory was removed.

The complete data-loss suite also exposed automatic publication racing an
already queued Reload/Keep after a held save. The focused `--reload-held`
case now uses the existing zero-limit seam to keep copies unavailable until
save owns the journal (P, disabled retention). Before the ordering fix it
exited 1:

```text
Full
 04:18:29 up  2:43,  1 user,  load average: 3.50, 6.33, 11.93
dataloss_test: step error=-2 journal=0 active_BASE=[/home/tobias/Projects/sublimite/.wt/edit-7cj8-s9/build/dataloss-4BpHAC/other]
dataloss_test:59: FAIL turn(e) == 0
dataloss_test:333: FAIL saved(e) == 0
```

Debugger inspection located the error in painting the still-stale unbound
layout while the automatic publication lease delayed the explicit choice.
Publication now yields to queued Reload/Keep. Only debugger runs disabled
LeakSanitizer (its ptrace restriction); final verification keeps leaks on.

## Green run

Same leak-enabled null-backend recovery command (exit 0):

```text
Charging
 03:34:59 up  2:00,  1 user,  load average: 18.22, 23.62, 23.33
dataloss_test: edit-7cj8 detached checkpoint=durable
dataloss_test: edit-7cj8 recovered original=complete modified=1
```

`build/san/tests/dataloss_test --retain-window` (exit 0, leaks enabled):

```text
Charging
 03:38:34 up  2:03,  1 user,  load average: 27.25, 23.32, 23.11
dataloss_test: edit-7cj8 copy window checkpoint=durable
dataloss_test: edit-7cj8 copy window recovered original=complete modified=1
```

The deterministic `--reload-held` selector then passed both Reload and Keep
(exit 0, leaks enabled):

```text
Full
 04:22:53 up  2:48,  1 user,  load average: 11.18, 7.57, 10.88
```

`build/san/tests/journal_retire_test --partial-copy` (exit 0, leaks enabled):

```text
Full
 03:44:09 up  2:09,  1 user,  load average: 32.73, 28.82, 25.58
edit-7cj8: partial copy live=protected dead=removed (below artifact cap)
```

The same copy-window command with the eviction fixture after correction
(exit 0, leaks enabled):

```text
Full
 04:00:53 up  2:26,  1 user,  load average: 13.02, 17.06, 21.43
dataloss_test: edit-7cj8 copy window checkpoint=durable
dataloss_test: edit-7cj8 copy window recovered original=complete modified=1
```

## Verification

`make -j3 all`: exit 0, gcc with the repository's C11 warning flags and
`-Werror`. Its first attempt exited 2 because gcc diagnosed an unchecked
pipe `write` in the new test hook; that hook now handles the error.
The release build and all named sanitizer targets were rebuilt successfully
after the final Reload/Keep ordering correction (exit 0). Sanitizer build:

```sh
make -j3 build/san/tests/editor_test build/san/tests/editor_session_test build/san/tests/editor_checkpoint_test build/san/tests/dataloss_test build/san/tests/journal_test build/san/tests/savectl_test build/san/tests/journal_retire_test build/san/tests/file_test
```

Each complete sanitizer suite ran with the brief's display/timeout settings,
leak detection, and a fresh private runtime directory, using this command
pattern; the wrapper removed the runtime directory afterward:

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/<name>
```

| Final binary | Exit |
|---|---|
| `build/san/tests/editor_test` | 0 |
| `build/san/tests/editor_session_test` | 0 |
| `build/san/tests/editor_checkpoint_test` | 0 |
| `build/san/tests/dataloss_test` | 0 |
| `build/san/tests/journal_test` | 0 |
| `build/san/tests/savectl_test` | 0 |
| `build/san/tests/journal_retire_test` | 0 |
| `build/san/tests/file_test` | 0 |
| `build/tests/editor_test` (release malloc guard, same display lock) | 0 |

Release typing-path `mallocs=0` (M)[AC], observed by the active allocator
guard. The final release run's stamp and completion:

```text
Full
 04:29:05 up  2:54,  1 user,  load average: 8.23, 9.60, 10.91
editor_test: all passed
exit=0
```

Final data-loss regression output, with leaks enabled:

```text
Full
 04:28:06 up  2:53,  1 user,  load average: 9.64, 10.14, 11.16
dataloss_test: edit-7cj8 detached checkpoint=durable
dataloss_test: edit-7cj8 detached recovered original=complete modified=1
dataloss_test: edit-7cj8 copy window checkpoint=durable
dataloss_test: edit-7cj8 copy window recovered original=complete modified=1
exit=0
```

`build/bench/journal_bench --track`
also exited 0 with correctness OK. Its larger-payload append row missed its
existing tracked component goal under the stamped shared load; this is a
note, not a new performance task. Raw stamped output:
`build-edit-7cj8-journal-bench.log`.

The default editor typing-bench run was stopped (exit 143, P: SIGTERM) to
release the shared display to the required suites. Its temporary source
copy and runtime directory were removed by its shell trap. After all required
checks passed, the existing serial-only mode was run on a temporary copy of
`src/editor/retain.c` (P, current source bytes):

```sh
build/bench/editor_bench --track --no-idle --no-p4 --serial-only --file=<temporary-source-copy>
```

That run completed the null-backend serial rows with structural checks OK
and no allocations. Its null injection-to-T4 row missed the existing tracked
G1 goal under its stamped shared load. It had not completed the raster row
when stopped (exit 143, P: SIGTERM); native rows remain unmeasured. No native
performance investigation was added to this bead. Available stamped data:
`build-edit-7cj8-editor-bench-serial.log`. The fixture and runtime directory
were removed by the shell trap.

GB fixtures in the required suites use their existing sparse constructors
and wrapper cleanup. Raw `df -h /` (M)[AC] around the GB suites:

```text
editor_session_test before:
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  397G   59G  88% /
editor_session_test after:
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  397G   59G  88% /
editor_checkpoint_test before:
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  397G   59G  88% /
editor_checkpoint_test after:
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  397G   59G  88% /
```

The worktree fixture-size audit found no remaining GB fixtures. Completed
red/debug-run fixture directories were removed. The journal bench exited
cleanly and removed its temporary inputs. No editor-bench or data-loss
fixture remained after the final cleanup check. Generated verification
driver/state files are under `build/`; raw test/build logs are the root
`build-edit-7cj8-*.log` files.

Final cleanup stamp and `df -h /` (M)[AC]:

```text
Full
 04:39:25 up  3:04,  1 user,  load average: 11.64, 11.71, 11.35
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  398G   58G  88% /
```

Sandboxed LeakSanitizer could not inspect threads and emitted its fatal
ptrace diagnostic. Leak-enabled runs therefore use the approved execution
outside the sandbox. Focused null-backend runs above do not access X;
display-locked runs use the brief's DISPLAY and lock-timeout settings.

## Remaining and limits

- There is still a crash window before source-change detection and durable
  publication. A permanently stalled journal worker prevents the existing
  quiescent-journal publication gate from opening.
- An in-place rewrite/truncate of a mapped original before retention has
  preserved its bytes remains unrecoverable; the new worker refuses that
  source instead of persisting changed or fault-filled text. Private COPY
  originals do survive that case.
- Complete worker publication retains the existing journal lease, so input
  parks until rotation finishes, as for the existing worker checkpoint.
- Full-checkpoint payload and journal storage scale with the current text
  (P, complete INSERT payload plus existing record overhead); no new memory
  or disk budget was chosen.
- The cited `edit-jy6` decision document is absent from this worktree.
- Native editor benchmark rows remain unmeasured as described above. The
  mandatory release typing-path allocation guard passed.

## Outside this bead's scope

- Legacy partials already named `.sublimite-base-*` cannot be distinguished
  from complete unreferenced retained generations by filename. Synchronous
  save-prepare retention still uses that namespace. Their existing reference
  inventory and artifact-cap policy is unchanged.
- The predecessor's externally deleted-file recovery case is unchanged.
