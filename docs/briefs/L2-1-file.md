Bead L2-1 — L2 file: acquisition, fault registration, change detection, the save transaction's file half, reload. (Dossier bead L2-1.) sublimité v2 is a clean-slate C11 editor rebuild; your checkout is a worktree of /home/tobias/Projects/sublimite-v2 (branch v2). One worker, one bead; do exactly this scope, then stop. Routing: Claude or Muse (tests stop the process at every save step and truncate mapped files; never Codex). Depends on: L2-0a (os watch + metadata copy, landed 2.0), piece 7b (real tree; build against the model until it lands), journal L2-2a (wire: SAVE(PREPARED)'s new-file witness format).

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md (L2 includes only L0/L1 and lower L2 by PLAN's order; one public header; file_ prefix; A15; Law 2; explicit FSM enums, no coordinating booleans; no UI-thread sleeps or spins; test hooks only under FILE_TEST_HOOKS in a *_test_hooks.o).
2. docs/decisions/wave1-L2-L5-harvest.md §0 and §1 in full (v1 functions by role, the API table and what changes, acceptance lines that exist, v1 structure NOT to carry (a)–(h), tests to port).
3. docs/decisions/wave1-os-fault-lease.md "L2 and L1 integration" and "Implementation acceptance tests" (the signed acceptance for file's half of the fault service; one closing poll).
4. docs/decisions/wave0-plan-amendments.md A5, A22 (Q12: refuse visibly on OS_E_NOSPC; the copy threshold carries the note that OS_FAULT_SLOTS is derived from it), A23 (save protocol: no retention, no clone, no link; rule 2's synced PREPARED with the temp's witness; rule 5 external replacement -> the buffer's self-contained checkpoint, which the journal writes — file DETECTS; rule 6 one buffer per file identity — file supplies the identity, the index lives with the buffers owner).
5. docs/decisions/wave1-L2-derivations.md Q9, Q12, Q14 (ABSENT | PRESENT(os_stat), one change classifier); docs/decisions/wave1-L2-q2-protocol.md (the save steps and what is durable after each; the new-file witness: volume + inode + size + CRC32C over all of S, computed while writing).
6. Headers: src/os/os.h (os_rename_at with OS_RENAME_NOREPLACE, os_map_file, os_fault_*, the watch object, os_file_copy_metadata, os_waitset), src/piece/piece.h (piece_open_copy / piece_open_mapped, piece_backing, piece_snapshot), src/undo/undo.h (piece_validate_fn: must neither edit nor block, runs inside the Law 2 window), src/work/work.h, src/journal/journal.h (wire).
7. v1 evidence, read-only: /home/tobias/Projects/sublimite-v1-oracle/src/file/ and its tests (§1.6), src/savectl/savectl.c :195-294 (the reload path that moves into file).

Scope (dossier §1.2 API table, with these decided points):
- file_open_tree(file*, piece_reserve*, piece_tree**): piece_open_copy below the copy threshold, piece_open_mapped at or above it, the mapping registered with the fault service BEFORE publication; on registration failure (OS_E_NOSPC) the unpublished map and fd are released and the open is refused visibly (Q12). The copy threshold constant states its derivation and that OS_FAULT_SLOTS is derived from it.
- Change detection: v1's map_change / check / changed / resolve_keep, with the fault poll as "os_fault_poll != OK or epoch != 0", identity + timestamps for a same-size in-place overwrite (no SIGBUS), and the owned watch object from L2-0a (no global).
- The live backing-identity validator for undo (piece_validate_fn), Law-2 clean, non-blocking.
- Save, file half: file_save_begin takes a caller-owned piece_snapshot (the cutoff is the caller's); write the temp, compute the new-file witness while writing (volume, inode, size, CRC32C over all of S), fsync; the journal's PREPARED is written by the caller between temp sync and rename (L2-3); rename with OS_RENAME_NOREPLACE when the expected destination is absent (OS_E_EXIST -> FILE_ERR_CHANGED; OS_E_NOTSUP needs a stated fallback in your decision doc); dir sync; verify the path against the fd-captured identity unconditionally (Bug 2 variant (a)); an unverifiable post-rename identity is CONFLICT_CANCEL, never FAILED with a stale baseline (§1.5(g)). Metadata copy via os_file_copy_metadata. NO retention, clone or link (A23 rule 1).
- Reload acquisition moves into file (it owns mappings), through the same registered construction (§1.5(h)).
- Close: a non-blocking two-phase close (begin, then work_handle_finished per handle); zero UI-thread sleeps or spins (§1.5(b)); worker publish backpressure with a stated bound or work_continue (§1.5(c)).
- One -Wswitch-enum job-state enum per job slot instead of v1's _fin/_ready/_installed triples and the save path's seven booleans (§1.5(e)).
- Seam-table GAPs, added 2026-11-24 by the advisor from docs/decisions/wave1-savectl-spec.md "Seam table". They hold under either journal model (W1):
  (1) an owned target handle with the full comparison and error contract (acquire full tagged target expectations, retain and release immutable bindings, and on every check tell a missing path apart from a failed observation);
  (2) custody and resumption of a synced temp across asynchronous jobs (fd, temp name, metadata and witness intact), with cleanup after refusal or close;
  (3) classified publication evidence (definitely before rename, rename uncertain, installed but directory barrier failed, verified, post-rename conflict) and a barrier-only retry on the original handle, which never renames again.
- The six save step hooks (TEMP_CREATED, MID_WRITE, TEMP_WRITTEN, FSYNCED, RENAMED, DIR_SYNCED) only under FILE_TEST_HOOKS.

Acceptance (each a test, red first):
- Absent target + a file created after the absence check is never overwritten (EXIST -> CHANGED).
- The fault spec's file row and integration tests: block a running job, cancel, release; the backing survives to physical acknowledgement and is retired exactly once; the release child sees zero unmaps/frees of that backing; Law 2 release guard active with zero allocations over snapshot take/retain, validator, foreground queries.
- A same-size in-place overwrite with no SIGBUS is detected by identity; a truncation with a real fault takes the same conflict path; "never adopt a handler's substitute zeroes as deleted text".
- file_save_begin takes a snapshot; typing on the live tree during the save is unaffected.
- Each of the six step hooks fires and the process can be stopped there; fsync and dirsync failures injectable via --wrap=os_file_sync,--wrap=os_dir_sync, each asserted to fire.
- A foreign rename over the target after our rename is detected at verification (A23 T5's detection half).
- OS_E_NOSPC on a mapped open or reload: refused visibly, the old tree and registration intact on reload.
- Zero UI-thread sleeps or spins (grep proof + a close-while-saving test).
- Ported tests per dossier §1.6; fuzz/file_fuzz.c ported; bench/file_bench.c ported without layout/raster/x11.
- Each seam GAP above: a test per result class and per cleanup path.
- docs/decisions/L2-1-file.md: each mechanism with invariant, simplest alternative, defeating counter-example; v1 kept/cut.

Rules: no git (not even git add -N), no bd. No rm -rf, no mktemp with a clean-up trap, nothing outside your checkout (test files and fixtures under build/); HOME under build/; build under build/; at most nice make -j2; no starts Tue 24 Nov 03:45–05:00 CET; run date before each; report rather than run anything needing approval. Acceptance lanes: nice make -j2 check (CONTRACT_CHECK_DISPLAYS from the coordinator), check-tsan, fuzz-smoke. Report: red/green per acceptance line, lines removed vs added, anything outside scope (report only).
