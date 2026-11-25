Bead EDIT-CORE: the edit kernel against the signed journal runtime header (edit split, advisor 2026-11-24). SIGNED by the advisor
2026-11-24. sublimité v2 is a clean-slate C11 editor rebuild. Your checkout is a worktree of
/home/tobias/Projects/sublimite-v2 (branch v2). One worker, one bead: do exactly this scope, then stop.

Routing: Sol. No process kill, no truncated mapping, no real journal. Kill cuts and the real journal are EDIT-INTEGRATION
(Claude or Muse, after 2b-1). A case that seems to need them is listed for that bead, not written.

Depends on:
- N2 (v2-zh6: wire amendment + pure reader) landed;
- the validator erratum (v2-bkz: piece_validate_fn) landed;
- the piece pinned-store reader erratum (v2-40h: piece_snapshot_read_span + D1' + the undo owner-abort rule) landed as 1.31.
LANDING ORDER (coordinator's note for the advisor): EDIT-CORE can be written and tested against the double as soon as
those three land. It LANDS only after 2b-1. The L2 layer check links libL2.a whole-archive, and edit.o's journal_*
calls resolve only once 2b-1 defines them. The worker adds no stub definitions to src/journal/.
Advisor's ruling on sequencing: as written. There are no stub definitions in src/journal/, and the whole-archive
layer check is not relaxed. While EDIT-CORE waits for 2b-1, its wip branch is pushed, and the landing reads (Astra on
the production source, and the advisor) are done on the wip, so that after 2b-1 only the rebase and the merged-tree
lanes remain.

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md:
   - Law 2: no malloc on the typing path;
   - A15: caller bugs ASSERT, input and exhaustion are return codes;
   - explicit FSMs under -Wswitch-enum;
   - one public header per module;
   - no #include of .c files in tests; test hooks only under EDIT_TEST_HOOKS.
2. docs/decisions/wave1-edit-spec.md: the SIGNED edit.h with the coordinator's textual cuts. This is the contract;
   implement it as written. Its sections "One keystroke, through submit", "Command, publication and failure", "E3",
   "Transaction and burst" and "Proof obligations for the implementer" are your acceptance source.
3. docs/decisions/wave1-journal-wire-amendment.md: the SIGNED runtime section (21 functions). Edit calls
   JOURNAL_LITERAL_LIMIT, journal_range_allowed, journal_command_begin/payload/member/abort/final, journal_state and
   journal_request_result/release.
4. docs/decisions/wave0-plan-amendments.md: A13, A19, A26, A27, A28 and the 2026-11-24 signatures.
5. Headers: src/piece/piece.h (with both errata), src/undo/undo.h (1.24, with piece_validate_fn), src/journal/journal.h
   (after N2), src/os/os.h, src/work/work.h.
6. Evidence only, read-only: v1 /home/tobias/Projects/sublimite-v1-oracle/src/editor/input.c (mutate_text,
   delete_part, stage_*, finish_edit), src/view/view.h, and v1 undo.h UNDO_BURST_NS at the pin (the 300 ms burst, G).

Scope:
- src/edit/: the signed edit.h and its implementation: 5 phases, with FINAL_PENDING the only post-commit state.
  - Forward edits and replay emit their grouped members across turns before undo_commit, through
    journal_command_member.
  - Edit reads journal_state, calls undo_commit, then journal_command_final, in one dispatch.
  - An internal CONFLICT in undo_commit leads to journal_command_abort and inert members.
  - Member OK transfers the frame even on a short write, and the cursor advances once.
  - Content id 0 means unknown.
  - Restores are streamed through the piece pinned-store reader (E3), with scratch of at most one record.
- A FLAT JOURNAL DOUBLE in tests/ that honours the signed runtime contract for the calls edit uses: exact reservation;
  the final record reserved at begin and never refused; the held frame on a short write; abandoned members inert;
  health through journal_state; refusals injectable at each claim. It links into the edit suites in place of the
  journal runtime objects, and never into an archive or the shipped binary.
- THE FLAT ORACLE from the spec ("Proof obligations"): a straight-line model of buffer text, selection, history
  position and the double's committed records that never calls edit's planner. Edit is compared against it on generated
  command sequences.
- One post-submit maintenance function, the only caller of the four, in this order: piece_reclaim, then
  piece_lines_resolve, then piece_replenish, then undo_maintain. It runs outside the input-to-submit window, with no
  resolve while a replay is prepared. Its three budgets are the caller's numbers, derived at the call site (Law 4); the
  edit header says so.
- Signed errata since the edit spec was signed (2026-11-25):
  - EDIT_SAVE_FORGOTTEN (L5 item 1), now in the edit spec at edit_save_boundary. FORGOTTEN clears only the saved
    bookmark, is idempotent, needs no cutoff, and is delivered in the same UI publication that invalidates the saved
    association. INSTALLED only follows a verified durable install, and RELEASED comes once on a definitive
    non-install. The cutoff is retained while a settlement is ambiguous. Each rule is a test.
  - D1' on edit's failure path: on CONFLICT from undo_commit or any failed undo call, do journal_command_abort, then
    release the candidate snapshot, then undo_abort, in that order and in one dispatch. No undo call closes a
    checkpoint by itself (undo.h, 1.31). Test: abort with a live provisional snapshot dies; the ordered path does not.
  - Every source-coordinate literal (a restore of bytes absent from the document) is read with
    piece_snapshot_read_span (piece.h, 1.31), with scratch of at most one record.
- The burst policy: join within 300 ms, (G) v1 parity with its source line.
- Every compound command is atomic. On any refusal nothing changes and the refusal is visible.

Acceptance (each a test, red first):
- Law 2 on EVERY row of the spec's proof table, under the live release malloc guard (active && count == 0): typing,
  delete, compound replace, undo and redo.
- Refusals injected at each claim (piece admission, undo, each journal reservation and member): nothing changed, the
  refusal is visible, a later command succeeds.
- The validator refused through its callback (piece_validate_fn) before commit: CONFLICT, whole rollback, members
  inert. Use the callback, not a mapping truncation.
- A13: delete and undo of a mapped range copy at most min(length, 16 × spans) ORIGINAL bytes with O(spans) inverse
  storage; a one-span delete plus its undo reads at most 131,072 B (P, 2 × 2 × 32,768), counted by piece's read counter.
- A19: line queries are exact after any edit or undo of any size, in O(h) node visits, counted. Fixture: the 513-chunk
  delete and its undo.
- E3: a by-value restore of a large range streams with scratch of at most one record (count the double's frames and
  edit's scratch high-water).
- The maintenance order: a test fails if any of the four is called elsewhere or out of order.
- Undo review MUST-FIX 2's trace at the edit level.
- The flat oracle agrees on fuzzed command sequences (fuzz/edit_fuzz.c).
- bench/edit_bench.c: typing begin-member-commit-final p50/p99, stamped [bat]/[AC] + load; a miss is a note.

Rules:
- No git (not even git add -N), no bd.
- No rm -rf, no mktemp with a clean-up trap. Nothing outside your checkout.
- HOME for cache-writing tools under build/. Build under build/.
- At most nice make -j2.
- Report rather than run anything needing approval.

Acceptance lanes: nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189, check-tsan, fuzz-smoke.

Report:
- red/green per acceptance line;
- lines removed vs added;
- the comment-free count per file beside v1's src/editor/input.c edit paths and src/view/edits.c;
- the cases listed for EDIT-INTEGRATION;
- anything outside scope (report only).
