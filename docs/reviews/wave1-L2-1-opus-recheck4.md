# Re-check 4: L2-1 file round 5 (Opus, adversarial)

Change re-checked: `git diff d78b003 review/l2-1g` (rounds 1–5), applied to a v2 worktree at d0c41df with one
`git apply`. The apply was clean: the whole diff applied, docs included. Earlier reviews:
`docs/reviews/wave1-L2-1-opus.md` and `-recheck.md` to `-recheck3.md`. Round 5's record is
`docs/decisions/L2-1-file.md` §3, §4, §11b, §12 and §14.

Scope (advisor), limited: MF-F, R4-A, R4l, the §14 enumeration (checked against the code, plus a search for a fourth
MF-E-class site), a rerun of every mutant, and new mutants of round 5's four changes. Out of scope: MF-A and MF-C
(v2-hy8); R3-B, R4b–R4d and the round-3 survivors (v2-4xk); N1 (publish-by-exchange, by ruling).

Round 5 against round 4 is small (`diff build/review-probe/file.c.r4orig src/file/file.c`, 48 added and 26 removed by
the worker's count):
- `source_displaced` returns false for a COPY source (file.c:721–732);
- `observe_rename` takes `pre`, an observation taken just before the rename, in every mode (file.c:772–786 and
  804–821);
- `file_check`'s source leg runs only for MMAP (file.c:1904–1928);
- `file.h` states the recover rule (file.h:393–397).

`os_posix.c` is unchanged from round 4 (`cmp`).

Method:
- `build/review-probe/probe6.c` is probe5 plus the round-5 probes. It is linked against the san archives with
  `--wrap=os_rename_at,os_dir_sync,openat` (`build-probe6.sh`). probe5 gained one rename seam, R_PREWRITE: a foreign
  same-size write into the destination just before the real rename. Logs: `build/review-probe/probe6-*.log`.
- `build/review-probe/mutate5r.py` is mutate4 retargeted at round 5. N1, N3 and R4a–R4d are re-expressed, because
  `observe_rename` now compares with `pre`, not `t->expect`. It adds 14 new mutants (R5a–R5n). Each mutant is
  applied, the san `file_test` and the rel `law2_file_test` (plus san `os_test` for `os_posix.c`) are rebuilt and run
  in full, and then it is reverted. Log: `build/review-probe/mutate5r.log`.
- `build/review-probe/mutate5w.py` is the worker's `build/r5/mutate5.py`, copied with only its two paths changed. It
  runs each of the worker's mutants (57, see below) against the named killing test only, which is how §14 claims them. Log:
  `build/review-probe/mutate5w.log`.

Stamp: BAT0 = Charging, load1 1.23 at the start (M), 2026-11-25 20:34 CET. Other work on the box raised load1 to
12.9 during the mutant runs (M); no number below is a timing.

Codes used below: status 0 OK, 1 IO, 5 CHANGED, 12 CONFLICT. pub 1 PRE_RENAME, 2 RENAME_UNCERTAIN, 4 VERIFIED,
5 CONFLICT. Temp state 0 EMPTY, 2 INSTALLED_UNSYNCED, 3 HELD, 4 DONE. Reasons 0x20 METADATA, 0x23 adds SIZE and
MTIME, 0x25 adds INODE and MTIME, 0x27 adds INODE, SIZE and MTIME.

## Verdicts

### MF-F: FIXED

Probe `mff` runs every ordered pair of foreign changes on a copy buffer. The kinds are chmod, an in-place rewrite, an
atomic replace, and ctime-only (a same-size in-place write with the mtime restored). That is 4 × 4 = 16 cases (P).
Each case runs this sequence with the watch on:
1. change 1;
2. check, Keep, check, check;
3. a plain Save with the kept baseline;
4. check, then a watch poll that drains our own save's events;
5. change 2;
6. watch poll, check, Keep, check, Save;
7. a third change (chmod), then check.

All 16 cases give the same result:
- Change 1 is reported: 0x20 for chmod and ctime-only, 0x23 for in-place, 0x25 for atomic.
- Keep returns OK, and the next two checks are clean (0, reasons 0).
- The Save gives status 0, pub 4. The check after it is clean, and our own save's watch events leave the buffer
  clean.
- Change 2 is reported by the watch and by `file_check`: 0x20, 0x23 or 0x27, by kind. Keep clears it, and the next
  Save is VERIFIED.
- Change 3 is reported again (0x20).
- On disk: the edited text.

Keep sticks, survives a save, and leaves the buffer able to hear later changes: Keep does not make the buffer deaf.
Round 4 gave `check=1` after every Keep (recheck3, probe5b).

The mapped control is unchanged: after a chmod, check reports 0x20, Keep refuses (5), and the change is still reported
(MF-A, v2-hy8). Probe `copy` (recheck3's `p_copy_r4`) differs from round 4 in exactly one column: `file_check` after
"Keep, then plain Save" is now 0 for all four kinds (was 1). Every refusal still refuses: a non-FORCE save over an
un-kept change, and the in-place race during prepare, each give 5 / pub 1 with the foreign text on disk.

`t_r5_copy_keep_sticks` pins chmod, in-place and atomic. The ctime-only kind and the cross-kind pairs are covered by
the probe only. They take the same path-leg comparison, so this is no gap in the kill (R5c below is caught).

### R4-A: FIXED

Probe `rename` (recheck3's `p_rename_r4`, plus `p6_force`). Under FORCE with a stale expectation (a foreign atomic
save after the observation):

| Rename result | status / pub / state | Temps after publish, after discard | Destination |
|---|---|---|---|
| EIO injected, nothing renamed | 1 / 1 / DONE | 0, 0 | the foreign text |
| EACCES injected, nothing renamed | 1 / 1 / DONE | 0, 0 | the foreign text |
| EISDIR injected, nothing renamed | 1 / 1 / DONE | 0, 0 | the foreign text |
| EINVAL injected, nothing renamed | 1 / 1 / DONE | 0, 0 | the foreign text |
| real EISDIR: the name became a directory, empty or not, before publish | 1 / 1 / DONE (oserr ISDIR) | 0 | the directory |
| renamed, then EIO reported | 0 / 4 / HELD | 0 | ours |

The fd count is unchanged in every case. Round 4 gave UNCERTAIN, HELD and one leaked temp for the first three.

"Not renamed" now rests only on observation. `pre` is taken in every mode just before `os_rename_at`
(file.c:808–821), and `observe_rename` compares the after-look with it (file.c:782). No errno chooses between
NOT_DONE, DONE and UNKNOWN. The only errnos trusted are EXIST and NOTSUP, and they are trusted as before: B5, by
design. Under REPLACE, a regular-file source makes Linux report EISDIR, never EEXIST, for a directory at the name, so
EXIST is reached only through NOREPLACE.

**R3-A, re-run.** Rename performed, EIO reported, then a foreign rename moves the destination back onto our exact
temp name:
- plain: 1 / 2 / HELD; the destination is absent; the temp name holds "ours"; the retry gives CONFLICT (12 / 5); the
  temp is still there after discard;
- under FORCE with a fresh expectation: the same;
- under FORCE with a stale expectation (new): the same. `pre` names the foreign inode, and the after-look finds the
  name absent, so the result is UNKNOWN;
- with an absent destination, plain or under FORCE: 1 / 1 / DONE, and our own inode is unlinked from the temp name.
  This is recheck3's accepted case: `pre` (ABSENT) still holds, and the only inode removed is ours.

The neighbours are identical to round 4: EIO, EISDIR and EACCES with nothing renamed (plain), the sweeper (2, then
CONFLICT on retry), and renamed-but-EIO (VERIFIED).

The new refusal is also correct. Under FORCE, a failed pre-rename observation is PRE_RENAME. Before round 5, FORCE
never observed, so this is new behaviour. It is reached only when `fstatat` on a name in a directory we hold open
fails, which also blocks the rename. No new mutant pins it (R5f below).

### R4l: FIXED

The rule is in `file.h` (file.h:393–397): NOENT, NOTDIR and ACCESS mean CHANGED; LOOP and ISDIR mean NOTREG; every
other error is IO and keeps the obligation.

Probe `recover` (`p6_recover`) injects one `openat` failure on the candidate's leaf, then retries cleanly:

| Injected errno | os_err | rc / state | Clean retry |
|---|---|---|---|
| EIO, EMFILE, ENFILE, EOVERFLOW, ENODEV, ENXIO | IO | 1 / EMPTY | 0 / INSTALLED_UNSYNCED |
| ENOMEM | NOMEM | 1 / EMPTY | 0 / INSTALLED_UNSYNCED |
| EAGAIN | AGAIN | 1 / EMPTY | 0 / INSTALLED_UNSYNCED |
| EBUSY, ETXTBSY | BUSY | 1 / EMPTY | 0 / INSTALLED_UNSYNCED |
| EINTR | (retried in os) | 0 / INSTALLED_UNSYNCED | — |
| EPERM | ACCESS | 5 / EMPTY | 0 / INSTALLED_UNSYNCED |

No fd leaked. EIO and EMFILE stay I/O errors and keep the obligation, which `t_r5_recover_transient` pins.

EPERM folds into ACCESS in `map_errno`, so an EPERM refusal (an LSM or fanotify denial) is CHANGED and drops the
obligation. That is "unreadable" by the rule, so it is consistent. I note it only because the rule names ACCESS, not
EACCES.

The round-4 kind and absence probes are unchanged: symlink, FIFO and directory give 3; absent, 0060, 0000 and
NOTDIR give 5.

## The enumeration (§14) against the code

I read `file.c` end to end and listed every site that reads, compares or requires the identity or state of the
original source or of the destination. Below, the table's rows are matched with the sites I found.

**Rows confirmed.** Every row's site exists, at the stated lines (±2), with the stated function.
- A1–A3, A5–A12 and B1–B15 match.
- The kinds column is right for every row. After round 5, a source check runs for COPY only at acquisition (A10,
  A11) and through the public `file_source_validate`, as A2 says.
- The killing tests are confirmed by rerunning the worker's 57 mutants against their named tests (below).

**Sites missing from the table** (three; none changes a verdict):
1. **`read_exact`'s premature EOF → CHANGED** (file.c:1155). It is the source's size checked during the acquisition
   read, for the prefix and for a copy's bytes (both kinds). Its weakening is largely dominated by A11's fd re-stat
   (a shrink changes the size).
2. **The acquisition's errno-based kind classification**: `split_path`'s ISDIR and the open's ISDIR → NOTREG
   (file.c:1205, 1213), both kinds. B9 is the destination analogue and has a row; this one is diagnosis only.
3. **The pair's before-stat** (file.c:823–825). It is part of the pair, but A6 is cited at 721–732 and A7 at 740–758,
   and neither range includes it. The row should name 823–825, because the mode gate in round 5 acts there (through
   `displaced`).

**Rows partly wrong** (two):
- **A5's equivalence claim for A5c holds only when a source is lent.** "The poll alone is equivalent, because A2 polls
  first" is true when `t->source` is the snapshot's own backing. The public synchronous core accepts
  `file_temp_prepare(..., source = NULL, ...)` with a mapped snapshot. There, the closing poll on `sb->fault` is the
  only check, and A5c removes it. The async path always lends the source (`file_save_begin`), so no in-tree caller is
  affected. A test with `--wrap=os_fault_poll` and a NULL source would pin it.
- **A6's rationale, "a copy's old inode is nobody's evidence"** (§4 step 4, §14 A6), is not quite true. For a copy
  buffer saving to its own path, the displaced inode IS the destination. The round-4 pair was also the only detector
  of a foreign write into the destination inside the rename window. See NOTE R5-A.

**A fourth site.** There is none on any in-tree path:
- prepare (A5), the pair (A6), `file_check` (A8) and Keep (A9) now skip copies;
- A10 and A11 guard the read that fills a copy;
- copies never reach `file_backing_validate`, because a copy tree has no piece backing (piece.c:1311–1315).

What remains is a residue at the API, NOTE R5-B: the copy backing still holds its original's fd for its whole life,
and the public `file_source_validate` on it reports CHANGED forever. Probe `mff`: `src_validate=5` after every Keep.

## Mutants

**My earlier mutants: 79 rows** (P, a count of `mutate5r.log`, R5 rows excluded): recheck3's 66, its 12 R4a–R4l,
and N10b. `mutate4.py` already carried N10b, so recheck3's "66" was one short. Each row was compared with its
round-4 result (`build/review-probe/r4-results.tsv`, taken from recheck3's `mutate4.log` and `mutate4b.log`).

**Regressions: none.** Every mutant caught in round 4 is still caught. Line numbers moved because round 5 inserted
tests.

**Changes since recheck3:**

| # | Round 4 | Round 5 |
|---|---|---|
| N4 two-name look: DONE treated as UNKNOWN | not caught | **caught**: `t_r5_renamed_despite_eio` |
| N5 the temp counts as ours by presence | not caught | **caught**: `t_r5_temp_name_swapped` |
| R4l every open error is a mismatch | not caught | **caught**: `t_r5_recover_transient` :2420 |
| N1, N3, R4a–R4d | — | re-expressed against `pre` (file.c:782). Results are unchanged: N3 and R4a are caught (`t_r3_rename_not_performed`, `t_r4_rename_back` :2274); N1 and R4b–R4d still survive. |

The first re-expression of R4a did not build, because it left `pre` unused under `-Werror`. It was rerun with
`(void)pre;`, and that run is the result reported here.

The survivors are 18 (P, a count of the rows): N1, N7, N10b, N11, N13, N14, N16, N17, N18, R4b, R4c, R4d, R4g
(equivalent), R4j, U2, U3, U5 and U6. Each was already a survivor and already assigned, to v2-4xk or the backlog.

**The worker's mutants: 57, not 59.** `build/r5/mutations-all.txt` has 57 rows, with 57 distinct tags (P, `wc -l` and
`cut | sort`). The "59" in §14 and in the brief is `wc -l`'s total over that file plus `mutations.txt`, whose 2 rows
are reruns of B12b and B13b. I reran all 57 with the worker's own script, against each one's named test:
- 56 are killed and A5c survives (equivalent);
- every row's result matches the worker's file, line numbers aside;
- every row's named killing test is the one that fails, so every "Killed by" cell in §14 is confirmed.

The count is a Law-4 slip: "59 mutants in all (P, the rows of …)" should read 57, with 56 killed.

### New mutants of round 5's four changes

| # | Mutation | Result |
|---|---|---|
| R5a | MF-F: the copy case falls into the mapped source leg (round 4) | caught: `t_r2_abandon_while_publishing` :1279, the first test to fail in the full run (also `t_r5_copy_keep_sticks`, A8a) |
| R5b | MF-F: the source leg is dropped for both kinds | caught: `t_sigbus` :573 |
| R5c | MF-F: the path leg is skipped for a copy, the copy's only leg | caught: `t_change_replace_keep` :537 |
| R5d | MF-F: Keep of a copy does not adopt the path | caught: `t_r4_copy_after_foreign_save` :1118 |
| R5e | R4-A: `observe_rename` gets `t->expect` again (round 4) | caught: `t_r5_force_not_renamed` :2393 |
| R5f | R4-A: under FORCE, a failed pre-rename observation proceeds with the expectation | **not caught** |
| R5g | R4-A: under FORCE, `pre` is replaced by a present expectation | caught: `t_r5_force_not_renamed` :2393 |
| R5h | R4-A: an absent `pre` never proves NOT_DONE (the safe side, Save As) | **not caught** |
| R5i | R4l: recover classes NOMEM as CHANGED | **not caught** |
| R5j | R4l: recover classes AGAIN and BUSY as CHANGED | **not caught** |
| R5k | R4l: recover classes IO as CHANGED (EIO and EMFILE alike) | caught: `t_r5_recover_transient` :2420 |
| R5l | third site: the pair runs for a copy (mode gate dropped) | caught: `t_r5_copy_pair` :2453 |
| R5m | third site: the pair never runs, mapped included | caught: `t_r2_validate_after_own_save` :1332 |
| R5n | third site: a copy's pair runs, but its verdict is dropped | **not caught**. Equivalent: the stored copy ctime is read only by the public validator. |

9 of 14 are caught (P, a count of the rows); R5n is equivalent.
- **R5f.** The FORCE refusal on a failed pre-rename observation is new in round 5 and unpinned. The suite already
  wraps `os_stat_at`, so a pin is cheap.
- **R5h.** It moves only to the safe side (UNCERTAIN instead of unlinking our own inode), so no test can call it
  wrong. It marks that recheck3's accepted Save As case is not pinned either way.
- **R5i and R5j.** `t_r5_recover_transient`'s two errnos are one class: EMFILE maps to OS_E_IO like EIO
  (`map_errno` default, os_posix.c). So the suite pins IO only. NOMEM, AGAIN and BUSY stay I/O in the code (probe
  `recover`), but a wrong classification of them would go unseen. One more errno, ENOMEM, would pin a second class.

None is a trial-use defect. They go to the backlog bead (Law 8).

## New findings: MUST-FIX

None.

## New findings: NOTEs

- **R5-A. Round 5's third-site fix removes the only detector, for copy buffers, of a foreign write into the
  destination during the rename.**
  - Input (probe `window`, the R_PREWRITE seam):
    1. Open a copy buffer and edit it.
    2. Save to its own path with a fresh baseline.
    3. A foreign same-size write into the destination lands after publish's recheck and the pair's before-stat, just
       before our rename.
  - Result: round 5 gives status 0, pub 4 (VERIFIED), a clean `file_check`, and the foreign write is silently gone
    with the displaced inode. Round 4 gave 12 / 5 (CONFLICT). The mapped buffer still gives CONFLICT, in both rounds.
  - Why NOTE, not MUST-FIX: this is a sub-window of N1 (recheck-then-REPLACE), which is open by ruling until
    publish-by-exchange. The larger part of N1, from the recheck to the before-stat, was never covered for either
    kind. The pair also could not tell this case from R4-B's harmless write through another hard link after the
    rename. So round 5 trades a partial, noisy detector for no false CONFLICT.
  - What should change: §4 step 4 and §14 A6 should say what was given up ("a write into the displaced destination
    during the rename is no longer reported for copies; N1"), not "nobody's evidence". `file.h`'s publish comment
    still says "when the custody's source is the displaced inode", without "mapped". The advisor may want a test
    that pins the chosen side.
- **R5-B. The copy backing's retained fd is now dead weight, and the public validator on it never clears.**
  - Nothing in file.c reads a copy backing's `fd` after acquisition except the public `file_source_validate`, which
    then reports CHANGED forever after any foreign change of the original (probe `mff`, `src_validate=5` after Keep
    and after a VERIFIED save).
  - The held fd also pins the displaced original's blocks after any atomic replace or own save, up to the copy
    threshold of 256 MiB (file.h), until the buffer closes. And it costs one descriptor per open copy buffer.
  - The simplest fix by round 5's own rule: close the copy's fd once its bytes are read, and have
    `file_source_validate` return OK for COPY, as `backing_poll_changed` already does. The alternative is to document
    it as "MAPPED only" in `file.h`. No in-tree caller acts on the value today, so this is backlog (Law 8) unless
    savectl's bead will call it.

## Lane results

Every mutation was restored before the runs. `cmp` of `src/file/file.c` and `src/os/os_posix.c` against
`build/review-probe/*.r5orig` passed before the lanes and after them.

Stamp: BAT0 = Charging. load1 was 12.98 at the start and 20.94 at the end of `check` (M); other work was running on the
box. 2026-11-25 20:47–21:07 CET. Logs: `build/review-probe/r5-lane-*.log`.

- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:190-:195`: exit 0, `check: ok`.
  - check-layers: 6 isolated layer links passed;
  - check-san: 33 test binaries passed, `file_test: ok (71 tests)`, `os_test: ok`;
  - check-law2: 9 release suites passed, `law2_file_test: ok`;
  - contract: PASS 64.
- `nice make -j2 check-tsan`: exit 0. 13 threaded suites passed, `file_test: ok (71 tests)`.
- `nice make -j2 fuzz-smoke`: exit 0. 16 fuzzers replayed their seeds.

## Outside scope (report only)

- MF-A and MF-C (v2-hy8) are unchanged: probe `mapped` gives Keep 5, and a FORCE save with a fresh observation gives
  5 / pub 1.
- R4-B: probe `verdict` (thr=0) now gives VERIFIED, a clean check, and successful later FORCE and Save As saves.
  Round 4 gave pub 5. This is the third-site fix. thr=1 (mapped) is unchanged at pub 5, which is MF-A territory.
