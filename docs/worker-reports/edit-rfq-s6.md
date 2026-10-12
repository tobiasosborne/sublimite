# edit-rfq — s6 worker report

Implemented startup retirement, age-ordered artifact eviction, and manual
set-aside replay in this worktree. Release build and the bead's tests pass.
Full `make check` is **not green**: sandbox socket/display restrictions and an
existing journal-kill test schema mismatch remain, detailed below.

No bd or git commands were run. No Makefile target was changed: its existing
tool/test/source globs build the additions. No typing-path work was added.
New startup/tool storage uses src/base arenas and pools; errors are return codes.

## Files touched

- `src/editor/recovery.c`: startup housekeeping before choosing recovery source.
- `src/journal/journal.c`: shared streaming parser gains read-only scan mode;
  existing recovery still validates BASEs and truncates damaged tails.
- `src/journal/journal.h`: scan, retirement and manual replay interfaces; derived cap.
- `src/journal/retire.c`: leases, complete reference inventory, identity/empty-log
  proof, crash-safe retirement and oldest-first eligible artifact eviction.
- `src/journal/manual.c`: selected-buffer dry run and atomic file replacement.
- `tools/journal_replay.c`: command-line interface.
- `tests/journal_retire_test.c`: headless integration, preservation, eviction,
  replay, failure-path and discovery measurement fixtures.
- `docs/decisions/edit-rfq.md`: choices and derivations.
- `docs/worker-reports/edit-rfq-s6.md`: this report.

## Safety and retention

Retirement holds the session lease, validates the entire read-only journal and
its saved BASE/TARGET identities, rechecks the source inode/timestamps, renames
with RENAME_NOREPLACE to `.retired`, fsyncs the directory, verifies the renamed
inode, unlinks, and fsyncs the directory. Dotted names never enter ordinary
session discovery. Crash-left retired journals are revalidated before removal;
an edited retired journal survives untouched. Stable lease files remain.

INSERT/DELETE, PREPARED SAVE, changed BASE/TARGET, corrupt tails and live leases
protect a journal. FINISHED SAVE metadata can accompany an empty eligible log.
Untitled/private-retained BASEs remain protected. BASE/SAVE references are
collected only after the complete artifact inventory exists, including
custom-named journals; directory enumeration order cannot hide a reference.
Uncertain inventories, child directories and symlinks preserve base copies.

The perf document contains no data-directory or on-disk storage budget.
(M)[AC], BAT0 `Not charging`: sealed empty fixture = JOURNAL_PAGE = 4096 bytes;
retained-original fixture = strlen("base") = 4 bytes. The fixture count is the
observed profile cohort, 32 (M)[AC] sessions. (E) policy: scale one measured
journal-plus-original fixture per observed session generation:
32 * (4096 + 4) = 131200 bytes. Eligible artifacts are evicted oldest mtime
first to that byte cap. This derives a storage cap, not an invented expiry
period. Protected edited journals and referenced originals can exceed the cap;
unsaved-data preservation takes precedence over a hard storage limit.

## Red runs

Commands used clang ASan/UBSan with `ASAN_OPTIONS=detect_leaks=0`.
(P) diagnostic numbers below are `__LINE__` source ordinals, enum return
codes and fixture buffer IDs; byte lengths derive from wire schema/strlen.
The startup test was written before housekeeping was implemented. With a
no-op retirement seam and startup wiring absent, the actual failure was:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/journal_retire_test
journal_retire_test:61 FAIL access(clean,F_OK)<0
journal_retire_test:142 FAIL retirement()==0
```

The manual replay test then failed against its no-op error-return seam:

```text
journal_retire_test:132 FAIL journal_manual_replay(path,base,0,false,output)==0
journal_retire_test:171 FAIL manual()==0
```

A custom-journal/reference test exposed the need to gather the complete
inventory before scanning, using an observed readdir ordering with its BASE
later in the directory:

```text
journal_retire_test:157 FAIL journal_retire_directory(root,NULL)==0 && access(base,F_OK)==0
journal_retire_test:245 FAIL custom_reference()==0
```

The differing TARGET regression was added before its identity validation;
the serial suite caught erroneous retirement of that pending target:

```text
== build/san/tests/journal_retire_test
journal_retire_test:207 FAIL (access(path,F_OK)==0)==(which!=MATCHING)
journal_retire_test:289 FAIL targets()==0
journal_retire_test: exit=1
```

Red evidence logs are `build/edit-rfq-{red,manual-red,order-red,target-red}.log`.
The earlier completed-SAVE regression is also in `build/edit-rfq-save-red.log`.

## Green runs and discovery measurement

Final release `make all`: success. Final compiler configurations use the
prescribed gcc release and clang ASan/UBSan warnings from the Makefile.
Actual targeted output, after all implementation changes:

```text
$ build/san/tests/journal_retire_test
(M)[AC] BAT0=Not charging empty_fixture_count=32 journal_bytes=4096 base_bytes=4 discovery_before_ns=997521 discovery_after_ns=178740 load1=23.47
journal_retire_test: passed
build/san/tests/journal_retire_test: exit=0
$ build/tests/journal_retire_test
(M)[AC] BAT0=Not charging empty_fixture_count=32 journal_bytes=4096 base_bytes=4 discovery_before_ns=1540848 discovery_after_ns=77603 load1=23.47
journal_retire_test: passed
build/tests/journal_retire_test: exit=0
```

The count derives from the observed profile cohort above. File sizes are
measured with stat and strlen. Discovery times are CLOCK_MONOTONIC differences
(P: nanoseconds per second = 1000000000), measured back to back on the same
fixture before retirement and after deletion. The discovery probe follows
startup's session-name filter, open/fstat and lease acquisition; these are
**discovery-only timings**, not total editor startup or retirement timings.
The second launch finds no retired session candidates. The power status is
read from `/sys/class/power_supply/BAT0/status` before the measurement; load is
read from `/proc/loadavg`. Release is the gcc run; sanitizer is the clang run.
No latency budget or repetition count was introduced.

Final targeted status lines:

```text
build/san/tests/journal_retire_test: exit=0
build/tests/journal_retire_test: exit=0
build/san/tests/journal_test: exit=0
build/san/tests/editor_session_test: exit=0
build/san/tests/editor_test --headless: exit=0
build/san/tests/editor_close_test --headless: exit=0
build/san/tests/editor_hot_exit_test: exit=0
```

Tests prove: clean journal disappears on startup; edited journal retains its
inode, size and timestamps during retirement; edited set-asides and edited
crash-left retired files survive; corrupt tails are not truncated; live leases
and base/target conflicts preserve sources; oldest orphan copies and eligible
set-asides are evicted while edited journals/referenced originals remain;
completed versus pending SAVE metadata is handled conservatively; late and
custom journal references protect originals; dry run leaves bytes unchanged;
apply writes the expected bytes; ambiguous buffers, invalid ranges, corrupt
sources and source/destination aliasing refuse writes. A child with a zero
file-growth rlimit (P: no permitted growth bytes) forces the temporary-write
failure and unlink path, proving the original target and edited source survive.
The executable CLI smoke also compared source journal bytes before/after both
modes and passed (`build/edit-rfq-cli.log`).

The required release journal bench ran on the box as-is, stamped (M)[AC],
BAT0 `Not charging` and load in `build/edit-rfq-bench.log`. Correctness passed;
a component append goal was a tracked miss, without a new budget or gate:

```text
journal_bench: correctness=ok mode=TRACK gate_misses=1 status=0 (complete G1/G9 endpoints require editor harness)
```

Full targeted output: `build/edit-rfq-green-final.log`.

## Full suites, remaining verification and outside-scope findings

`make check` was run. Its first attempt reached CLI setup and failed in
socket initialization. The final attempt used immediate display-lock refusal
(P: zero waiting allowed) because another display owner held the shared lane;
it stopped at CLI again. Logs: `build/edit-rfq-check.log` and
`build/edit-rfq-check-final.log`. The remaining binaries were exercised serially
with the same sanitizers, safe DISPLAY and no display-lock waiting; logs are
`build/edit-rfq-suites-final.log`. That broad pass includes the expected TARGET
red above; its final corrected green is in `build/edit-rfq-green-final.log`.

- Headless editor/session/close/hot-exit and journal suites pass after the final
  changes. The other non-display module suites passed in the serial broad run,
  **except `journal_kill_test`** below. `file_test` and `lineidx_test` initially
  failed their existing asynchronous watchdog checks under load; both passed
  the serial rerun. No unrelated file/index code was changed.
- `journal_kill_test` expects the legacy WINDOW payload, but unchanged
  `journal_set_window` emits the current WINDOW schema. (P) legacy is
  width32 + height32 = 8 bytes; current adds flags32 = 12 bytes. Its callback
  rejects that record before any edit replay. This pre-existing mismatch is
  outside this bead and was not fixed. Therefore the requested completely
  green non-display `make check` condition remains unmet.
- `ipc_test`, CLI and IPC-containing editor integration sections cannot bind
  their sockets here (`Operation not permitted`). Native raster/X11/reference
  suites fail for lack of the accessible X socket in this sandbox; some pure
  portions/optional GL and Wayland mock suites pass. Mixed `file_kill_test`
  fails in its native viewport setup. The coordinator must rerun socket/display
  suites outside this sandbox on Xvfb :99 (the coordinator's assigned display).
- LeakSanitizer itself reports that it cannot operate under this sandbox's
  ptrace restrictions. Leak detection was disabled; AddressSanitizer and
  UndefinedBehaviorSanitizer remained enabled. Leak checking needs an
  unrestricted coordinator rerun.
- Existing checkpoint encoders serialize DELETE/INSERT for full contents even
  for semantically clean nonempty buffers. Such older journals are deliberately
  retained under the requested **empty edit log** rule; their encoding was not
  changed. Edited set-asides need operator inspection/recovery and disposition,
  rather than expiry that would discard unsaved edits. Manual replay uses the
  explicitly supplied existing file's bytes as its starting point and prints a
  differing BASE identity; the operator must choose the intended original.
- Stable lease files remain infrastructure. Housekeeping is a shallow inventory;
  uncertain/cross-directory references conservatively prevent base eviction.
  No profile artifacts outside this worktree were inspected or modified.

No implementation work remains within the specified empty-log retirement and
manual replay scope. Coordinator verification and the unrelated legacy WINDOW
expectation remain outstanding. One bead handled; stopping here.
