Bead L2-3 — L2 journal save transaction without retention: ordered preparation, peer checkpointing, finish and cancel (dossier bead L2-3 as reshaped by A23). sublimité v2 is a clean-slate C11 editor rebuild; your checkout is a worktree of /home/tobias/Projects/sublimite-v2 (branch v2). One worker, one bead; do exactly this scope, then stop. Routing: Claude or Muse (tests stop the process at every save step; never Codex). Depends on L2-2a (wire + codec) and L2-2b (append, transport, rotation, recovery) on v2.

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md; src/journal/journal.h (wire section incl. the SAVE records PREPARED / FINISHED / CANCELLED, GENERATION with VALUE_ONLY, CHECKPOINT_BY_VALUE, TARGET) and docs/decisions/L2-2a-journal-wire.md, L2-2b's decision file.
2. docs/decisions/wave0-plan-amendments.md A23 (rules 1–7, the self-contained-size rule, the T5 product contract) and A22.
3. docs/decisions/wave1-L2-q2-protocol.md in full (durable state at every step; T1–T5 traces; the record table) and wave1-L2-q2-crash-consistency.md (the two refuting traces the protocol closes).
4. docs/decisions/wave1-L2-derivations.md Q9 (savectl RECOVERY_PENDING; a post-rename foreign replacement permits cancel only), Q10 (content ids), Q11 (now answered by A23 rules 4 and 6), Q14.
5. docs/decisions/wave1-L2-L5-harvest.md §2 (v1's journal_save_prepare/finish/cancel and journal_retry) and §3 (savectl, which drives this API — L2-4, not yours).
6. v1 evidence, read-only: /home/tobias/Projects/sublimite-v1-oracle/src/journal/journal.c :1219-1331 (save prepare/finish/cancel) and the save tests.

Scope (A23):
- Prepare, in order: capture the snapshot S and its boundary s (and from this moment journal any restore from the generation being replaced by value: set VALUE_ONLY at capture, rule 3); write and sync the temp (the file layer's work, L2-1; you consume its fd witness); for every OTHER journal-recorded buffer state whose BASE is the generation being replaced — live or closed-but-kept, including recovered drafts of the same target (rule 6) — write and sync a CHECKPOINT_BY_VALUE (rule 4; worst case file-sized, only when such a state exists); then append and sync SAVE(PREPARED) with the temp's witness and s; only then may the caller rename.
- Finish: after the target directory sync and the fd/path verification, publish SAVE(FINISHED) declaring the installed generation (a durably published finish image or a complete CURRENT image per the wire section).
- Cancel: after reconciling the namespace and securing an independently usable current representation, SAVE(CANCELLED) (or the token dropped in a complete CURRENT image with the same state); cancellation never marks the buffer saved.
- External replacement detected by the file owner: the same CHECKPOINT_BY_VALUE mechanism (rule 5), one code path with two triggers.
- NOT built (A23 rule 1): no retention of the replaced file, no clone, no link, no retention artifacts, no startup retirement scan of them. v1's retire.c is not carried.

Acceptance (each a test, red first; stop the process at every step):
- T1–T4 of wave1-L2-q2-protocol.md as executable tests: one buffer; Save As over a dirty peer (including a stop during the peer's checkpoint); a closed-but-kept state on the replaced generation; an undo by reference issued while the temp is written. Each stop: recovery yields exactly the text the protocol table says.
- T5 as the product contract: foreign rename after our rename, then a stop before the conflict checkpoint -> unresolved BASE reported, journal literals offered, nothing applied to the wrong file; a buffer at or below the self-contained size loses nothing.
- The piece-pool row "Source conflicts, rotation and history lifetime" at journal level, minus its retention parts (A23): Select All -> Delete -> Save empty -> Undo -> stop/restart with different old and new BASE generations; mismatched generation.
- No UI-thread drain loop (Q7); law2 on any typing-path call you add.
- docs/decisions/L2-3-journal-save.md: each mechanism with invariant, simplest alternative, defeating counter-example.
If a SAVE field is missing from the wire, do not improvise it: report it to the coordinator as an amendment request (the wire section's first paragraph says how).

Rules: no git (not even git add -N), no bd. No rm -rf, no mktemp with a clean-up trap, nothing outside your checkout; HOME under build/; build under build/; at most nice make -j2; no starts Tue 24 Nov 03:45–05:00 CET; run date before each; report rather than run anything needing approval. Acceptance lanes: nice make -j2 check (CONTRACT_CHECK_DISPLAYS from the coordinator), check-tsan, fuzz-smoke. Report: red/green per acceptance line, lines removed vs added, anything outside scope (report only).
