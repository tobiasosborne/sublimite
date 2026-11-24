Bead L2-2b-1: the journal WRITER on A28. It covers the UI append, slots and producers, fair service, the command-group
API with its reserved final record, the sync receipt, and failed-append reconciliation. It also deletes the dead wip.
Written 2026-11-24 from the advisor's split of L2-2b; the advisor reads it before dispatch.

sublimité v2 is a clean-slate C11 editor rebuild. Your checkout is a worktree of /home/tobias/Projects/sublimite-v2
(branch v2). One worker, one bead: do exactly this scope, then stop. Routing: Claude or Muse, because the tests kill
processes and tear writes; never Codex.

Order: L2-2b is three beads that land in order (2b-1 writer, 2b-2 recovery and repair, 2b-3 rotation and the
exceptional path), each with its own lanes and owner read (advisor, 2026-11-24). Nothing of L2-2b is dispatched before
the N2 bead (wire amendment + pure two-pass reader, Sol) lands and the advisor signs the runtime section (artifact 1b).

The journal does not know undo exists (advisor). It offers a command group whose final record is reserved at begin and
cannot be refused later. The caller (edit) puts undo_commit between the last member and the final record. Journal
tests stand in for the caller with a hook between the two.

STARTING POINT: the wip at a94af15 (branch t3/journal-runtime, worktree
/home/tobias/.t3/worktrees/sublimite-v2/t3-de932866-2c152938cbe90882). Astra's §4 names the symbols.
- THIS bead deletes the dead wip, so that 2b-2 and 2b-3 start from a tree without it:
  - the global group state (j->group, admit, the ownerless journal_group_begin/commit);
  - rollback or truncation to BEGIN;
  - leased worker writes and worker truncation (lease_admit, JOURNAL_TRANSPORT_LEASE, run_checkpoint's write path,
    JOB_REPAIR's worker rewrite);
  - the lease/BUSY and group-rollback tests.
- KEPT here, restarted from the wip:
  - the single-attempt UI append (ui_write: one full positioned os_file_pwrite_once per record before ACK; a short,
    zero or failed write keeps the exact held bytes and offset and latches recovery suspension);
  - the exact-byte retry, routed through the UI;
  - the off-thread sync through a captured watermark;
  - one UI dispatcher;
  - retention of a record until physical retirement.
- Any wip code that 2b-2 or 2b-3 owns (recovery, repair, rotation) and that is not on the deleted list stays as it is,
  untouched and compiling, for its bead.
- The model-to-real-tree move for every test this bead touches comes first, before any new test. The piece model
  harness drives the real tree since 7b (1.23).

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md.
   - Layering: L2 includes only L0/L1.
   - One public header; the journal_ prefix.
   - A15 (ASSERT only for caller bugs); Law 2 on the typing path.
   - Explicit FSM enums under -Wswitch-enum, with no coordinating booleans.
   - Checked allocations.
   - Tests link the archive, never #include a .c, and inject syscalls with -Wl,--wrap.
2. docs/decisions/wave0-plan-amendments.md: A28 (the write model, rules 1–8; binding), A13, A14, A19, A20, A22 (Q7's
   exclusive sections are superseded by A28), A23.
3. docs/decisions/wave1-journal-write-model.md in full (Astra; the section numbers below refer to it).
4. src/journal/journal.h: the wire sections as amended by the N2 bead, and the RUNTIME section as signed (artifact 1b of
   docs/decisions/wave1-journal-wire-amendment.md). Implement the signed header. Do not invent API; a missing call goes
   to the coordinator as an amendment request.
5. docs/decisions/wave1-L2-L5-harvest.md §0 and §2. v1 structure NOT to carry: unchecked arena sites, the sync spin, the
   private pool and message handler, the env CRC switch, seven coordinating booleans.
6. Headers: src/piece/piece.h, src/os/os.h, src/work/work.h, src/base/base.h.
7. The decision files of the earlier L2-2b beads, if any have landed.
8. Q6 in docs/decisions/wave1-L2-derivations.md (one write per record on the UI thread; page cache suffices for a process
   kill). v1 evidence, read-only: /home/tobias/Projects/sublimite-v1-oracle/src/journal/journal.c append :264 and
   cache_write :604-625.

Scope:
- ONE WRITER (A28 rule 1). Only the UI thread writes the named journal: one complete record per single-attempt write.
  - Workers encode and CRC records from pinned snapshots into bounded buffers handed over by mailbox, and sync the named
    inode through a captured offset. They never write or truncate it.
  - Only the writer knows the final global sequence: finalize the header and its CRC at append, or combine a header CRC
    with the worker's payload CRC (§2.6; state which).
- SLOTS AND PRODUCERS (rules 2–3). Per buffer, one command slot and one checkpoint slot.
  - A checkpoint request that the in-flight checkpoint satisfies coalesces with it. A request it cannot satisfy
    restarts it with a new BEGIN. No queue and no waiting.
  - The UI rejects members and completions of an abandoned instance by producer identity before appending.
  - A checkpoint's boundary is a published snapshot pinned at its BEGIN (rule 5).
- THE COMMAND GROUP. The final record is reserved exactly at begin and cannot be refused later. Capacity to close every
  admitted operation is reserved before it starts. The caller's commit sits between the last member and the final
  record; the tests use a hook there.
- FAIR SERVICE (rule 6). Each loop turn serves input first, then at least one ready record of each producer class in
  rotation, then ready barriers. The claim is isolation and progress, never elapsed time.
- BARRIERS. The inode-specific sync receipt (seam §3.8): the sealed image, the offset submitted, and the pinned fd and
  image epoch. Only the submitted watermark is credited after a successful sync. Also failed-append reconciliation and
  physical job retirement.
- The INSERT slice size is encoder policy, with a per-write derivation (v2's reason, not v1's).
- One -Wswitch-enum state enum per independent axis (for example worker transport; recovery suspension). State why each
  is independent, or merge them.

Acceptance (each a test, red first):
- The typing-path append and the command group under the release malloc guard (active && count == 0; law2 suite).
- Fair service: a large checkpoint or long command on buffer A does not delay a one-byte key on buffer B beyond the
  record its own turn writes. Assert this by turn and record counts, not time.
- §1.3 as a regression: a second checkpoint request on a buffer restarts (or coalesces with) the first. The journal
  never holds two open checkpoint instances of one buffer.
- Stale member: a worker member or completion of an abandoned checkpoint is rejected before append.
- The final record reserved at begin is never refused: exhaust capacity between the members and the final record (the
  caller hook), and the final record still appends.
- A short, zero or failed write: the exact held bytes and offset are kept, suspension latches and success is withheld.
  The retry writes the same bytes at the same offset (--wrap=os_file_pwrite_once).
- The sync receipt credits only the submitted watermark on the named inode. A sync failure credits nothing.
- A grep proof that the deleted wip symbols are gone.
- Measurement 1 (A22, Q6's Wave 1 exit number): input-to-submit median and tail with the append, during ordinary typing
  and under heavy writeback from another process, stamped [bat]/[AC] + load. The load generator is a child the bench
  owns and reaps. If the tail fails G1, report it.
- The cost of JOURNAL_LITERAL_LIMIT's one-page choice (advisor, at signature of the runtime section), as tracked
  numbers, stamped: records and UI write calls per MiB of checkpoint, and the bytes per second the UI appends while
  idle.
- check-tsan on the journal suites. Fuzz: append sequences with interleaved producers and torn writes.

Rules:
- No git (not even git add -N), no bd.
- No rm -rf, no mktemp with a clean-up trap. Nothing outside your checkout, fixtures included.
- HOME for cache-writing tools under build/. Build under build/.
- At most nice make -j2. Run date before each.
- Report rather than run anything needing approval.

Acceptance lanes: nice make -j2 check (CONTRACT_CHECK_DISPLAYS from the coordinator), check-tsan, fuzz-smoke.

Report:
- red/green per acceptance line;
- the wip symbols kept and deleted;
- lines removed vs added;
- the comment-free count per file (gcc -fpreprocessed -dD -E -P f | grep -c '[^[:space:]]') against v1's journal
  (/home/tobias/Projects/sublimite-v1-oracle/src/journal/);
- anything outside scope (report only).
