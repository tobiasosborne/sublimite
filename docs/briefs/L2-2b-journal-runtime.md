Bead L2-2b: the L2 journal runtime on A28. It covers the UI single-writer append with fair service, worker encoding and
barriers, recovery materialisation into piece, repair, writer-owned rotation, exceptional recovery, the self-contained
size, and two measurements.

Rewritten 2026-11-24 on A28; the advisor reads this brief before dispatch. sublimité v2 is a clean-slate C11 editor
rebuild. Your checkout is a worktree of /home/tobias/Projects/sublimite-v2 (branch v2). One worker, one bead: do exactly
this scope, then stop. Routing: Claude or Muse, because the tests kill processes and tear writes; never Codex.

Depends on:
- the wire amendment + pure two-pass reader bead (A28 N2, Sol), landed on v2. The record grammar, the reader's logical
  plan and the PAD seal come from there; this bead does not re-derive them.
- 7b (landed, 1.23: the real piece tree).
- the undo replay erratum (v2-bmw: owner-called commit/abort), for the bracketed commit.

STARTING POINT: the wip at a94af15 (branch t3/journal-runtime, worktree
/home/tobias/.t3/worktrees/sublimite-v2/t3-de932866-2c152938cbe90882). Restart from it ONLY where the KEPT list below
applies. Everything else is deleted, not adapted (advisor). Astra's §4 of
docs/decisions/wave1-journal-write-model.md names the symbols.
- KEPT:
  - the single-attempt UI append (`ui_write`: one full positioned os_file_pwrite_once per record before ACK; on a short,
    zero or failed write, the exact held bytes and offset are kept and recovery suspension latches);
  - the exact-byte retry (now routed through the UI);
  - the off-thread sync through a captured watermark;
  - one UI dispatcher;
  - retention of a record until physical retirement;
  - version and required-feature refusal with the image set aside intact;
  - CRC and global-sequence checks;
  - damage set-aside;
  - generation verification, verified-ORIGINAL versus by-value replay, and unresolved literals;
  - the stand-alone kill, damage, source-validation and allocation test cases.
- DELETED:
  - the global group state (`j->group`, `admit`, the ownerless journal_group_begin/commit);
  - rollback or truncation to BEGIN;
  - leased worker writes and worker truncation (`lease_admit`, JOURNAL_TRANSPORT_LEASE, `run_checkpoint`'s write path,
    JOB_REPAIR's worker rewrite);
  - contiguous checkpoint replay (`in_checkpoint`, the one pending descriptor, the global touched-tree group);
  - the worker-side rotation switch (`run_rotation`'s rename and later fd adoption);
  - the lease/BUSY, group-rollback and leased-rotation tests and expectations;
  - the self-contained threshold derived from lease occupancy.
- The model-to-real-tree move is done FIRST, before any new test. The piece model harness drives the real tree since
  7b.

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md.
   - Layering: L2 includes only L0/L1.
   - One public header; the journal_ prefix.
   - A15 (ASSERT only for caller bugs); Law 2 on the typing path.
   - Explicit FSM enums under -Wswitch-enum, with no coordinating booleans.
   - Checked allocations.
   - Tests link the archive, never #include a .c, and inject syscalls with -Wl,--wrap.
2. docs/decisions/wave0-plan-amendments.md A28 (the write model, rules 1–8; binding), A13, A14, A19, A20, A22 (Q7's
   exclusive sections are superseded by A28), A23.
3. docs/decisions/wave1-journal-write-model.md in full: §1.4 slots, §1.5–1.6 cuts, §1.9 fair service, §2.3–2.7 reader
   and repair, §4 what your wip keeps and loses, §5.4 exceptional recovery.
4. src/journal/journal.h as amended by the N2 bead (the grammar and the pure reader API), and that bead's decision file.
5. docs/decisions/wave1-L2-L5-harvest.md §0 and §2. v1 structure NOT to carry: unchecked arena sites, the sync spin,
   the private pool and message handler, the env CRC switch, seven coordinating booleans. Port the tests per §2.7.
6. docs/decisions/wave1-L2-derivations.md Q6 (one write per record on the UI thread; page cache suffices for a process
   kill) and Q3 (RESTORE_BASE_RANGE by reference only onto a verified ORIGINAL, else by value; a worker reads, the UI
   applies). Q7 as amended by A28. docs/decisions/wave1-L2-q2-protocol.md (K and M; T1–T5).
7. Headers: src/piece/piece.h, src/undo/undo.h (after v2-bmw), src/os/os.h, src/work/work.h, src/base/base.h.
8. v1 evidence, read-only: /home/tobias/Projects/sublimite-v1-oracle/src/journal/journal.c (append :264,
   cache_write :604-625, replay :269-316, apply_piece :409-417) and its kill/hot-exit/safety tests.

Scope (A28):
- ONE WRITER (rule 1). Only the UI thread writes the named journal: one complete record per single-attempt write.
  Workers encode and CRC records from pinned snapshots into bounded buffers handed over by mailbox, and sync the named
  inode through a captured offset. They never write or truncate it. Only the writer knows the final global sequence:
  finalize the header and its CRC at append, or combine a header CRC with the worker's payload CRC (§2.6; state which).
- SLOTS AND PRODUCERS (rules 2–3). Per buffer, one command slot and one checkpoint slot.
  - A checkpoint request that the in-flight checkpoint satisfies coalesces with it.
  - A request it cannot satisfy restarts it with a new BEGIN. No queue and no waiting.
  - The UI rejects members and completions of an abandoned instance by producer identity before appending.
  - A checkpoint's boundary is a published snapshot pinned at its BEGIN (rule 5).
- THE BRACKETED COMMIT (rule 5, W1 d).
  - For a multi-record command, every record except the last reaches the journal before undo_commit, and the last one
    follows it.
  - A failed read or write before the commit is undo_abort plus an ignored group.
  - Capacity to close every admitted operation is reserved before it starts.
- FAIR SERVICE (rule 6). Each loop turn serves input first, then at least one ready record of each producer class in
  rotation, then ready barriers. State the claim as isolation and progress, never elapsed time.
- BARRIERS. Implement the inode-specific sync receipt (seam §3.8): the sealed image, the offset submitted, and the
  pinned fd and image epoch. Only the submitted watermark is credited after a successful sync. Also implement
  failed-append reconciliation and physical job retirement.
- RECOVERY. Call the N2 pure reader for the logical plan, then materialise it.
  - Choose origins per buffer before opening any BASE.
  - Apply whole validated commands under the buffer's piece checkpoint, in final-sequence order.
  - Checkpoint slices never enter command replay.
  - RESTORE_BASE_RANGE per Q3.
  - Refuse BASE-relative replay onto a file whose identity does not match, on every path. Report an unresolved BASE and
    offer the journal's literal bytes (A23).
- REPAIR (§2.7; the advisor's rule that repair must not destroy what it cannot explain).
  - Physical framing is tracked separately from logical completeness.
  - Orphan members are kept whole at EOF, and the allocator resumes past every retained record.
  - Only a demonstrably torn final record is truncated.
  - Complete corruption followed by more bytes sets the image aside intact under a set-aside name, and a new image is
    written from the valid prefix.
  - Never seek past corruption to apply a later marker.
  - Nobody deletes a set-aside image automatically; the user is told where it is.
- ROTATION (rule 7). It is the only image switch, exact-match, performed by the writer, and deferred while any token,
  pinned cutoff or open group is live.
  - The worker writes and syncs the candidate.
  - On an exact match, the UI switches the name and routes later appends as one serialized action.
  - New durability credits wait for the parent-directory barrier.
- EXCEPTIONAL RECOVERY (rule 8(ii), §5.4). It covers writeback loss, and sequence or budget exhaustion, while a token or
  group is live.
  - A fresh complete image is built with acknowledgments suspended. This is acceptable only here, because typing has
    already stopped.
  - The visible state and ownership of outstanding evidence are specified.
  - Headroom is checked before a capture or group start.
- THE SELF-CONTAINED SIZE (A23, rule 8(iii)). Re-derive it from fair append service under typing, not from lease
  occupancy. Use the measured journal append rate on this machine and what one producer class may append per turn
  without delaying typing. Cite v1's whole-text versus delta split at the pin, and write the derivation beside the
  constant.
- The INSERT slice size is encoder policy, with a per-write derivation (v2's reason, not v1's).
- One -Wswitch-enum state enum per independent axis (worker transport; recovery suspension). Derive the axes against
  v1's journal_retry, and state why each is independent, or merge them.

Acceptance (each a test, red first):
- PLAN §4.2 at journal level. Kill at every member and marker of a command group and of a checkpoint group, with
  acknowledged keys of the same and other buffers interleaved. Before ACK: pre-edit or whole-edit; after ACK: whole-edit.
  Port v1's journal_kill_test and journal_hot_exit_test.
- Astra's §1.5 cuts a–f as executable tests under K, and under M where a sync point exists:
  - a same-buffer key between checkpoint members;
  - a kill between BEGIN and final with acknowledged keys;
  - a sync through final, then a lost later key;
  - two buffers' checkpoints, the second closing first;
  - B's command among A's members;
  - abort, then a new command.
- §1.3 as a regression: a second checkpoint request on a buffer restarts (or coalesces with) the first. The reader
  never sees two open checkpoint instances of one buffer.
- A stale worker member of an abandoned checkpoint is rejected before append (producer identity).
- Unknown version or required feature: refused and set aside without truncation.
- Repair:
  - a torn final record is repaired in place;
  - a complete record with one flipped bit, followed by valid records, leaves the original bytes byte-identical under
    a set-aside name, recovery reports it, and the session continues on a new image;
  - interleaved orphan members are kept, and later committed records of other buffers survive.
- A23 T5 at journal level: a BASE replaced by a foreign file recovers as "unresolved BASE" with literals offered, never
  applied to the wrong file. A self-contained buffer recovers exactly after its BASE is deleted or replaced.
- Fair service: a large checkpoint or long command on buffer A does not delay a one-byte key on buffer B beyond the
  record its own turn writes. Assert this by turn and record counts, not time.
- Rotation: deferred while a token, pinned cutoff or open group is live. An exact-match switch with the parent barrier
  gating new credits. A kill at each rotation step recovers one named image containing every acknowledged record.
- Exceptional recovery: injected writeback loss with a live token. Acknowledgments are suspended and visible, a fresh
  complete image is written, and nothing acknowledged is lost under K.
- Typing-path append and the bracketed commit under the release malloc guard (active && count == 0; law2 suite).
- Measurement 1 (A22, Q6's Wave 1 exit number): input-to-submit median and tail with the append, during ordinary typing
  and under heavy writeback from another process, stamped [bat]/[AC] + load. The load generator is a child the bench
  owns and reaps.
- Measurement 2 (A22, Q3): on the 10 GiB class, peak ADD extents, piece nodes, scratch, RSS and temporary disk for
  recovery of a mixed-generation checkpoint with repeated ranges.
- DISK HYGIENE for both measurements: run `df` on the build filesystem before each run, and refuse to start without the
  fixture size plus a stated margin free. Fixtures only under the checkout's build/, removed on success and on failure.
  Sizes carry derivations.
- check-tsan on the journal suites. Fuzz append/recover sequences with interleaved groups and torn tails (the N2
  reader's fuzz covers the pure reader). Port bench/journal_bench.c.

Rules:
- No git (not even git add -N), no bd.
- No rm -rf, no mktemp with a clean-up trap. Nothing outside your checkout, fixtures included.
- HOME for cache-writing tools under build/. Build under build/.
- At most nice make -j2. Run date before each.
- Report rather than run anything needing approval.

Acceptance lanes: nice make -j2 check (CONTRACT_CHECK_DISPLAYS from the coordinator), check-tsan, fuzz-smoke.

Report:
- red/green per acceptance line;
- which wip parts were kept and which deleted, by symbol;
- the self-contained size with its derivation and measurements;
- both measurements, stamped;
- lines removed vs added, and the comment-free count per file against v1's journal;
- anything outside scope (report only).
