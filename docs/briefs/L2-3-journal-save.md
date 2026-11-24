Bead L2-3: the L2 journal save transaction on A28. Save phases are appended records with barrier receipts; peer and
conflict protection are checkpoint groups; there is no finish image and no exclusive section.

Rewritten 2026-11-24 on A28; the advisor reads this brief before dispatch. sublimité v2 is a clean-slate C11 editor
rebuild. Your checkout is a worktree of /home/tobias/Projects/sublimite-v2 (branch v2). One worker, one bead: do exactly
this scope, then stop. Routing: Claude or Muse, because the tests stop the process at every save step; never Codex.

Depends on, all landed on v2:
- the wire amendment + pure reader (A28 N2);
- L2-2b (single-writer append, checkpoint producers, the inode-specific sync receipt, recovery);
- L2-1 (file: the temp custody, classified rename results and barrier-only retry this bead consumes).

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md. src/journal/journal.h as amended (the SAVE records PREPARED / FINISHED / CANCELLED; GENERATION with
   VALUE_ONLY; checkpoint groups and their optional TARGET), the N2 and L2-2b decision files, and
   docs/decisions/L2-1-file.md.
2. docs/decisions/wave0-plan-amendments.md A28 (binding; rule 5: append-only save phases), A23 (rules 1–7, the
   self-contained-size rule, the T5 product contract) and A22 (Q7's exclusive sections superseded by A28).
3. docs/decisions/wave1-journal-write-model.md §1.6 (the save cuts T1/T2 under A28), §2.3 (checkpoint boundaries and
   staged TARGET), §2.4 (append-only SAVE reader semantics), §3.4–3.7 (the seams this bead closes or consumes).
4. docs/decisions/wave1-L2-q2-protocol.md in full (durable state at every step; T1–T5), and
   wave1-L2-q2-crash-consistency.md.
5. docs/decisions/wave1-savectl-spec.md: the caller's contract. savectl (L2-4) drives this API; it is not yours.
6. docs/decisions/wave1-L2-L5-harvest.md §2 (v1's journal_save_prepare/finish/cancel and journal_retry).
7. v1 evidence, read-only: /home/tobias/Projects/sublimite-v1-oracle/src/journal/journal.c :1219-1331 and the save
   tests.

Scope (A23 + A28):
- PREPARE, in order:
  1. Capture the snapshot S and its boundary s, and the image. From capture on, journal any restore from the generation
     being replaced by value (VALUE_ONLY, A23 rule 3).
  2. Consume the file layer's synced temp and its witness (L2-1).
  3. For every OTHER journal-recorded buffer state whose BASE is the generation being replaced (live or
     closed-but-kept, including recovered drafts of the same target, rule 6), request a checkpoint group through
     L2-2b's per-buffer producer. Its boundary is a published snapshot at BEGIN. Ordinary typing on that buffer and on
     every other buffer continues between its members.
  4. When those checkpoints are complete and covered by a sync receipt, append SAVE(PREPARED) with the temp's witness
     and s, and sync it.
  5. Only then may the caller rename.
- FINISH (§2.4).
  - After the target directory sync and the fd/path verification, stage the installed generation's GENERATION
    declaration and any required binding consequences before FINISHED.
  - Append SAVE(FINISHED). The new origin and the saver's cutoff take effect together at a complete FINISHED.
  - An orphan declaration binds nothing.
  - No finish image and no rename of the journal.
- CANCEL. After reconciling the namespace and securing an independently usable representation, append SAVE(CANCELLED).
  It refers to a complete matching checkpoint or to the still-usable prior representation, and resolves only its token.
  Cancellation never marks the buffer saved.
- KEEP and external replacement detected by the file owner: the same checkpoint-group mechanism (rule 5), using the
  checkpoint's atomic TARGET where needed. One code path with two triggers.
- CLASSIFIED PUBLICATION RESULTS (seam §3.7, narrowed). Prepare, finish and cancel each return one of: definitely
  absent; kernel-visible/possibly written; durably synced. Token ownership survives uncertainty. A failed barrier never
  certifies a finish. On writeback loss, hand over to L2-2b's exceptional recovery.
- The generation-membership fence (§1.7) is a session-owner rule (L5 app_buffers and savectl). This bead exposes what
  the fence needs: the capture, the peer set at prepare, and a refusal when membership affecting G changed. It does
  not own the session index.
- NOT built (A23 rule 1): no retention of the replaced file, no clone, no link, no retention artifacts, no startup
  retirement scan. v1's retire.c is not carried.

Acceptance (each a test, red first; stop the process at every step, under K, and under M where a sync point exists):
- T1–T4 of wave1-L2-q2-protocol.md as executable tests, with the A28 cut tables of §1.6:
  - one buffer;
  - Save As over a dirty peer, including a stop during the peer's checkpoint while the user types in the peer and in a
    third buffer;
  - a closed-but-kept state on the replaced generation;
  - an undo by reference issued while the temp is written.
  At each stop, recovery yields exactly the text the table says.
- T5 as the product contract: a foreign rename after our rename, then a stop before the conflict checkpoint, gives
  unresolved BASE reported, journal literals offered and nothing applied to the wrong file. A buffer at or below the
  self-contained size loses nothing.
- Append-only finish: a stop between the staged GENERATION declaration and FINISHED binds nothing (the orphan
  declaration). A stop after FINISHED adopts the installed generation and the cutoff together.
- Typing continues during every save phase: a one-byte key on the saving buffer and on a peer is acknowledged while
  the peer checkpoint is in flight. Assert by records and turns, not time.
- The piece-pool row "Source conflicts, rotation and history lifetime" at journal level, minus retention (A23): Select
  All -> Delete -> Save empty -> Undo -> stop/restart with different old and new BASE generations; mismatched
  generation.
- Each classified result class is produced by a test.
- Law2 on any typing-path call you add.
- docs/decisions/L2-3-journal-save.md: each mechanism with its invariant, the simplest alternative and the defeating
  counter-example.

If a SAVE field is missing from the wire, do not improvise it. Report it to the coordinator as an amendment request.

Rules:
- No git (not even git add -N), no bd.
- No rm -rf, no mktemp with a clean-up trap. Nothing outside your checkout.
- HOME under build/; build under build/.
- At most nice make -j2. Run date before each.
- Report rather than run anything needing approval.

Acceptance lanes: nice make -j2 check (CONTRACT_CHECK_DISPLAYS from the coordinator), check-tsan, fuzz-smoke.

Report: red/green per acceptance line, lines removed vs added, anything outside scope (report only).
