# edit-ck4 — session 9 worker report

Bead remains OPEN. This is a partial implementation of the specified P4-3
journal/recovery findings. Git was read-only; no bd, HANDOFF.md, or worklog edits.
No new globals. Production changes use reserved editor storage and per-instance
workers/controllers. Display work uses :99 only. No performance gate pass is
claimed from a single loaded-box timing.

## Per finding, in requested order

- **§3 — OPEN.** A forked editor probe proves Ctrl+S does not save. It remains
  opt-in (`--ck4-save`) and intentionally red. The editor still lacks the
  published immutable session root, retained source/storage leases, exact
  Saving-frame acknowledgement, all-target reconciliation and controller finish
  integration. The direct large-file save helper remains unsafe with a journal.
  No incomplete save wiring was installed.
- **§4 — PARTIAL.** Keep captures the accepted disk BASE and durably checkpoints
  the complete current session before clearing suspension; kept contents become
  dirty. Reload replaces buffer resources within the same logical tab and
  checkpoints without the obsolete BASE before allowing editing. ID, order and
  wait membership survive. Abrupt-child-exit tests recover selected, unrelated
  and retained closed buffers for COPY and mapped Keep/Reload. A failed
  checkpoint keeps the source suspended. **Remaining:** Reload of a COPY source
  whose replacement is still acquiring its remainder is refused and leaves the
  prompt; this needs a retained asynchronous replacement state. A complete
  checkpoint also refuses any other stale named BASE, so simultaneous source
  conflicts need a staged session-wide resolution plan. Editor-owned
  in-flight save-token preservation depends on the missing §3 integration.
- **§5 — DONE for current Linux generation identities.** BASE v2 persists ctime
  alongside size, mtime, inode, device and prefix checksum. New checkpoints and
  save BASEs share the encoder. Legacy named BASEs cannot match the current
  captured ctime and produce a conflict; legacy empty BASEs remain valid. The
  child exits without closing the journal, then the parent rewrites beyond the
  checksum prefix and restores mtime. Recovery rejects the BASE before any
  callback. Prefix capture uses the actual file-worker original identity.
  Savectl test/fuzz/bench wire producers were migrated with the schema; their
  workload/policy was otherwise unchanged.
- **§6 — OPEN.** An actual main-process stdin-session → SIGKILL → relaunch →
  SIGKILL probe remains opt-in (`--ck4-startup`) and red. Prior session discovery,
  restoring buffers/associations/views/tabs/window, conflict handling and durable
  retirement of recovered artifacts are not implemented. The confirmed red run
  used local Xvfb socket access; earlier sandbox connection failures are not
  treated as recovery evidence.
- **§8 — PARTIAL.** A private journal worker/pool and event source reserve
  durability execution independently of bulk index/find/save work. Pumping no
  longer depends on a clean frame. Insert, delete and metadata records use
  complete-record credit including eventual PAD; registration streams initial
  contents with the same protected admission. Large replay ranges drain in
  protected slices from the UI-owned tree, with input parked and retirement
  deferred until drained. No new allocation follows undo commit. A full frame
  cannot publish while its content or latest metadata remains unstaged.
  An independent durable byte image is exact with an occupied bulk worker and
  continuously dirty/busy frames; observed unsynced volume stays within
  64 KiB (G). **Remaining:** unconditional one-second (G) wall-clock guarantees
  under stalled kernel/device I/O are not established; source-open preparation
  still contains blocking setup, and per-slice syscall/CPU latency certification
  is not claimed. The private pool does not make Linux pwrite/fdatasync stalls
  bounded.
- **§7 — DONE for journaling, restoration still §6.** Normal frame submission and
  explicit flush coalesce VIEW/TABS/WINDOW records using reserved storage;
  accepted caches advance only after successful append. Metadata admission
  respects durability credit and delays a containing frame when necessary.
  TARGET persists intended association independently of content BASE, including
  missing named targets, and complete checkpoints retain it. Direct editor API
  relative names are preserved verbatim; cross-cwd restoration remains part of
  the missing startup integration. Healthy flush plus
  abrupt child exit verifies selection, tab order/active tab and dimensions.
- **§10 — PARTIAL.** Normal BASE preparation no longer independently opens or
  resolves the path in the editor. It consumes canonical identity and owned
  prefix produced by file's validated nonblocking acquisition, before prefix
  storage retirement. File exposes a small immutable prefix-identity accessor;
  that API gap is documented in the decision. Standalone journal capture also
  opens nonblocking. The journal-enabled FIFO/no-writer regression is green.
  **Remaining:** the editor/IPC request-preparation state machine itself is still
  synchronous and lacks a delayed-filesystem/resumable-request regression;
  fixed-size prefix CRC construction runs on the owner after worker acquisition.
  No complete asynchronous admission claim is made. The changes near prefix
  acquisition are limited to BASE capture/getter validation, for coordinator
  merging alongside edit-dnc2.
- **§32 — DONE at controller API level; editor UI integration remains §3.** File's
  replacement callback records replacement independently of its return code.
  A known post-rename directory-barrier failure retains a saved BASE and exposes
  needs_finish; finish syncs the target parent before reconciliation. Unknown
  replacement identity remains ambiguous and can resolve through explicit
  complete-CURRENT cancellation. `savectl_recover_cancel` /
  `journal_save_cancel` cover failed PREPARE, pre-rename failure and ambiguous
  post-rename failure without manual token deletion. CURRENT publication precedes
  retention retirement; cancellation stays dirty and never installs Saved.
  The failed-PREPARE regression also forced repair of an ambiguous journal
  namespace barrier before fresh rotation. Other live tokens/BASEs must remain
  in the caller's complete checkpoint, per the existing controller contract.
- **§33 — DONE at controller API level.** Setup retains a configurable writable
  cache-directory descriptor. Reload tries source-parent reflink backing, then
  private cache backing with bounded copying. Unsupported O_TMPFILE uses a
  random, exclusive, no-follow temporary file unlinked through the retained
  directory descriptor. Read-only-source-parent plus unsupported-O_TMPFILE
  coverage passes. No source-directory write access is required for fallback.

## Red runs (pasted)

The following are the actual pre-fix outputs, in scope order. Assertion line
numbers are identifiers; logical byte/count constants are test contracts (G).
Observed execution results are (M)[AC]. `--ck4-save` and `--ck4-startup` are still
intentionally failing opt-in probes and do not run in the passing default suite.

### save

```text
editor_test:1385: FAIL !memcmp(saved, "xa", 2)
editor_test:1398: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
```

### keep

```text
editor_test:1402: FAIL rc == JOURNAL_OK
```

### reload

```text
editor_test:1402: FAIL rc == JOURNAL_OK
```

### generation

```text
journal_test:1835 FAIL rc==JOURNAL_BASE_CHANGED && m.records==0
```

### startup

```text
waiting for display :99 (held by 142206)
editor_test:1451: FAIL rc==0 && model.len==7 && !memcmp(model.bytes,"unsaved",7)
```

### credit

```text
editor_test:1480: FAIL journal_get_stats(e->journal).unprotected_bytes<=JOURNAL_DEFAULT_SYNC_BYTES
```

### metadata

```text
editor_test:1530: FAIL meta.count==2 && meta.active==0 && meta.ids[0]==2 && meta.ids[1]==1
```

### target

```text
editor_test:1567: FAIL found
```

### fifo

```text
editor_test:1549: FAIL WIFEXITED(status) && WEXITSTATUS(status)==0
```

### directory

```text
tests/savectl_test.c:1081: savectl_get_model(f.s).needs_finish
```

### abort

```text
tests/savectl_test.c:331: savectl_recover_finish(f.s,current,2)==0
```

### prepare

```text
tests/savectl_test.c:1161: savectl_get_model(f.s).modified && !savectl_save_token(f.s)->previous_path[0]
```

### readonly

```text
tests/savectl_test.c:1105: rc==0 && replacement
```

## Green runs (pasted)

These are post-fix focused runs, (M)[AC]. Source-choice tests were strengthened
with unrelated and retained closed buffers after the initial COPY red runs.

### keep

```text
P4-3 section 4: mode=COPY choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
```

### reload

```text
P4-3 section 4: mode=COPY choice=Reload child abrupt exit; selected/unrelated/closed recovery exact
```

### mapped-keep

```text
P4-3 section 4: mode=mapped choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
```

### mapped-reload

```text
P4-3 section 4: mode=mapped choice=Reload child abrupt exit; selected/unrelated/closed recovery exact
```

### generation

```text
P4-3 section 5: abrupt child exit, beyond-prefix rewrite/restored mtime rejected before callbacks
```

### credit

```text
P4-3 section 8: complete-record credit bounded; independent durable image exact with dirty frames and occupied bulk worker
```

### metadata

```text
P4-3 section 7: healthy flush survives abrupt child exit with exact selection/tab order/active/window metadata
```

### target

```text
P4-3 section 7: missing named target association persisted independently of empty content BASE
```

### fifo

```text
P4-3 section 10: journal-enabled editor rejects FIFO without a writer; child completes before alarm
```

### directory

```text
P4-3 section 32: post-rename directory barrier failure reconciles through public recovery, no manual token deletion
```

### abort

```text
P4-3 section 32: pre-rename failure cancels through complete CURRENT checkpoint, no manual token deletion
```

### identity

```text
P4-3 section 32: post-rename identity failure remains ambiguous, explicit complete-current cancellation releases retention and stays dirty
```

### prepare

```text
P4-3 section 32: failed ambiguous PREPARE retains artifact until complete CURRENT recovery publishes durably
```

### readonly

```text
P4-3 section 33: read-only source parent and unsupported O_TMPFILE reload through private writable backing
```

## Verification and remaining acceptance

GCC is 13.3.0; clang is 18.1.3 (M)[AC]. Warning flags are the repository's
-Wall -Wextra -Werror -Wshadow -Wconversion. Final release `make all` and the focused sanitizer/fuzz target build exit 0.
The final full-suite run did not complete by the 40-minute (G) worker budget.
It incurred repeated shared :99 lock waits and reached journal_kill_test at the
last observation. Full `make check` acceptance is NOT established. The command
was approved for local Xvfb/Unix socket access and used leaks-disabled ASan/UBSan;
there was no automatic approval rejection. The coordinator must rerun the full
suite, including leaks on, before closing the bead. Focused final-tree ASan/UBSan savectl, editor
credit and mapped source-choice tests pass. The complete release journal suite
passes. The release editor counting guard reports:

```text
editor_test: null 10000 keys mallocs=0 guard=active
```

All numerical counts in the output above are (M)[AC]. The final build includes
all repository warning flags. `git diff --check` is clean.

The first full sanitizer check exited nonzero at the existing allocator-failure
replay regression after the new staging snapshot allocated following commit.
The implementation was corrected to borrow the UI-owned tree while input is
parked and buffer retirement is deferred. The focused sanitizer recovery sweep
then passed:

```text
review 22: allocator positions=22 failures=22; tree/view/history/journal rollback passed (M)[AC]
```

Final-tree fuzz campaigns completed cleanly on [AC], launch load1=1.20 (M).
Each used 60 seconds (G), max_len=16384 (G), timeout=10 seconds (G), fresh/seeded
worktree-local corpus storage, ASan/UBSan and leaks disabled. Exact final output
counts/durations are (M)[AC]:

```text
editor_fuzz: Done 16796 runs in 61 second(s); exit=0
journal_fuzz: Done 14365 runs in 61 second(s); exit=0
savectl_fuzz: Done 2322 runs in 61 second(s); exit=0
```

The earlier campaigns preceded the final staging/cancellation corrections and
are historical only; the runs above are against the final production code.

LeakSanitizer is disabled with `ASAN_OPTIONS=detect_leaks=0` because it cannot
run inside this sandbox; ASan and UBSan remain enabled. The coordinator must
rerun with leaks on. Native checks require local Xvfb/Unix socket access and
serialize through the repository display lock on :99. No real-display window
was opened. No new timing benchmark verdict is claimed. Full bead acceptance
is missing while §§3/6 and the stated partial integration requirements remain.

Design choices and the file API gap are in `docs/decisions/edit-ck4.md`.
No unrelated P4-3 findings were fixed. In particular Enter/brace indentation and
large-file prefix routing remain owned by the other workers. The narrowly
required wire-producer edits in savectl bench/fuzz are format migration only.

## Final worker handoff

The report and decision record were written before the worker budget expired.
No further feature scope was added after the scope freeze. Mandatory acceptance
remains incomplete: full make check did not finish within the budget, and §§3/6
plus the explicitly listed partial integration requirements remain open. All
three final module fuzz campaigns and focused correctness/allocation tests pass.
The header's final prefix-identity lifetime wording was corrected to "borrowed
until file_close"; no executable behavior changed by that documentation edit.
Final release rebuild and full-suite logs are worktree-local build artifacts.

The final release rebuild exits 0 (M)[AC]. Raw transcripts are retained under
`build/ck4-logs/`; that build directory is ignored. The full-suite process was
still running when this report was frozen, so its later log output must not be
read as a completed acceptance claim in this worker report.
