Send-back (advisor read of docs/decisions/wave1-savectl-spec.md at 6ef9e34, 2026-11-24). One round, document work, no
execution. Revise docs/decisions/wave1-savectl-spec.md in place. Each item: give the counter-example or make the cut.

KEPT (do not reopen):
- The save commits a published edit boundary and installs that boundary's content id.
- Durable PREPARED authorises the rename.
- The finish candidate exists only in FINISHING. A post-rename mismatch removes both the candidate and the finish
  capability (the structural answer to v1 edit-v1f5).
- A failed directory barrier retries the barrier only and never renames again.
- The flat oracle never calls the controller's own classifier.
- T1–T5.
- The phase split is APPROVED in principle, subject to item 1: the temp write happens without the lease, and the lease is
  held only around the journal barriers.

1. Typing under the lease.
   - State what a keystroke does while the journal lease is held: applied in RAM and appended after the lease returns,
     refused, or delayed. State what the user sees.
   - For each phase that takes the lease, give the maximum bytes written and the number of syncs while it is held, as a
     derivation (Law 4).
   - The peer checkpoint row allows a payload equal to the peer's whole length. Either show the interleaving that corrupts
     if those bytes are written before the lease (with only the fence and the PREPARED record inside it), or move them
     out of the lease.
2. The axes are not orthogonal: 35 of 144 tuples are reachable.
   a. The lease is journal's state. Cut the stored axis; whoever needs it asks journal.
   b. RELOADING is a phase.
   c. External state is forced by the phase everywhere except IDLE and RECOVERY_PENDING. With (a) and (b) that leaves 11
      states: IDLE clean, IDLE changed, RELOADING, PREPARING, WRITING, VERIFYING, DIRSYNC_RETRY, FINISHING,
      RECOVERY_PENDING clean, RECOVERY_PENDING changed, CONFLICT_CANCEL. Each also carries `closing`.
   d. No RETIRED state and no retire call. savectl_destroy returns BUSY until the controller is closing, idle and holds
      nothing.
   Proposal: one tag with 11 enumerators under -Wswitch-enum plus `closing`, giving 22 states, all reachable. To keep four
   stored axes, give a reachable situation the single tag cannot express. Either way, draft A27 for PLAN 4.4: its
   "orthogonal four axes" are observables, not stored fields.
3. savectl_resolution carries no information.
   - Each waiting phase admits exactly one resolution:
     - DIRSYNC_RETRY: the barrier;
     - FINISHING: finish (the controller knows whether writeback was lost);
     - RECOVERY_PENDING and CONFLICT_CANCEL: cancel.
   - Proposal: savectl_resolve(s) with no action argument.
   - State in one line why FINISHING waits for the session owner rather than advancing on tick. If there is no reason,
     tick drives it and only a retry after a failure is explicit.
4. Standalone mode (log == NULL). Name the v2 product path that constructs a controller without a journal, or delete the
   mode. Tests use a real journal in a directory under build/.
5. Layer leak: savectl_view (cursor, anchor, scroll) in an L2 header. Cut the type and the `latest` argument; take_reload
   returns the tree and the source. Keeping it needs a counter-example.
6. Small cuts.
   a. savectl_knowledge and savectl_saved_id go if content ids start at 1 (settle with the edit spec), so saved id 0
      means unknown.
   b. request_ns: name its consumer or strike it.
   c. The five failure fields (error, outcome, file_error, journal_error, native_error): name the consumer of each.
7. The header must be readable without the Q-documents.
   - Define once, in the header, every term it uses: embargo, sealed joining, witness, peer fence, displaced generation,
     cutoff, image-local boundary, and the letters S, s, G, H.
   - Use full sentences, not telegraphic comments.

PLUS a seam table, as its own section.
- Cover every call the spec requires of file and journal: resumable synced temp, session-wide displacement fence,
  image-qualified root pins, classified publication outcomes, checkpoint_by_value, prepare/finish/cancel.
- For each, give the exact line of docs/briefs/L2-1-file.md, docs/briefs/L2-3-journal-save.md or src/journal/journal.h
  that provides it, or write GAP.
- Do not confirm a seam from the proposal's own description of it.

No implementation bead for savectl until the revised spec is signed.
