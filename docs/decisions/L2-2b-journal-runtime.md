# L2-2b · journal runtime: append, transport, rotation, recovery (second half of L2-2)

Brief: `docs/briefs/L2-2b-journal-runtime.md` (bead v2-0fv.24). Read in full first: CLAUDE.md, `src/journal/journal.h`
(wire section and codec, L2-2a), [L2-2a-journal-wire.md](L2-2a-journal-wire.md), amendments A13, A14, A19, A20, A22,
A23, harvest §0 and §2, derivations Q3, Q6, Q7, q2-protocol, the piece, undo, os, work and base headers, and the v1
journal source, tests and bench at the oracle pin (read-only). The wire is unchanged: no amendment request arose. Built
against the piece model (`tests/piece_model.o` ahead of `libtext.a`), as undo and find were before 7b.

Landed: the runtime API in `src/journal/journal.h` (below the codec section), `src/journal/journal.c` (live writer),
`src/journal/recover.c` (recovery), `src/journal/internal.h` (module-private helpers), an incremental validator in
`src/journal/codec.c` (`journal_stream_init/next`; `journal_stream_validate` is now a loop of it), suites
`tests/journal_test.c` (san tsan), `tests/journal_kill_test.c` (san), `tests/law2_journal_test.c` (law2), the shared
fixture `tests/journal_fixture.h`, the fuzzer `fuzz/journal_recover_fuzz.c` with seeds `fuzz/corpus/journal_recover/`,
`bench/journal_bench.c`, and the lane and link rows in `tests/lanes.mk` and the Makefile.

## The module in one paragraph

**Invariant (writer):** a record is acknowledged only when one `os_file_pwrite_once` put all of it in the named
journal's page cache at the append position, and nothing is ever written after a byte range that could hold a partial
record unless that range is first rewritten whole or removed. **Invariant (reader):** recovery applies only the
committed prefix the codec certifies, applies a BASE-relative record only onto the verified bytes of its declared
generation, and never destroys bytes it cannot explain as an unacknowledged torn append. Everything else in the module
serves one of the two.

## Mechanisms: invariant, simplest alternative, defeating counter-example

1. **The UI writes every record; the worker only syncs (outside a lease).** Invariant: the append position has one
   owner at a time, so no hole can precede an acknowledged record. Alternative: v1's two batch buffers handed to the
   worker, which rewrote the missing tail of any short UI write and accounted the rest. Counter-example to keeping it:
   in v2 a held record never needs a batch (one record is held at most, because a held record suspends appends), and
   a worker that writes concurrently with the UI needs a reserved region; a kill while the region is incomplete leaves
   a hole, and every acknowledged UI record after the hole is outside the valid prefix. So: one descriptor, UI
   `pwrite_once` per record (Q6), worker `fdatasync` on the same descriptor (positioned writes and fdatasync may run
   concurrently; it is closed only with no job). No batch buffers, no copy of acknowledged bytes in RAM.
2. **The UI writes the PAD before each barrier.** Invariant (wire section 4): a synced boundary ends on a 4,096 B
   block, so a later append never rewrites a block holding durable records. The PAD sits at the append position, so
   only its owner can write it; the barrier job is submitted with (end, sequence) after it and the UI keeps appending
   behind it. A PAD write that fails is held like any record. Open also seals the recovered end with a PAD and fsync,
   so the first append of a session starts on a fresh block.
3. **Leases for every worker write into the image.** Gap repair, the self-contained checkpoint and rotation each take
   the append position for one job; appends return BUSY meanwhile. Invariant: the lease ends with the image either
   extended by whole records (adopted: end and next sequence move) or exactly as it was (a failed checkpoint
   truncates back to the grant offset; if even that fails the journal latches NEEDS_ROTATION). Counter-example to
   not truncating: a partial group of sequences b, b+1, ... left at the old end, then a UI GROUP_BEGIN written over its
   first 32 B with sequence b, makes the stale descriptor at b+1 a valid continuation.
4. **Two orthogonal state enums replace v1's seven booleans**, derived against `journal_retry` (v1 journal.c:575-591),
   the only function that cleared them:

   | v1 state | v2 |
   |---|---|
   | `active` (worker holds the batch) | transport SYNC or LEASE (who may write) |
   | `failed` (worker's job failed) | no state: the outcome is adopted into the suspension axis |
   | `append_failed` + `append_pending` + `pending_sequence` | suspension APPEND_GAP with the held record; appends BUSY until a repair lease rewrites it |
   | `writeback_failed` | suspension WRITEBACK_LOST; only a rotation from live state repairs it (no rewrite can prove the lost pages back) |
   | `directory_pending` | suspension DIRECTORY_PENDING after a rotation whose rename landed but whose directory barrier failed; a job retries it; appends BUSY |
   | sticky FULL error | suspension NEEDS_ROTATION (also sequence exhaustion and an unremovable lease write) |
   | `recovery_pending` | no state: recovery is a separate call; open continues a repaired image or creates a fresh one |

   v1's distinction "retry closes the gap / only a rotation repairs" is exactly BUSY versus SUSPENDED at the API:
   BUSY means the journal makes progress itself (retry the same record later), SUSPENDED means the caller must rotate.
   The transport gains RETIRING (result adopted, slot not yet physically finished), so a completion message can return
   the append position at once while the job struct stays the worker's until `work_handle_finished`.
5. **Completion without a drain loop or message handler (Q7).** The worker writes its outputs into the job, then
   publishes one wake-up message bound to the journal with `work_mailbox_bind`, so the application's ordinary drain
   delivers it to the journal and nowhere else. `journal_pump` also adopts on `work_handle_finished` (a dropped message
   cannot strand the journal), takes its own leftover message with selective receive, and unbinds. `journal_close`
   returns BUSY while a job runs: the caller pumps, the journal never waits.
6. **Recovery: validate, repair, plan, install origins, apply, offer literals.** Each step is a forward read of the
   committed prefix through one window of one maximum record (wire section 3's reader cost, 1 MiB), so memory does not
   grow with the image. Declarations are resolved in stream order (a use must follow its declaration; a conflicting
   redeclaration distrusts the key; VALUE_ONLY is checked at the use's position). The plan picks each buffer's origin
   (last BASE, or the committed checkpoint with the greatest covered sequence when not before it) *before* any BASE is
   opened (q2-protocol: "resolve ... before opening a BASE"), which is what lets a self-contained buffer recover after
   its BASE is gone. Apply skips every checkpoint group's slices (they are text, not edits, selected or superseded) and
   applies a group's members under one piece checkpoint per touched tree; a failing member aborts every tree the group
   touched (whole or not at all, across buffers).
7. **One validator.** The incremental `journal_stream_next` (codec) carries the group position as one explicit phase
   (OUTSIDE, GROUP_EMPTY, GROUP, CHECKPOINT, CHECKPOINT_TARGET) instead of L2-2a's four booleans, and
   `journal_stream_validate` is a loop of it. Alternative: a second, file-streaming copy of the group rules in the
   runtime. Counter-example: two copies of "a checkpoint group may end with one TARGET" drift, and recovery would
   certify bytes the codec refuses. The codec suite and its fuzzer pass unchanged; the recovery fuzzer checks
   recovery's prefix and last sequence against `journal_stream_validate` on every raw input.
8. **Torn tail versus damage (advisor).** TORN TAIL: the first failure is the input ending inside a record or inside an
   open group, *and no record that decodes at its own sequence starts anywhere after the failing header*; repaired in
   place (truncate to the valid prefix, fsync). ANYTHING ELSE: the whole file is copied aside byte for byte (synced,
   directory synced), then a new image holding exactly the valid prefix replaces the name. Counter-example to the
   simplest rule ("TRUNCATED means torn"): a length field flipped to claim past EOF turns a damaged record with
   acknowledged records behind it into "TRUNCATED"; truncation would destroy them. The scan is diagnostic only
   (nothing found is applied; wire section 8 forbids resynchronising) and runs only on a tail under one maximum record
   (a TRUNCATED record claims at most 1 MiB). Why copy, not rename: with rename-then-recreate there is an instant with
   no journal under the name; copying first keeps a complete image named at every instant. A damaged image that cannot
   be copied is left untouched (DAMAGED) and `journal_open` refuses to append to it. A refused preamble is renamed
   aside (nothing valid to keep) and the session starts on a fresh image.
9. **BASE identity in every path (A23).** A BASE origin and a by-value restore source are opened by declared location
   (NOFOLLOW, NONBLOCK so a FIFO cannot hang recovery), checked against the 48 B identity projection and the prefix
   witness before reading, and the identity again after the bytes were used. A RESTORE_BASE_RANGE goes by reference
   only when its key is the buffer's verified ORIGINAL (Q3). Anything else is UNRESOLVED_BASE: no record is applied to
   that file and every literal the journal holds for the buffer after its origin is offered (T5).
10. **Rotation shapes.** BY_VALUE buffers become a CHECKPOINT_BY_VALUE group (covered = the sequence before its
    BEGIN). BY_REFERENCE buffers become BASE, then v1's checkpoint-diff shape generalised for A14(c): kept ORIGINAL
    spans in increasing order cost a DELETE of the gap, ADD spans go by value, an out-of-order or repeated ORIGINAL
    span goes as RESTORE_BASE_RANGE, or by value when its generation is VALUE_ONLY (A23 rule 3). The image ends with a
    PAD, is synced, renamed over the journal, and the directory synced.
11. **Checked allocations.** journal.c: one arena sized (P) as the struct plus two record buffers and two payload
    buffers plus five alignment paddings; each allocation from it is ASSERTed with that derivation beside it.
    recover.c: one arena of (P) 4 windows + 32 x the file (every record is at least 32 B, so at most file/32 records,
    each adding at most one table entry under 256 B, doubled by growth, plus copies each no larger than their record),
    reserved lazily; every allocation from it is checked and returns JOURNAL_ERR_NOMEM, with nothing published. The
    default source opener reserves exactly the file's size. v1's unchecked sites (harvest §2.6) have no counterpart.

## The two derived sizes (written beside the constants in journal.h)

**INSERT slice: 256 KiB.** v2's reason: one `journal_insert_step` call is one unit of UI work between input checks;
the UI slice is 0.5 ms (G, v1 perf G1 row); the call gets half (250 us). Measured (M)[AC] load1 3.09, 2026-11-24
01:17 CET, n = 1,000: one 256 KiB insert_step p99 212 us; records of 256 KiB p99 196 us, 512 KiB p99 368 us. Largest
power of two within 250 us at p99: 256 KiB. v1's 16 KiB was an inherited (G) ceiling.

**Self-contained size: 256 KiB.** The lease delays every append by its length. Budget: the same half UI slice at p99.
Measured lease (start to adoption) (M)[AC] load1 3.08, 2026-11-24 01:17 CET, n = 1,000, piece model: p99 244 us at
256 KiB, 470 us at 512 KiB (498 us at load1 2.41), 921 us at 1 MiB; about 1.25 B/ns plus a ~60 us scheduling floor.
Largest power of two within 250 us at p99: 256 KiB. v1's whole-text/delta split at the pin was 1 MiB
(`FILE_PREFIX_MAX`, v1 file.h:86; edit-28u decision 5), tied to its synchronous-open prefix rather than a write budget.
Under writeback the 4 MiB lease's p99 reached 12.6 ms and 20.7 ms in two runs (dirty throttling): the small size is not
only within budget but out of the regime where large leases blow up.

## Measurements (A22)

**Typing append** (M)[AC] load1 2.75: 1 B insert_step p50 509 ns, p99 1,866 ns, max 115 us (n = 100,000, barriers by
v1's 64 KiB / 1 s policy). Run at load1 3.09: p50 883 ns, p99 3,416 ns.

**Measurement 1, input-to-submit with the append** (Q6; `journal_bench --g1`): undo_begin/replace/commit of one key
plus the journal append, release build, live malloc guard (active=1 count=0 in every scenario), one key per 2 ms,
n = 10,000 per scenario, piece model. Writeback load: a child sweeping a file of 5 GiB (P: above the 4.82 GB dirty
limit = dirty_ratio 20 % of MemAvailable) with 1 MiB writes and no sync; the file is unlinked as soon as it is open;
df checked first (fixture + 4 GiB margin), child killed and reaped.

| run | scenario | input-to-submit p50 / p99 / max | of which append p50 / p99 / max |
|---|---|---|---|
| (M)[AC] load1 2.72, 01:17 CET | ordinary | 9.8 us / 34.1 us / 330 us | 1.2 us / 9.4 us / 258 us |
| (M)[AC] load1 3.58 | writeback (Dirty 1.1-1.6 GB, Writeback 0.1-0.5 GB) | 37.9 us / 154 us / **153.7 ms** | 5.7 us / 24.1 us / **153.6 ms** |
| (M)[AC] load1 5.68, 01:18 CET | ordinary | 9.6 us / 29.3 us / 314 us | 1.1 us / 7.4 us / 46 us |
| (M)[AC] load1 5.54 | writeback (Dirty 1.6-1.8 GB) | 54.1 us / 124 us / **97.7 ms** | 8.5 us / 29.4 us / **97.6 ms** |

G1's gate (input to present, p50 1.0 ms / p99 2 ms) is not failed at p99 by the append: 0.12-0.15 ms under writeback.
**The maximum is**: in both writeback runs one key's `pwrite_once` blocked for ~100-150 ms. That is Q6's stated risk
(Linux regular-file writes can block; the attempt bound is no time bound) observed. Per A22 the comparison is a
preallocated shared mapping of the journal segment; reported, not built (out of scope).

**Measurement 2, 10 GiB recovery** (Q3; `journal_bench --recovery-10g`), (M)[AC] load1 6.00, 01:19 CET, piece model.
BASE: 10 GiB (G, the PLAN large-file class), sparse with its first MiB written; df demanded its full size + 16 MiB +
4 GiB margin. Second generation: 16 MiB, written. Image (319,520 B): BASE + 1,000 DELETEs of 1 MiB, 1,000 by-reference
restores of 1 MiB from 16 repeated BASE ranges, 1,000 by-value restores of 64 KiB from the second generation, 1,000
inserts of 100 B; mapped opener (os_map_file + os_fault_register + piece_open_mapped).

| quantity | value |
|---|---|
| verdict, time | CLEAN, 142 ms |
| text | 10,803,054,240 B, equal to the script's model length |
| ADD | 65,636,000 B = 65,536,000 by value + 100,000 inserted (P: by-reference restores, 1,048,576,000 B, took none) |
| recovery scratch | arena used 1,121,392 B (window 1 MiB + source chunk + prefix + tables), reserved 14,418,944 B |
| RSS peak (VmHWM) | 3,268 kB before, 70,468 kB after (ADD + scratch + model) |
| temporary disk | none created (CLEAN: no repair, no copy); statvfs moved 28,672 B, other writers on the shared filesystem |
| piece nodes | n/a: the model is a flat span array; rerun with 7b |

Not yet done from Q3's outstanding list: byte-exact verification at 10 GiB (length only), retry after insufficient
memory, the file-backed alternative.

## Acceptance, red and green

Red first: `tests/journal_test.c` was written against the API before any runtime existed; the red run was a link
failure on every runtime symbol (make exit 2, 2026-11-24 00:51 CET, `build/red-journal_test.log`). The kill, Law-2 and
fuzz suites came after the runtime; their reds are mutation runs (below): the suite fails against a deliberately
broken runtime, then passes against the real one.

| acceptance line | test | red | green |
|---|---|---|---|
| PLAN §4.2 oracle: kill at AFTER_BEGIN / AFTER_DELETE / BEFORE_COMMIT / AFTER_COMMIT, barrier inside the group | journal_kill_test `test_group_boundaries` | mutation M2 | pass |
| before ack pre-edit or whole, after ack whole; hot-exit phases (cache, durable, torn insert, torn slice, torn commit, rotation before/after rename) | journal_kill_test `test_phases`, `test_campaign` (50 SIGKILL trials) | mutation M2 | pass |
| unknown version / required feature refused, set aside, not truncated | journal_test `test_preamble_refusal` (also foreign, bad CRC, torn preamble) | mutation M1 (v1-style truncating scan) | pass |
| torn tails per record class | journal_test `test_torn_tails` (insert, in group, commit, open group, VIEW, PAD, header) | link | pass |
| repair: torn final record in place; flipped bit with valid records after set aside byte-identical, session continues | journal_test `test_damage_set_aside` (and a length field corrupted past EOF) | link; M5 not run (PAUSE) | pass |
| A23 T5: foreign replacement or deletion of BASE -> unresolved, literals offered | journal_test `test_t5_unresolved_base` | link; M4 not run (PAUSE) | pass |
| self-contained buffer recovers after its BASE is deleted or replaced | journal_test `test_self_contained` (incl. an edit between snapshot and group) | link; M6 not run (PAUSE) | pass |
| typing path under the release malloc guard, active && count == 0 | law2_journal_test (1,000 keys, delete, restore, 3-slice grouped insert) | mutation M3 | `active=1 count=0` |
| Measurement 1 | journal_bench --g1 | n/a | above |
| Measurement 2 | journal_bench --recovery-10g | n/a | above |
| check-tsan on the journal suites | journal_test, journal_codec_test | n/a | see lane run |
| fuzz beyond decode | fuzz/journal_recover_fuzz.c, 12 seeds; 15,598 runs in 90 s without failure (one harness property fixed: a cut inside the BASE record leaves no buffer) | n/a | fuzz-smoke |

Also covered: append-gap latching for failed, short and zero writes (held record rewritten exactly once), a failed
barrier latching WRITEBACK_LOST until a rotation, rotation of a by-reference buffer with repeated and reordered ranges
and a by-value buffer with TARGET, VIEW and TABS, mixed-generation restores (reference and value) and an out-of-range
restore refused, four-slice inserts, FULL latching NEEDS_ROTATION, and id allocation continuing past the image.

## Out of scope, reported (not fixed)

1. **G1 maximum under writeback** (Measurement 1): single appends blocked 97.7 ms and 153.6 ms. Candidate bead: the A22
   comparison, a preallocated shared mapping of the journal segment, measured the same way.
2. **SAVE resolution is L2-3's.** Recovery keeps the old replay for every buffer and ignores PREPARED/FINISHED; the
   identity check makes that safe (a renamed-over target reports UNRESOLVED_BASE with literals, never wrong text) but
   not exact for T1's RENAMED rows. Keys SAVE allocated still raise the high-water.
3. **Abandoned temporaries.** A kill inside a rotation before its rename leaves `<name>.rot-*` (the kill suite asserts
   it); a kill inside fresh-image creation leaves `<name>.new-*`. Never the only copy of anything; startup cleanup
   belongs with session discovery (L2-3 / L5).
4. **Rotation holds the lease for its whole write**, proportional to by-value text and ADD bytes of by-reference
   buffers; appends are BUSY meanwhile. A catch-up rotation (worker writes from snapshots while the UI appends to the
   old image, UI copies the short tail and renames) removes that, at real complexity; worth it only if a trace shows
   rotation lag.
5. **v1's durability credit is not carried** (insert_step refusing beyond `sync_bytes` of unsynced bytes): v2's M
   promise is "through the last barrier" by time and bytes policy, and a paste no longer stalls on durability. Say
   so if machine-loss exposure needs a byte bound.
6. **The edit kernel must group multi-slice inserts** (stated at `journal_insert_step`): slices are separate records.
7. **The torn-tail scan is quadratic only on adversarial tails** (many fake magics, each with a long claimed length,
   all inside one sub-1 MiB tail).
8. **Recovery fuzzer RSS** grew to 168 MB over 15,598 executions; not traced (libFuzzer's corpus, or a reservation the
   model keeps). Worth one look under the real tree.
9. **Measurement 2 with 7b**: node counts, and the model's span-array costs replaced by the tree's.
10. Re-derive both 256 KiB sizes when the real tree lands (the lease reads the snapshot through piece_iter) and once on
    battery.

## Mutation reds (Law 9 evidence for suites written after the runtime)

Each mutation is applied by a scratch script, the named suite rebuilt and run (red recorded), the source restored
byte-identical (asserted) and the suite rebuilt and re-run green. Run 2026-11-24 01:25 CET:

- M1 recovery truncates a refused-preamble file to 0 instead of renaming it aside (v1's truncating repair at its last
  valid byte, which for a v2 file is byte 0): journal_test exit 2, `FAIL r.set_aside[0] != 0: unknown-version`,
  `FAIL ... name freed`. Restored: PASS.
- M2 recovery takes the open group at EOF as committed (valid = stream end): journal_kill_test exit 134,
  `FAIL ... after-delete: before`, then the piece model's ASSERT on a checkpoint left open. Restored: PASS.
- M3 one malloc in the UI write: law2_journal_test exit 1, `active=1 count=1007`. Restored: `active=1 count=0`.
- M4 (skip source verification; T5 must fail), M5 (every TRUNCATED torn; the bad-length case must fail) and M6 (BASE
  always beats a checkpoint; the self-contained case must fail) were **not run**: the coordinator's PAUSE arrived
  after M1-M3. Their acceptance lines are red only by the link failure of the first journal_test build.

## Status at the PAUSE (2026-11-24 01:26 CET, coordinator quota stop)

**Compile state:** `HOME=$PWD/build/home nice make -j2 all` exits 0 (log `build/make-all-pause.log`), after the
mutation script restored every source byte-identical (no `MUTATION` marker remains in src/journal/).

**Done:**
- Runtime API, writer, recovery, incremental validator (codec), both state enums, checked allocations, both derived
  sizes with their derivations beside the constants.
- Suites: journal_test (10 cases), journal_kill_test (group boundaries, 7 phases, 50-trial campaign),
  law2_journal_test, recovery fuzzer with 12 seeds, bench with both A22 measurements (results above).
- Lanes, all green, 2026-11-24: `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:120-:129` exit 0 (01:24 CET;
  check-san 33 binaries, check-law2 8 suites, contract harness PASS); `nice make -j2 check-tsan` exit 0 (12 threaded
  suites incl. journal_test and journal_codec_test); `nice make -j2 fuzz-smoke` exit 0 (16 fuzzers incl.
  journal_recover_fuzz). One 90 s fuzz run: 15,598 executions, no failure.
- Mutation reds M1-M3 (above).
- Lines (against the v2 checkout at 3f0d196 for modified files): journal.h +429 -0 (later also a one-comment wording
  fix), codec.c +117 -104, Makefile +12, tests/lanes.mk +4; new files 4,796 lines (src 2,294, tests 1,421, fuzz
  333, bench 541, this doc), 12 corpus seeds.

**Missing (the bead resumes here):**
- Mutation reds M4, M5, M6 (script: the scratch `mutate.py` is not in the tree; the three anchors and expected
  failures are listed above, each one sed-sized).
- After any further change: re-run the three lanes. They ran on this tree except one later comment-only edit in journal.h (the journal_checkpoint_start wording); `make all` passed after it.
- Scratch to tidy under build/ (all inside the checkout): `build/fuzz-work/` (a fuzz corpus copy, its log and an
  empty `build/` the fuzzer created relative to that cwd), bench and lane logs `build/*.log`.
- No worklog entry or HANDOFF.md update: session state is the coordinator's.
