# v2-bmw · Owner-committed undo replay

[Signed replay contract and history charge](wave1-edit-spec.md#e1--undoh-erratum-for-signature) ·
[Undo landing decision](v2-0fv.8.md)

| Choice and invariant | Simplest alternative | Counter-example |
|---|---|---|
| One prepare applies one group under an undo-owned piece checkpoint. Only the owner's successful commit advances the applied cursor and counters. Abort restores the starting text and ADD mark. Each additional group starts at another owner-committed boundary. | Commit at the end of application. | Journal staging can refuse after the final mutation. An internally committed undo would already have moved the cursor and discarded the rollback root. |
| One phase tag owns either the forward draft or the replay continuation. Applying replay can resume only in its direction. Prepared replay admits reads, candidate snapshots, iteration and owner close. Other writers and history changes require the owner to close first. The caller preserves the live piece revision between replay calls and while prepared. | Treat an exhausted replay cursor as idle, or let a second prepare replace it. | A one-record undo exhausts the cursor immediately. Beginning another command at that point would attempt a nested checkpoint or overwrite the state the owner needs for abort. |
| Apply at least one mutation per prepare call before honoring its deadline. Keep the landed pre-group snapshot and a separate intermediate revision guard on MORE. Keep source checks before and after each mutation, with a final validation in owner commit. | Check the deadline before mutation, reuse the pre-group snapshot as the guard, or validate only during prepare. | An elapsed deadline would prevent progress. The legitimate intermediate revision differs from the pre-group view. A mapped backing can change after preparation while the owner stages the journal. |
| Walk the group's own records in application order. Return source descriptors by value and share the literal cut walk with piece application. Inline and external vectors keep their stored order. Iteration can restart and does no lookups or payload reads. | Diff snapshots or look up each insertion in the prepared text. | Redo may insert bytes that a later record in the same group deletes. The final snapshot has no document position for them. A joined backspace also captures segments in the opposite order from their insertion. |
| Export `undo_default_cap_bytes()` because the slot layout is private. Its constant expression multiplies the oracle depth of 32,768 (G, v1 `cfg.history_keys` at `6fec2e8:src/editor/buffers.c:256`) by `2 * sizeof(undo_slot) + 1` (P, one group, one record and one input byte). On LP64 this is 529 B per key and 17,334,272 B total (P, slot derivation beside the expression). The caller supplies the cap. | Freeze the LP64 result or use the page cost of a fresh group. | A slot layout change would silently change the default depth. Live groups can share slab pages with each other and with retired records, so page cost does not represent the published charge. |

Deleted `undo_undo`, `undo_redo`, the `undo_change` result, and the replay engine's
dirty-range accumulator and internal multi-group commit loop. The tests, fuzz harness
and benchmark now close replay groups at the public owner boundary.

## Deferred verification

The Claude worker can extend the existing mapped replay tests by refusing live
validation after successful prepare and before owner commit. Confirm that commit
returns CONFLICT, rolls back the full group, resets the replay state and preserves the
published counters, then retry. Also run the existing mapped refusal cases at
admission and after MORE and the closing fault-poll case on the resumed mutation.
The opposite-direction resume and writer calls while prepared are caller precondition
tests for the existing assertion harness.

**Validator erratum (v2-bkz).** `undo_create` uses `piece_validate_fn` from `piece/piece.h` for the same
mapped-backing check, and the owner still keeps the borrowed context alive. Piece is the lowest layer both undo and
journal include. Keeping the typedef in undo would require journal to include undo. The validator contract is signed
in the [journal wire amendment](wave1-journal-wire-amendment.md#proposed-runtime-section-of-journalh).
