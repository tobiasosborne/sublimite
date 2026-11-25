Bead: L2-1 file ROUND 2 (advisor, 2026-11-25). It fixes the fresh Opus review's ten MUST-FIX findings against the
AMENDED file.h. sublimité v2 is a clean-slate C11 editor rebuild. Your checkout is a worktree of
/home/tobias/Projects/sublimite-v2. One worker, one bead: do exactly this scope, then stop.

Routing: Claude (Opus), a fresh worker; the module was written by another model. It is not a patch round. Two of the
ten (RENAME_UNCERTAIN leaking the temp; abandon or close during publish losing the outcome) are exactly what the
signed L5 errata 3, 4 and 5 respecify, so the target is the amended header.

SETUP: start from branch review/l2-1b, which is v2 plus L2-1 round 1. Apply it with this one command, run in your
checkout; it is the only git command you may run, and you make no commit:
  git -C /home/tobias/Projects/sublimite-v2 diff <your base> review/l2-1b | git apply
The coordinator gives the exact base in your task.

Read first (absolute paths under /home/tobias/Projects/sublimite-v2):
1. CLAUDE.md:
   - A15: user input never reaches an ASSERT;
   - Law 2;
   - explicit FSMs under -Wswitch-enum, with no coordinating booleans and no `default:`;
   - no UI-thread sleeps, spins or blocking calls.
2. docs/reviews/wave1-L2-1-opus.md in full. It has the ten MUST-FIX, NOTEs N1–N9, and the 23-mutation table. The
   reviewer's probes are in the reviewer's worktree under build/review-probe/; the coordinator gives their path in your
   task. Turn each probe into a red test.
3. docs/decisions/wave1-app-spec.md, the signed errata §3 (file_target as a value), §4 (file_temp_verify,
   file_temp_recover, FILE_TEMP_HELD and the custody rules) and §5 (the validator: a null ctx selects the backing's
   own source). Implement the file.h and os.h parts. The savectl and journal parts belong to their own beads.
4. docs/briefs/L2-1-file.md, the round 1 scope, which still holds, and docs/decisions/L2-1-file.md, round 1's decision
   file. Revise it in place.
5. src/os/os.h, src/piece/piece.h and src/work/work.h.

The advisor's rulings the fixer needs:
- Finding 5 (our own rename changes the old inode's ctime, and the validator then refuses intact mapped bytes): ctime
  STAYS in the comparison, because finding 6 needs it. The publish job takes an fstat of the old backing's held
  descriptor immediately before and immediately after its own rename. If the pair differs in ctime and link count
  only, the after value becomes that backing's baseline. Any other difference is CONFLICT. No other code path
  re-baselines.
- Finding 7: os gains the default creation mode as a value captured ONCE at startup, before any thread exists. That is
  the only moment the process umask can be read without a window in which another thread creates a file under an
  altered umask. For a new destination, file applies 0666 & ~that value by fchmod on the temp. There is no umask()
  call after startup. This bead carries the os change too.
  - The decision file records this as a deliberate deviation from v1's 0644 regardless of umask (v1 file.c:908/919 at
    6fec2e8). The two agree under umask 022, and v2 never widens past the user's umask.
  - Assertions: umask 022 gives 0644; umask 077 gives 0600; the temp is never wider than the result at any step.
  - Also the file-suite assertion for 1.28's mode copy: an existing destination keeps its mode.
- Finding 8: user input never reaches an ASSERT. A trailing slash and an overlong name are results with their own
  diagnosis.
- Finding 9: the destination's metadata open is non-blocking, and anything that is not a regular file is refused before
  any wait.
- NOT in this bead: the chown limitation (bead v2-jh7, a product decision for Tobias), and publish-by-exchange (N1;
  Astra's derivation, docs/decisions/wave1-publish-by-exchange.md, is for a later bead).

Acceptance (each red first):
- Each of the ten MUST-FIX as a test built from the reviewer's probe, or from the review's input where there is no
  probe.
- Each of the twelve surviving mutants (M3, M4, M5, M7, M8, M9, M11a, M11b, M12, M15, M18, M20, M21) killed by a named
  test. Show it: apply the mutant, the named test fails, revert.
- N4 in scope: custody and publication are -Wswitch-enum states, with no coordinating booleans and no `default:` cases.
- N5 in scope: no unlink or munmap on the UI thread inside a const query.
- N8 in scope: decode checks the generation of the slot it trusts.
- N2: answered in the decision file (fd_stat used for verification) or deleted.
- The fault-spec integration tests run on a MAPPED open, with fault registration and unmaps counted (MUST-FIX 10).
- The decision file says, per finding, what the root cause was, not only that the test is green.

Rules:
- No git beyond the one apply command. No bd.
- No rm -rf, and no mktemp with a clean-up trap. Nothing outside your checkout; fixtures go under build/.
- HOME under build/. Build under build/.
- At most nice make -j2. Run date before each.
- Report rather than run anything needing approval.
- Never touch /home/tobias/Projects/sublimite (v1). No input on display :0. No pkill -f.
- WRITE your decision file and report BEFORE the final lanes, then update only the lane results.

Acceptance lanes: nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189, nice make -j2 check-tsan,
nice make -j2 fuzz-smoke.

Report:
- red/green per finding, with its root cause;
- the mutant table;
- lines removed vs added;
- the comment-free count of file.c, file.h and the os files before and after, against v1's src/file core (1,664);
- anything outside scope (report only).
