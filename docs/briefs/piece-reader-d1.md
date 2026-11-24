Bead: the piece span reader, erratum (i), the D1' deletion, and the undo owner-abort rule. These are one change; the
advisor signed them 2026-11-24. sublimité v2 is a clean-slate C11 editor rebuild. Your checkout is a worktree of
/home/tobias/Projects/sublimite-v2 (branch v2). One worker, one bead: do exactly this scope, then stop.

Routing: Claude (mapped reads, fault polls, death tests). Never Codex.

It is a prerequisite of EDIT-CORE. It is ONE bead because the new abort ASSERT fires in undo's present conflict tests
unless undo changes in the same commit.

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md:
   - A15: ASSERT only for caller bugs;
   - Law 2;
   - Law 4;
   - red-green TDD;
   - no #include of .c files in tests;
   - test hooks only under <MODULE>_TEST_HOOKS.
2. docs/decisions/wave1-piece-span-reader.md in full. This is the signed contract:
   - the reader piece_snapshot_read_span (whole-span range check before any access; at most one chunk per call,
     ≤ 65,536 B; 8 directory levels from the enforced 2^63 bound; fail-closed validation; three results);
   - erratum (i): a take pins ORIGINAL and exactly ADD [0, add_length at take), and a cached take must match the
     current ADD length;
   - the "D1' pass": the deletion list, the exact header text, the test changes, and the new acceptance lines.
3. docs/decisions/wave0-plan-amendments.md: A15, A26, and the 2026-11-24 signatures and D1' ruling. Also
   docs/decisions/wave1-edit-spec.md, E1 and the failure path (updated).
4. src/piece/piece.h, src/piece/piece.c, src/undo/undo.h, src/undo/undo.c (landed up to 1.26), and their tests:
   tests/piece_shape_test.c, piece_test.c, law2_piece_test.c, undo_test.c, law2_undo_test.c, and the piece and undo
   fuzzers.

Scope:
- PIECE:
  - Add piece_snapshot_read_span and its result enum as signed.
  - Erratum (i).
  - D1': a snapshot taken while a checkpoint is open is provisional. The checkpoint counts its live provisional
    snapshots. Commit turns them into ordinary snapshots. Abort ASSERTs the count is zero and regrows in place.
  - Delete, as listed in the D1' pass: piece_private_boundary, piece_spares_make, the reserve's spare boundary chunk
    and directory nodes, PIECE_EXTRA_BOUNDARY in admission and replenish, PIECE_CP_PRIVATE, the obsolete subtree
    add_high bookkeeping, and the tests of each. KEEP restore_tail.
- UNDO: write this rule ONCE in undo.h, replacing the per-call wording:
  "No undo call ever closes a checkpoint by itself: after any result other than OK or MORE from undo_begin,
  undo_replace, undo_replay_prepare (initial or resumed) or undo_commit, the owner releases its provisional pins and
  calls undo_abort; undo_abort is valid after any failed call and does nothing if that call opened no checkpoint."
  Implement it: undo_commit on a closing CONFLICT, and undo_replay_prepare on NOMEM, RANGE or CONFLICT (initial or
  resumed), return their error and leave the checkpoint open. Undo releases its own replay guard before it calls piece
  abort.
- The header texts in piece.h and undo.h, and the central A26 text in docs/decisions/wave0-plan-amendments.md: the
  request bound becomes ceil(S / e) + 9 × k, and the private-boundary term goes. These are this bead's work.

Acceptance (each a test, red first):
- The reader:
  - ORIGINAL and ADD spans, including ADD bytes below the captured length that are absent from the document;
  - the whole-span range check before any access (RANGE);
  - the per-call bound;
  - fail-closed validation through piece_validate_fn and the fault poll (CONFLICT);
  - zero allocations under the release guard (law2).
- Erratum (i): a cached snapshot across a capture-only append is not reused; a fresh take reads the new span; a
  repeated unchanged take hits the cache.
- D1':
  - abort with a live provisional snapshot dies (assert_death, exact message);
  - an owner-ordered CONFLICT path (release, then undo_abort) succeeds with a zero count.
- The undo rule:
  - every failing call (begin, replace, initial prepare, resumed prepare, commit) leaves the checkpoint open, or opened
    none;
  - undo_abort after each failing call restores the pre-call state;
  - undo_abort after a call that opened nothing is a no-op.
- The three 1.25 tests change their expectation (rollback follows the owner's abort): mapped_prepared_validator_refusal,
  the closing-poll arm of mapped_fault_poll_conflicts, the forward arm of late_fault_rollback. Also the other lifetime
  tests listed in the D1' pass (owner_replay, typing_abort_reuse, t_threads, the law2 piece test's main).
- A grep proof that the deleted symbols are gone.

Rules:
- No git (not even git add -N), no bd.
- No rm -rf, no mktemp with a clean-up trap. Nothing outside your checkout.
- HOME for cache-writing tools under build/. Build under build/.
- At most nice make -j2. Run date before each.
- Report rather than run anything needing approval.

Acceptance lanes: nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189, nice make -j2 check-tsan,
nice make -j2 fuzz-smoke.

Report:
- red/green per acceptance line;
- lines removed vs added;
- piece.c's comment-free count before and after (gcc -fpreprocessed -dD -E -P f | grep -c '[^[:space:]]'; it should
  shrink), and the counts for piece.h, undo.c and undo.h;
- anything outside scope (report only).
