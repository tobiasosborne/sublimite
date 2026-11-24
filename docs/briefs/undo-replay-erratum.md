Bead: undo replay erratum (owner-committed replay). Signed by the advisor 2026-11-24 (S1, S2). sublimité v2 is a
clean-slate C11 editor rebuild; your checkout is a worktree of /home/tobias/Projects/sublimite-v2 (branch v2). One
worker, one bead: do exactly this scope, then stop. It is a prerequisite of the edit bead and independent of the journal
write-model question (W1).

Routing: Sol. You write and run no test that injects faults, kills processes or truncates mapped files. If the erratum
needs a conflict-path test of that kind (for example, a validator refusing because a mapped backing changed), list it in
your report for a Claude worker; do not write it.

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md: L1 includes only L0 and lower L1; A15 (ASSERT only for caller bugs); Law 2 (no malloc on the typing
   path); Law 4; red-green TDD; explicit FSM enums under -Wswitch-enum; no #include of .c files in tests.
2. docs/decisions/wave1-edit-spec.md, section "E1 · undo.h erratum for signature". This is the signed contract:
   signatures, preparation, iteration, and commit or abort. Implement it as written.
3. Section "E5 · default history charge" of the same file.
4. src/undo/undo.h and src/undo/undo.c (the landed contract and the record layouts), tests/undo_test.c,
   tests/law2_undo_test.c, bench/undo_bench.c, and docs/decisions/ for undo's landing decision (v2-0fv.* undo bead).

Scope:
- Add undo_replay_prepare, undo_splice_iter_init, undo_splice_iter_next, undo_splice_span, and the
  undo_replay_direction, undo_splice_iter and undo_prepared_splice types, exactly as in the spec section.
- Use the existing undo_commit and undo_abort as the owner's close for replay. Only a successful undo_commit moves the
  applied-history position. undo_abort is valid for an applying or prepared replay.
- Delete the internally committing undo_undo and undo_redo, and migrate every caller (tests, bench, law2 suite, and any
  other caller). A caller that wants several groups loops at owner-committed boundaries.
- While a replay is prepared, block begin, replace, another prepare, cap change and revision-ending operations; reads of
  published counters stay legal. Blocking is a return code where input can cause it, and an ASSERT only where only a
  caller bug can.
- S2, the default history cap:
  - undo exports the default as a constant expression 32,768 × c, with c = 2 × sizeof(undo_slot) + 1, never the
    literal 529.
  - It is named UNDO_DEFAULT_CAP_BYTES or exported as a function if undo_slot is private to undo.c; choose and state
    why.
  - The derivation sits beside it: v1 cfg.history_keys = 32,768 at v1 src/editor/buffers.c:256 at the oracle pin (G),
    plus the per-group charge from the record layout (P).
  - A test asserts c == 529 and the cap == 17,334,272 on LP64.
  - The cap stays a caller-supplied input.

Acceptance (each a test, red first):
- Undo and redo of every landed record kind go through prepare, then iterate, then commit. The iterator yields, in
  application order, offsets, removed counts and spans that reproduce exactly the bytes the piece mutation applied
  (compare against the tree after commit).
- A mid-group yield (MORE) resumes in the same direction. A resume in the other direction is refused.
- Abort after the final mutation leaves the document and history exactly as before the prepare.
- NOMEM, RANGE or CONFLICT during preparation rolls back the whole group and leaves the log idle (inject NOMEM by the
  existing allocator hook; no fault-injection tests).
- History position unchanged until commit: published counters read during the prepared state equal those at entry.
- The Law-2 suite: zero allocations over prepare, iterate and commit on the typing path.
- A grep proof that no undo_undo or undo_redo remains.
- The E5 test above.
- A decision file docs/decisions/<bead>.md: each choice with its invariant, the simplest alternative and its defeating
  counter-example. It also records what was deleted.

Rules: no git (not even git add -N), no bd. No rm -rf, no mktemp with a clean-up trap; nothing outside your checkout is
created or removed. HOME for any tool that writes caches is a directory under your checkout's build/. Build under
build/. At most nice make -j2. If a command would need approval, do not run it: report it and continue.
Acceptance lanes: nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189, nice make -j2 check-tsan,
nice make -j2 fuzz-smoke.
Report: red and green per acceptance line; lines removed vs added; the comment-free count of undo.c and undo.h
(gcc -fpreprocessed -dD -E -P f | grep -c '[^[:space:]]'); any conflict-path test listed for a Claude worker; anything
outside scope (report only).
