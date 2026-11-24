Bead EDIT-INTEGRATION: edit against the real journal, with the kill cuts (edit split, advisor 2026-11-24). SIGNED by the advisor
2026-11-24. sublimité v2 is a clean-slate C11 editor rebuild. Your checkout is a worktree of
/home/tobias/Projects/sublimite-v2 (branch v2). One worker, one bead: do exactly this scope, then stop.

Routing: Claude or Muse (process kills, real journal, mapped files). Never Codex.

Depends on: EDIT-CORE and L2-2b-1 (the journal writer) landed. Cases that need recovery (2b-2) use it once landed;
until then they are listed, not faked.

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md.
2. docs/decisions/wave1-edit-spec.md (signed), especially the kill cuts and "Contracts to check at integration".
3. EDIT-CORE's decision file and the cases it listed for this bead.
4. docs/decisions/wave1-journal-wire-amendment.md (signed runtime) and 2b-1's decision file.
5. docs/decisions/wave1-journal-write-model.md §1.5 (cuts a–f).
6. Evidence only, read-only: v1 /home/tobias/Projects/sublimite-v1-oracle/tests/journal_group_test.c,
   tests/dataloss_test.c (torn_undo_group) and tests/editor_undo_branch_test.c.

Scope:
- Replace the flat double with the real journal in the edit integration suites. The double stays for EDIT-CORE's unit
  suites.
- The kill cuts. Port v1's journal_group_test: kill at every member and marker × UNDO/REDO/REPLACE/PASTE, with
  acknowledged keys of other buffers interleaved (A28). Before ACK: pre-edit or whole-edit. After ACK: whole-edit.
- dataloss torn_undo_group: a 256 KiB replacement undo killed mid-emission restores to length 1 or 262,144, never a
  prefix.
- editor_undo_branch_test, re-targeted at edit with the real journal (savectl comes later).
- Fair service at the edit level: a large paste or undo in buffer A does not hold a one-byte key in buffer B beyond its
  own record. Assert by records and turns.
- Every case EDIT-CORE listed.
- The double against the real thing (advisor): when this bead lands, EDIT-CORE's unit suites that drove the flat
  journal double are also run once against the real journal. Any case where the double and the real journal disagree
  is reported as a defect of the double and fixed in this bead. A stand-in that is never compared with the thing it
  stands for drifts.

Acceptance: each item above as a test, red first. Law 2 on the typing path with the real journal (active && count == 0).

Rules:
- No git (not even git add -N), no bd.
- No rm -rf, no mktemp with a clean-up trap. Nothing outside your checkout, fixtures included.
- HOME under build/; build under build/.
- At most nice make -j2. Run date before each.
- Report rather than run anything needing approval.

Acceptance lanes: nice make -j2 check (CONTRACT_CHECK_DISPLAYS from the coordinator), check-tsan, fuzz-smoke.

Report: red/green per acceptance line, lines removed vs added, the comment-free count per file, anything outside scope
(report only).
