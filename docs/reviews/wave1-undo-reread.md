# L1 undo re-read · v2-0fv.8 fix round

Re-read of `review/undo2` at `a80bb6b`, applied as `build/review/undo2-on-v2.diff` onto `v2` at `82c3cc0` with
`git apply --binary`. It applied cleanly. The questions come from `docs/reviews/wave1-undo-astra.md`, and its traces are
the test cases here. Tags: **G** marks a signed or frozen requirement, **P** a deduction from the cited code or a constructed
trace, and **M** a run made in this checkout on 2026-11-23 at about 23:30 CET. That is outside the Tue 03:45–05:00 quiet
window. These are functional runs only, and no number below is a timing.

Reviewer traces live in `build/review/undo_reread_test.c` and are not part of the change. They were built by hand with the
san flags (`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`, ASan+UBSan, `-O1 -g`) against `build/san/libtext.a`,
`libcore.a` and `tests/piece_model.o`, with `--wrap=os_vm_reserve,os_fault_poll` for `piece_harness.h`.

## 1. MUST-FIX 1: count-record provenance. CLOSED

There are three sources of `nl` records for `piece_replace_spans`, and each one is legitimate:

- **Regular inverse.** `prepare_inverse` (`src/undo/undo.c:460`) re-seeks to `r->off` and describes `[off, off+removed)`
  with `end`. Every stored record is therefore an exact iterator description of a clipped span on the live view (P).
- **Burst extension.** `erase_extension` (`:508`) describes the new erase from the live view, first without counts to
  establish a single run of exactly `del` bytes, then again with counts. It joins that record to the old record with
  `piece_span_nl_join`. The old record is the stored `spans[0]/nl[0]` of a one-span inverse, or the `literal.source`.
  Each is an exact description (`:477` stores a source only when the single span covers the whole removal) or an earlier
  join (`:593`, `:596`). By induction, every carried record is a description or a join of descriptions (P). The amended
  piece contract allows this (G, `src/piece/piece.h:134`).
- **Literal and forward replay.** These pass `NULL` counts (`:727`, `:739`, `:759`), so they carry no records (P).

**Same tree and source generation.** Records are described only from `piece_tree_view(u->tree)`, never from a snapshot (P),
so the "described after the abort from a snapshot" exclusion cannot occur. On a mapped tree a join runs before the
closing `trusted()` check at commit. If the source generation changed, commit returns CONFLICT and `undo_abort` discards
the draft (`:713`). The join itself is pure arithmetic. The model ASSERTs only non-emptiness, adjacency and store, which
do not depend on generation (P).

**After an abort.** An abort can never publish a pending record or the draft of an extended published record
(`p->replacement` is written back only in `publish`, `:693`). Every published ADD span therefore ends at or below the
mark of any later checkpoint (Q2), and the ADD lineage clause holds (P).

**Across a sliced replay.** Replay creates no records and grows no ADD (G, `piece_replace_spans` "No ADD"). A replay abort
restores an ADD length equal to the current one, so stored records keep their provenance across a yield, a refusal and a
retry (P).

**Evidence (M).** In `erase_threshold`, the 17-step burst ends as one join whose carried record equals a fresh exact
description `want`. The wrapped planner checks both arrays element by element. The burst runs in both directions and
straddles a chunk seam (`head = 1, tail = 2`). It also counts `len - 1 = 16` joins. In `external_store`, the nine-span
inverse matches a fresh description.

## 2. MUST-FIX 2: checkpoint ownership. CLOSED

Undo now owns the only forward restore point. `undo_begin` opens the checkpoint (`:423`), and `undo_abort` aborts it and
drops every pending record, the fresh group and the draft replacement (`:437`). A forward mutation outside a command fails
an ASSERT (`:668`). `piece_checkpoint_*` belongs to undo once a log exists (G, `undo.h` objects paragraph). Publication,
redo detachment and eviction all happen in `publish`, after `piece_checkpoint_commit` (`:717`). `undo_abort` leaves the
cursor, redo and cap untouched.

**Can published history name ADD above a restore point that can still be rolled back? No (P).**

- A forward command's mark is the ADD length at `undo_begin`. Every published span came from a committed earlier command,
  so it ends at or below that mark.
- A replay's mark equals the current ADD length, and replay appends nothing.
- No caller can open any other checkpoint.

The nesting branch of the original finding cannot be expressed: a literal capture under a caller-owned checkpoint would
need a second `piece_checkpoint_begin`, which is now undo's alone.

**Traces run against the new API (M, `undo_reread_test`: PASS):**

- **`original_trace(true)`, the review's trace verbatim.** The tree holds 16 `a`. One command inserts 16 `B` at EOF, then
  deletes `[16,32)` as a one-span equality inverse naming ADD `[0,16)`. Then `undo_abort` runs. The test checks ADD = 0
  and the history is empty (serial 0). `piece_capture(0,16)` then regrows ADD `[0,16)` with lowercase bytes. `undo_undo(8)`
  returns OK with 0 groups and the text is still 16 `a`. The stale record never existed in history.
- **`original_trace(false)`.** Same, but ADD regrows through two undo commands: insert 16 `a`, then delete them.
  `undo_undo(8)` replays exactly those 2 groups and restores 16 `a`.
- **`literal_trace`, the literal-capture variant.** The command inserts `BBB`, deletes it (a 3 B literal capture) and takes
  a 1 B literal of the original under the same checkpoint (ADD = 7). Then the command aborts (ADD = 0). The test regrows
  ADD with `piece_capture`, a literal-erase command and an insert. Undo and redo of 2 groups give back the right bytes.
- **`published_extension_abort`, which goes beyond the review.** A joined command extends a published literal record
  (its capture is `y` at ADD[1]), then aborts. A new command puts `Q` at ADD[1]. Undoing 2 groups restores `axyb`, not
  `axQb`, so the abandoned draft did not leak.

The suite's own `abandoned_add_lineage` and `typing_abort_reuse` cover the same ground (M, PASS).

## 3. MUST-FIX 3: validator on mutable mapped trees. CLOSED

- `undo_create` ASSERTs a non-NULL validator whenever the tree has a backing (`:347`). The death test
  `mapped_without_validator` confirms the abort message (M).
- `trusted()` (`:140`) calls the owner's validator and then makes one `os_fault_poll` that must return epoch 0.
  - `undo_commit` runs it once after the last capture or replace and before `piece_checkpoint_commit`. On failure it
    aborts (`:713`).
  - Replay runs it before each mutation, which is the permitted opening check that keeps the sanitizer recount off a
    known-changed source, and again after each mutation before `commit_group` (`:852`, `:865`). Resume goes through the
    same loop.
- **Reviewer trace `mapped_validator` (M, PASS).** The tree holds a 20 B mapped ORIGINAL. The trace deletes a one-span 16 B
  range, then the owner reports a same-size overwrite (validator false, epoch still 0). Undo returns CONFLICT with 0
  groups and the text unchanged. A forward replace then returns CONFLICT at commit: text, ADD = 0 and serial are all
  unchanged. With the validator true again, the undo restores the original bytes.

## 4. SHOULD-FIX 1 and 2: requested tests

| Request | Test | Verdict |
|---|---|---|
| Rollback of text, ADD, counts and cursor when the closing validation fails after a mutation | `late_fault_rollback`, forward half: the wrapped `piece_replace` succeeds, then the poll is set to FAULTED. Commit returns CONFLICT. The test checks the text, rank/select at EOF, ADD = 0 and serial = 0. Replay half: a yield, then a resume that passes the opening check, then a poll ERROR after the splice. The whole group rewinds, and the test checks text, rank/select, ADD, history, a NULL replay snapshot and a clean `c`. | PRESENT |
| Counted mapped replay compared with the byte model | `one_span_mapped`: `ph_lines_complete` counts, adopts and resolves a 513-chunk mapping. `ph_check_lines(..., want_exact = true)` compares rank and select with a private-copy model before the delete, after it and after the undo. It also checks that the restored description and counts equal the originals. The 131,072 B read ceiling is left to 7b and recorded at the top of the suite, as the review allowed. | PRESENT |
| Identical-bytes guard negative during a yield | `wrote_during_yield`: after the yield, a direct `piece_replace(0,1,"a")` with identical bytes, then resume. `assert_death` requires `now == u->replay.guard` from `src/undo/undo.c` (the ASSERT is always on). | PRESENT |
| Refusal in a later group after an earlier group completed in the same call | `later_group_refusal`: `undo_undo(2)` with the third mutation refused (the first record of the older group). The test checks `c.groups == 1`, state `z`, dirty range exactly `{2,1}` (the completed group only), text `ab`, ADD unchanged and history 1/1, then a clean retry. | PRESENT |
| Cap fixture pinned to 264 B slot / 529 B first key / 24 B per entry, with an exact fit and a cap one byte short | The constants `SLOT_BYTES = 264`, `GROUP_RECORD_BYTES = 528` and `ENTRY_BYTES = 24` carry their derivation (P, A20), independent of `sizeof`. `cap_boundary` tests caps 529 (kept, charge 529) and 528 (evicted). `external_store` asserts the active charge `528 + 9 × 24` and the held payload `9 × 24`, then caps at the exact fit and one byte below it (evicted). `typing_record` asserts `529 + i` per key. | PRESENT |

## 5. New MUST-FIX-grade defects introduced by the fix round. None found

These paths were checked by reading the code. The P traces were run as the reviewer tests above.

- **Extending a published record.** It is edited through a single draft, `p->replacement`. Nothing can be staged before
  it, because `pending_tail` reaches the published tail only when no record is pending. This makes overwriting
  `p->charge` at `:566` safe. The charge, refund and draft are discarded on every error and on CONFLICT (P).
- **Charge invariant.** A pending literal's charge always includes `r->removed`, so `p->charge - old` cannot wrap. This
  holds across literal→literal and literal→span extensions, whether published or pending (P).
- **Joining after a branch was undone.** With `join_after_branch` (M, PASS):
  - A literal below 16 B refuses capture extension once ADD has grown, because the copy end is no longer the ADD end.
    The new erase becomes a second record in the joined group.
  - At 16 B the literal turns into a joined one-run span without needing ADD contiguity.
  - Undo and redo give back the right bytes, and the restored description is ORIGINAL `{2,16}`.
- **Abort bookkeeping.** `undo_abort` frees pending vectors before it restores `u->append`. The extent saved at begin keeps
  at least one published or retired claim because `undo_maintain` is a no-op while a command is open, so the restored
  pointer is never dangling (P).

Not a finding: when a replay yields between groups it takes no guard, so a forward command can legally run before the
resume. The resumed call then targets the new history boundary. Text and history stay consistent, and nothing is lost or
corrupted (P).

## Verdict: LAND

All three MUST-FIX findings are CLOSED, and all five requested test areas are PRESENT. I found no new MUST-FIX-grade
defect (P, sections 1–5).

Runs (M, this checkout):

- `HOME=$PWD/build/home nice make -j2 build/san/tests/undo_test` exited 0.
- `./build/san/tests/undo_test` printed `undo_test: PASS`, including both `assert_death` checks.
- The hand build of `build/review/undo_reread_test.c` exited 0, and `./build/review/undo_reread_test` printed
  `undo_reread_test: PASS`.
