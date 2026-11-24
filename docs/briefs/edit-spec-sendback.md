Send-back (advisor read of docs/decisions/wave1-edit-spec.md at 5d57762, 2026-11-24). One round, document work, no
execution. Revise docs/decisions/wave1-edit-spec.md in place, after the savectl send-back round. Both come back to the
advisor together with the seam table. No implementation bead for edit, savectl or the undo erratum before signature.

KEPT:
- Terms are defined in the header.
- One state tag.
- Lease and suspension stay with journal.
- Id 0 means unknown.
- An independent flat oracle.
- A keystroke costs 41 B of journal and 0 syncs.

DECIDED (write into the header):
- E2 (key waits under an exclusive grant): accepted as the rule. The grant gets a requirement: an exclusive journal
  grant covers no sync, and no write whose size depends on buffer, peer or session content. This is answered in the
  savectl round and in wave1-L2-derivations.md "Q7 amended".
- E4 (no-fail journal handoff): approved, with three conditions written into the header:
  1. Every reservation precedes undo_commit.
  2. Reservations are exact and released on every abort path, with a test per path.
  3. A reservation holds descriptors, not payload copies, unless E3 shows a copy is unavoidable and bounded.
- E5 (history cap): (G) v1 parity in depth, converted by a (P) charge.
  - v1's default is 32,768 history records (v1 src/editor/buffers.c:256 at the pin, cfg.history_keys).
  - v2's default cap in bytes = 32,768 × c, where c is what undo charges for one unjoined one-byte typing group, taken
    from undo.c's record layout.
  - The implementer writes c and the product next to the constant.
  - It stays a caller-supplied input (config). No other number.

TO DO (each: adopt, or give the input that defeats it):
E1. Replay seam. The observer-with-veto inside undo is NOT approved.
    - The simplest alternative reuses the forward protocol. Replay stops with the group applied and NOT committed. The
      caller reads that prepared group's splices through a read-only iterator: offset, removed length, inserted range
      as a span description in source coordinates, in application order. The caller stages, takes the candidate
      snapshot, then calls the existing undo_commit or undo_abort.
    - Invariant: the history position moves only inside undo_commit called by the owner, for forward edits and replay
      alike. Edit then has one publish sequence, and undo gains no callback, no re-entrancy rules and no new refusal
      result.
    - Edit may slice the staging of a very large group across turns before it commits; edit already owns that state.
    - The rejected alternative's counter-example does not defeat this, because history has not advanced when staging
      fails.
    - The change to the landed undo.h becomes its own small erratum bead, a prerequisite of the edit bead, filed once the
      shape is signed.
E3. By-value restore payload. "Delete a mapped range, save, undo" restores an embargoed range by value.
    - State where those bytes live between undo commit and the last append. Either they are copied into staging (then RAM
      is proportional to the range, and a large undo is refused for memory: the v1 edit-hagx class again), or they are
      streamed from the pinned snapshot during EMITTING.
    - If streamed, say what happens when a mapped read fails validation after the memory commit.
    - Give the RAM bound and the user-visible behaviour at 10 GiB (G, Wave 2's file size), as a derivation, not a
      sentence.
E6. Cuts. Each needs a counter-example to keep it.
    a. preferred_col goes: columns are layout, and the bead is "layout-free". The caller holds it and resets it when
       edit_take_change reports a change.
    b. UNDOING and REDOING become one REPLAYING tag with a direction, giving five tags.
    c. edit_maintain's three budgets are the caller's numbers. The header says they must be derived at the call site
       (Law 4), and the edit bead's brief names who derives them.
E7. Style: the MODEL paragraph is right. Use full sentences throughout the function comments; several are still
    telegraphic lists.
