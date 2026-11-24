Bead L2-2b-3: journal ROTATION AND THE EXCEPTIONAL PATH on A28. It covers writer-owned exact-match rotation,
exceptional recovery with headroom checks, and the self-contained size. Written 2026-11-24 from the advisor's split of
L2-2b; the advisor reads it before dispatch. Depends on 2b-2 landed.

sublimité v2 is a clean-slate C11 editor rebuild. Your checkout is a worktree of /home/tobias/Projects/sublimite-v2
(branch v2). One worker, one bead: do exactly this scope, then stop. Routing: Claude or Muse, because the tests kill
processes and tear writes; never Codex.

Order: L2-2b is three beads that land in order (2b-1 writer, 2b-2 recovery and repair, 2b-3 rotation and the
exceptional path), each with its own lanes and owner read (advisor, 2026-11-24). Nothing of L2-2b is dispatched before
the N2 bead (wire amendment + pure two-pass reader, Sol) lands and the advisor signs the runtime section (artifact 1b).

The journal does not know undo exists (advisor). It offers a command group whose final record is reserved at begin and
cannot be refused later. The caller (edit) puts undo_commit between the last member and the final record. Journal
tests stand in for the caller with a hook between the two.

STARTING POINT: v2 after 2b-2. Restart from the wip at a94af15 (branch t3/journal-runtime) only where this KEPT list
applies. The rest is deleted, not adapted.
- KEPT: writing and syncing a private rotation candidate.
- DELETED:
  - the worker-side rotation switch (run_rotation's rename and later fd adoption by the UI, journal_rotate_start's
    leased shape);
  - the leased-rotation tests;
  - the self-contained threshold derived from lease occupancy.
- Do the model-to-real-tree move for any test that has not moved yet.

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
8. docs/decisions/wave1-L2-derivations.md "Q7 amended" (the exact-match switch). v1 evidence, read-only:
   /home/tobias/Projects/sublimite-v1-oracle/src/journal/journal.c rotate :943-1027.

Scope:
- ROTATION (A28 rule 7). It is the only image switch, exact-match, performed by the writer, and deferred while any token,
  pinned cutoff or open group is live.
  - The worker writes and syncs the candidate.
  - On an exact match (logical watermark, policy and epochs), the UI switches the name and routes later appends as one
    serialized action.
  - New durability credits wait for the parent-directory barrier. A directory retry credits only data already synced on
    the candidate.
- EXCEPTIONAL RECOVERY (rule 8(ii), §5.4). It covers writeback loss, and sequence or budget exhaustion, while a token or
  group is live.
  - A fresh complete image is built with acknowledgments suspended. This is acceptable only here, because typing has
    already stopped.
  - The visible state and ownership of outstanding evidence are specified.
  - Headroom is checked before a capture or group start.
- THE SELF-CONTAINED SIZE (A23, rule 8(iii); advisor correction).
  - With A28, a checkpoint no longer delays typing, so the size is a policy on journal volume.
  - The parity value is v1's whole-text versus delta split at the pin (G). Reduce it only if the rotation budget cannot
    hold one such checkpoint with a stated margin (P).
  - The constant carries this derivation. It is not derived from an append rate measured on this machine.
  - Measure and report how long a checkpoint of that size takes to become durable under typing, as a tracked number,
    not as the source of the constant.

Acceptance (each a test, red first):
- Rotation is deferred while a token, pinned cutoff or open group is live.
- An exact-match switch, with the parent barrier gating new credits. A miss leaves the old image active and loses
  nothing.
- A kill at each rotation step recovers one named image containing every acknowledged record. Under M, either image
  before the parent sync.
- Exceptional recovery: injected writeback loss with a live token. Acknowledgments are suspended and visible, a fresh
  complete image is written, and nothing acknowledged is lost under K. Headroom refusal before a capture or group start
  is visible, not a lost edit.
- The self-contained size: the constant with its derivation. A test that a buffer at or below it becomes self-contained
  after its first acknowledged edit, off the typing path. The tracked durability number, stamped [bat]/[AC] + load.
- check-tsan on the journal suites. Port bench/journal_bench.c.

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
