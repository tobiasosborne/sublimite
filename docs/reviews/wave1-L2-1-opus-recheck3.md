# Re-check 3: L2-1 file round 4 (Opus, adversarial)

Change re-checked: `git diff d78b003 review/l2-1f` (rounds 1–4), applied to a v2 worktree at 4e99cc1. Earlier
reviews: `docs/reviews/wave1-L2-1-opus.md`, `-recheck.md` and `-recheck2.md`. Round 4's record is
`docs/decisions/L2-1-file.md` §4, §11a and §13.

Scope (advisor): MF-E, R3-A, R3-C, a rerun of every mutant, and new mutants of round 4's three changes. Out of
scope: R3-B, R3-D, X12 and the 15 round-3 survivors (bead v2-4xk); MF-A and MF-C (v2-hy8).

Round 4 against round 3 is small (`diff file.c.r3orig src/file/file.c`): the source validation moved under the
`sb != NULL` gate in `temp_prepare_core` (file.c:606–621); `observe_rename` gained the destination-expectation clause
(file.c:770); `file_temp_recover` classes NOENT, NOTDIR and ACCESS as CHANGED (file.c:1019). `os_posix.c` is unchanged
(`cmp`).

Method:
- `build/review-probe/probe5.c` is probe4 plus round-4 probes, linked against the san archives with
  `--wrap=os_rename_at,os_dir_sync`. `build/review-probe/probe5b.c` checks `file_check` after Keep. Logs:
  `build/review-probe/probe5.log`, `probe5b.log`.
- `build/review-probe/mutate4.py` is mutate3 retargeted at the round-4 file.c. M21, N1 and N3 are re-expressed
  because round 4 rewrote the lines they target. N11 now runs as a scripted three-site edit (a file-scope last
  verdict that `temp_reset` restores), so it builds. It adds 12 new mutants (R4a–R4l). Each mutant is applied, the
  san `file_test` and the rel `law2_file_test` (plus san `os_test` for `os_posix.c`) are rebuilt and run, and then it
  is reverted. Log: `build/review-probe/mutate4.log`.

Stamp: BAT0 = Discharging, load1 3.33 at the start (M; other work was running on the box), 2026-11-25 19:41 CET.

Codes used below: status 0 OK, 1 IO, 3 NOTREG, 5 CHANGED, 12 CONFLICT. pub 1 PRE_RENAME, 2 RENAME_UNCERTAIN,
4 VERIFIED, 5 CONFLICT. Temp state 0 EMPTY, 2 INSTALLED_UNSYNCED, 3 HELD, 4 DONE.

## Verdicts

### MF-E: FIXED, for saves (Keep has a residue: MF-F below)

My probe4 scenario: open a small file in copy mode, edit it, let a foreign atomic save land, then Keep. Probe
`copy_after_rename(keep=0)` gives:
- Save with the new observation: status 0, pub 4;
- Save with FORCE: status 0, pub 4;
- Save As: status 0, pub 4;
- on disk, the edited text at both names.

Without Keep it gives the same. Round 3 refused all three with CHANGED.

`p_copy_r4` runs four kinds of foreign change on a copy buffer: an atomic save, an in-place rewrite, a `chmod`, and a
same-size in-place rewrite with the mtime restored (ctime moved). Each kind gives the same results:

| Save | Result | On disk |
|---|---|---|
| no Keep, plain, stale baseline | status 5 (CHANGED), pub 1 | the foreign text, untouched |
| no Keep, FORCE | status 0, pub 4 | the edited text |
| Keep, then plain Save | status 0, pub 4 | the edited text |
| Save As to a fresh name | status 0, pub 4 | the edited text |
| foreign same-size in-place write at FSYNCED (after the write, before publish's recheck), plain | status 5, pub 1 | the foreign text, untouched |

- **A non-FORCE save over an un-kept foreign change still refuses.** The destination expectation catches each of the
  four kinds, the mtime-restored rewrite included, because `file_expectation_diff` compares ctime and the path names
  the same inode.
- **A mapped buffer still refuses where it must.** `t_r4_mapped_still_refuses` passes. In probe `mapped`, a ctime-only
  change (`chmod`) refuses Keep (5), and a FORCE save with a fresh observation also refuses (5, pub 1). That is MF-A's
  territory (v2-hy8), unchanged.
- **Attack: can a copy buffer's save now overwrite a foreign destination it should have protected? None found.** The
  only check removed is the fd stat of the copy's original inode. For a save to the own path, the path-based
  expectation compares the same fields on whatever the path names now, so it dominates the fd check. A foreign change
  to that inode is visible at the path. A rename-over shows as INODE. A save to another path never involved the
  source. The single exception is FORCE, which skips the destination check by contract.
- **The gate keys on the snapshot (`piece_view_backing(view) != NULL`), not on `t->source`'s mode.** For a copy tree,
  `piece_open_copy` leaves no mapped core, so the view's backing is NULL (piece.c:1311). The two keys agree whenever
  the snapshot comes from the lent file's own tree.

**The copy test.** `t_r4_copy_after_foreign_save` now pins the right behaviour:
- The first three cases would each fail under round 3.
- The last case expects the refusal at prepare (SAVE_DONE, CHANGED) and the foreign text on disk. That is correct: the
  metadata step sees another inode at the name and refuses without FORCE (§4 step 5). The discarded first version
  expected PREPARED, which was wrong.
- The test checks `file_check` only before Keep, never after. That gap hides MF-F.

### R3-A: FIXED

Re-run (`p_rename_r4`, and `t_r4_rename_back` in the suite). The rename is performed and EIO is reported. Then a
foreign rename moves the destination back onto our exact temp name. The result:
- status 1, pub 2 (UNCERTAIN), state 3 (HELD);
- the destination is absent and the temp name still holds "ours", so nothing is unlinked;
- the retry gives CONFLICT (12, pub 5) with no sync;
- after discard, the temp is still there (HELD never unlinks).

Under FORCE the result is the same. With an absent expectation (a Save As), the same race gives PRE_RENAME and our own
inode is unlinked. That is acceptable: the expectation (ABSENT) still holds, and the only inode removed is ours.
Nothing foreign is unlinked in any case.

Regression probe (EIO, EISDIR and EACCES with nothing renamed, the sweeper, renamed-but-EIO): identical to round 3.
The NOT_DONE cases keep their old results only while the expectation is fresh (see NOTE R4-A).

### R3-C: FIXED

Probe `recover4`. The fd count is the same before and after each call.

| Name | rc | oserr | state |
|---|---|---|---|
| mode 0060 | 5 (CHANGED) | ACCESS | EMPTY |
| mode 0000 | 5 | ACCESS | EMPTY |
| absent | 5 | NOENT | EMPTY |
| parent is a regular file | 5 | NOTDIR | EMPTY |
| readable control | 0 | — | INSTALLED_UNSYNCED |

The round-3 kind probe is unchanged: a symlink, a FIFO and a directory each give NOTREG. `noread` (recover after MF-B)
now says "classified". `t_r4_recover_unreadable_absent` pins ACCESS and NOENT. NOTDIR is pinned only by the probe
(mutant R4j).

## Mutants

All 23 + 18 + 25 = 66 mutants were rerun (P, a count of `mutate4.log` + `mutate4b.log`). Each was compared with
`recheck2` row by row.

**Regressions: none.** Every mutant caught in round 3 is still caught. Line numbers moved because round 4 inserted
tests.

**Changes since the last re-check:**

| # | Round 3 | Round 4 |
|---|---|---|
| X12 discard of HELD unlinks the temp name | not caught | **caught**: `t_r4_rename_back` :2239. The rename-back case leaves our fd at the temp name under HELD, which is the test recheck2 asked for. R3-D is closed by round 4's own test. |
| N1 temp naming our fd suffices for NOT_DONE | not caught | still not caught. It is now close to equivalent: "both names are ours" plus "the destination matches the expectation" needs an expectation of our own new inode. |
| N11 a stale pair survives `temp_reset` | not caught (applied by hand) | still not caught. It now builds as a scripted three-site edit. |
| M21 prepare skips the source validation | caught (:1577) | caught: `t_r2_source_validation` :1585. Re-expressed under the mapped gate. |
| N3 NOT_DONE treated as UNKNOWN | caught (:1809) | caught: `t_r3_rename_not_performed` :1817. Re-expressed. |

The other round-3 survivors (N4, N5, N7, N10b, N13, N14, N16–N18, U2, U3, U5, U6) still survive, as expected. They
belong to v2-4xk.

### New mutants of round 4's three changes

| # | Mutation | Result |
|---|---|---|
| R4a | R3-A: NOT_DONE ignores the destination expectation (round 3) | caught: `t_r4_rename_back` :2235 |
| R4b | R3-A: the destination check compares presence only | **not caught** |
| R4c | R3-A: the destination check compares identity only | **not caught** |
| R4d | R3-A: the destination check ignores metadata (ctime, perm) | **not caught** |
| R4e | MF-E: the source is validated for every buffer again (round 3) | caught: `t_r4_copy_after_foreign_save` :1095 |
| R4f | MF-E: the gate is inverted (copy validated, mapped poll only) | caught: `t_r2_source_validation` :1585 |
| R4g | MF-E: the mapped closing poll is dropped (validation kept) | **not caught**. Equivalent: `file_source_validate` polls first (file.c:324). |
| R4h | R3-C: ACCESS stays an I/O error | caught: `t_r4_recover_unreadable_absent` :2261 |
| R4i | R3-C: NOENT stays an I/O error | caught: `t_r4_recover_unreadable_absent` :2265 |
| R4j | R3-C: NOTDIR stays an I/O error | **not caught**. Probe `recover4` shows the real code gives CHANGED. |
| R4k | R3-C: ACCESS classed NOTREG | caught: `t_r4_recover_unreadable_absent` :2261 |
| R4l | R3-C: every open error is a mismatch | **not caught**. A transient EIO, EMFILE or ENOMEM on restart would then drop a recovery obligation. The real code keeps I/O (file.c:1013). |

6 of 12 are caught (P, a count of the rows). One survivor, R4g, is equivalent.
- R4b–R4d mark that only the "absent" outcome of R3-A is pinned. A rename-back where the name then holds some other
  present file is not.
- R4j and R4l are cheap pins: a recover through a path whose parent is a regular file, and an `openat` wrap that
  returns EIO for recover's open.
- None is a trial-use defect by itself. They go to the review's backlog bead (Law 8), alongside v2-4xk.

## New findings: MUST-FIX

**MF-F. Keep on a copy buffer does not stick: after any foreign change, `file_check` reports a change forever.**
This is the Keep half of MF-E's root cause. It is pre-existing: `file_check` and `file_resolve_keep` are unchanged in
round 4.
- Input (probe `probe5b`):
  1. Open a small file (copy mode).
  2. A foreign tool changes it: an atomic save, an in-place rewrite, or only a `chmod`.
  3. `file_check` reports it (correct).
  4. `file_resolve_keep` returns OK.
- Wrong result: the very next `file_check` reports a change again. The reasons are 0x20 (METADATA) after an atomic
  save or a `chmod`, and 0x23 after an in-place rewrite. A second Keep changes nothing. It stays true after a
  VERIFIED save of the kept text too (probe `copy[*] keep=0 then plain Save: ... file_check=1`).
- For the user: a "changed on disk" state that Keep can never dismiss on the common small-file case, for example after
  any `git checkout` or formatter run. The only way out is a reload, which discards the kept edits. That is wrong
  state on screen (Law 8).
- Cause: `file_check`'s fd leg (file.c:1887–1905) compares the backing's held fd stat, ctime included, with the
  backing's acquisition baseline for every mode, and `file_resolve_keep` re-baselines only `f->baseline` (the path
  leg). For COPY, nothing re-baselines the backing (`case FILE_MODE_COPY: break;`, file.c:1930).
- Simplest fix, by round 4's own rule (source drift matters only where bytes are read from the source): run the fd
  leg only for `FILE_MODE_MMAP`, as prepare now does.
  - Counter-example sought: does a copy buffer need the fd leg? The path leg sees every change to whatever the path
    names, and a copy's text is private. The fd leg's one test, `t_r2_keep_hardlink` (X17), opens its file mapped
    (`open_file(&f1, a, 1)`), so it is unaffected.
  - The alternative, re-baselining the copy backing's stat at Keep, also works, but it keeps a meaningless
    comparison.
- Advisor's call: Keep was scoped as "a separate check" in round 4, but this is what makes Keep work at all for copy
  buffers.

## New findings: NOTEs

- **R4-A. FORCE with a stale expectation turns a definite "nothing renamed" into RENAME_UNCERTAIN and leaks the temp
  file.** This is a regression caused by R3-A's fix.
  - Input (probe `r4rename`):
    1. Observe the destination.
    2. A foreign atomic save lands.
    3. Save with FORCE, using the stale expectation, as the suite itself does (`t_r4_copy_after_foreign_save`, FORCE
       case).
    4. The rename fails with nothing renamed: EIO or EACCES (injected), or a real EISDIR because the name is now a
       directory. Prepare under FORCE accepts a directory at the name.
  - Result: status 1, pub 2 (UNCERTAIN), state HELD. The retry gives CONFLICT. After discard, the `.tmp` file is still
    in the user's directory (temps = 1). Round 3 gave PRE_RENAME, and the temp was unlinked.
  - Cause: under FORCE, the publish recheck is skipped, so `t->expect` is not what was at the name just before the
    rename. The new clause then rejects every NOT_DONE.
  - Safe side: nothing is lost. The costs are a stray temp file and a misleading UNCERTAIN.
  - Fix: compare the look with an observation of the destination taken immediately before `os_rename_at`. Without
    FORCE that observation is the recheck's, which equals `t->expect`, so the behaviour is unchanged. Under FORCE it is
    what the rename could have displaced. The counter-example still holds: a rename-back leaves the name absent, which
    differs from that observation, so the result is UNKNOWN.
- **R4-B. A copy buffer's FOREIGN rename pair still reports CONFLICT for installed text.** Probe `verdict` (thr=0):
  pub 5, with our text installed. This is §12's "report only" item and N-C's conflation, unchanged. A later FORCE save
  and a later Save As of the same copy object now succeed (pub 4). Round 3 refused them at prepare, which was MF-E.

## Lane results

Every mutation was restored before the runs: `cmp` of `src/file/file.c` and `src/os/os_posix.c` against
`build/review-probe/*.r4orig` passed before and after.

Stamp: BAT0 = Charging, load1 2.06 at the start and 1.06 at the end of `check` (M), 2026-11-25 19:55–19:58 CET.
Logs: `build/review-probe/r4-lane-*.log`.

- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:190-:195`: exit 0, `check: ok`.
  - check-layers: 6 isolated layer links passed;
  - check-san: 33 test binaries passed, `file_test: ok (60 tests)`, `os_test: ok`;
  - check-law2: 9 release suites passed, `law2_file_test: ok`;
  - contract: PASS 64.
- `nice make -j2 check-tsan`: exit 0. 13 threaded suites passed, `file_test: ok (60 tests)`.
- `nice make -j2 fuzz-smoke`: exit 0. 16 fuzzers replayed their seeds, `file_fuzz` included.

## Outside scope (report only)

- R3-B is unchanged: probe `noread` under FORCE, after a chmod to 0040, still gives 0060. It is in v2-4xk.
- MF-A and MF-C (v2-hy8) are unchanged: probe `mapped`.
