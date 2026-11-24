Bead: piece_validate_fn erratum. Approved by the advisor 2026-11-24 at signature of the journal runtime section. The
validator type that undo and journal both need moves to the lowest layer both include. sublimité v2 is a clean-slate
C11 editor rebuild. Your checkout is a worktree of /home/tobias/Projects/sublimite-v2 (branch v2). One worker, one bead:
do exactly this scope, then stop. It must land before L2-2b-1 starts.

Routing: Sol. There is no behaviour change, so the lanes are the proof. It runs in parallel with the N2 bead; the files
are disjoint (piece.h, undo.h, undo.c and their tests; nothing under src/journal/).

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md (one public header per module; module prefixes; A15).
2. docs/decisions/wave1-journal-wire-amendment.md, the block "Place the following declaration after piece_backing in
   piece/piece.h". It gives the declaration and its comment as signed.
3. src/piece/piece.h (frozen; this is a signed erratum to it), src/undo/undo.h and src/undo/undo.c (1.24 and 1.25), and
   every user of undo_validate_fn in src/, tests/, fuzz/ and bench/ (grep).

Scope:
- Declare `typedef bool (*piece_validate_fn)(void *ctx, const piece_backing *backing);` once in src/piece/piece.h,
  immediately after piece_backing, with the signed comment.
- Delete undo_validate_fn from undo.h and use piece_validate_fn in its place everywhere.
- Undo's existing lifetime wording (the owner keeps ctx alive) stays, adjusted only to name the shared type.
- No other change. No behaviour change, and no new journal code.
- A one-paragraph erratum note appended to the piece decision file docs/decisions/v2-0fv.17.md, and to
  docs/decisions/v2-bmw.md for undo: what moved and why (the advisor's reason: a type both undo and journal need belongs
  at the lowest layer both include; the alternative makes the journal include undo).

Acceptance:
- A grep proof that undo_validate_fn is gone.
- The tree builds.
- All three lanes are green, unchanged in counts:
  - nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189
  - nice make -j2 check-tsan
  - nice make -j2 fuzz-smoke
- tools/check_exports.py and the layer DAG checks pass (part of check).

Rules:
- No git (not even git add -N), no bd.
- No rm -rf, no mktemp with a clean-up trap. Nothing outside your checkout.
- HOME for cache-writing tools under build/. Build under build/.
- At most nice make -j2.
- Report rather than run anything needing approval.

Report: each lane's exit code and summary lines, lines removed vs added, the comment-free counts of piece.h and undo.h
before and after, and anything outside scope (report only).
