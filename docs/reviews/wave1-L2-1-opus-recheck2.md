# Re-check 2: L2-1 file round 3 (Opus, adversarial)

Change re-checked: `git diff d78b003 review/l2-1e` (rounds 1–3), applied to a v2 worktree at d78b003. Earlier
reviews: `docs/reviews/wave1-L2-1-opus.md` (round 1) and `docs/reviews/wave1-L2-1-opus-recheck.md` (round 2). Round 3's
decision record is `docs/decisions/L2-1-file.md` §§2–4, 8, 10–13.

Scope (advisor): MF-B, MF-D, the 14 surviving mutants (X1–X9, X13–X16, X18), the NOTE fixes N-B, N-C and N-D, and the
`/proc/self/status` umask. Out of scope: MF-A and MF-C (v2-hy8), the busy spin (1.32), and custody hand-over.

Method:
- `build/review-probe/mutate3.py` re-expresses the 23 round-1 mutants and X1–X18 against round 3. Five patterns needed
  new text: M2, M18, X9, X10 and X16, because round 3 moved the code they target. It also adds 25 new mutants (N*, U*)
  of round 3's own lines. Each mutant is applied, the san `file_test` and rel `law2_file_test` (plus san `os_test` for
  `os_posix.c`) are rebuilt and run, and the mutant is reverted. `cmp` against the saved originals passed after the
  run. N11 cannot be written as a one-line edit: gcc -O2 rejects it with `maybe-uninitialized`. It was applied by hand
  as a two-site edit, and run and reverted the same way (`cmp` passed).
- `build/review-probe/probe4.c` holds the new attack probes, linked against the san archives with
  `--wrap=os_rename_at,os_dir_sync`. The umask probes run in `unshare -rm` with a tmpfs over `/proc`. That covers
  "/proc not mounted" and planted `status` files; neither is otherwise reachable unprivileged.

Stamp: BAT0 = Not charging, load1 0.22 at the start (M), 2026-11-25 06:27–06:38 CET.

## Verdicts on the round 3 scope

| Item | Verdict | Probe evidence | Stated root cause |
|---|---|---|---|
| MF-B | **FIXED** (two residues: NOTE R3-B, NOTE R3-C) | `noread`: 0060 stays 0060, both plain and FORCE (rc 0, VERIFIED). FORCE over another readable inode takes what is there now (0640). | Correct. "Metadata not copied" was treated as "no destination". |
| MF-D | **FIXED** | X6, X7, X9 and X13 are killed by `t_r3_verify`, `t_r3_recover` and `t_r3_held_retry_proof`. `recover`: symlink and directory give NOTREG (oserr LOOP/ISDIR), FIFO gives NOTREG, and the state is EMPTY. | Correct for N-F (ELOOP fell into the generic I/O path). |
| N-B | **FIXED** (one residue: NOTE R3-A) | `rename`: EIO, EISDIR and EACCES with nothing renamed give PRE_RENAME, DONE and zero temps, and the destination is "old". "Renamed but EIO reported" gives VERIFIED. A sweeper moving our temp away gives RENAME_UNCERTAIN and HELD; the retry gives CONFLICT and does no sync. | Correct: the errno class was trusted instead of the kernel's evidence. |
| N-C | **FIXED** | `verdict` (mapped and copy): a FOREIGN pair gives CONFLICT, the barrier runs (1 sync), the retry stays CONFLICT and the validator stays false. | Correct: the verdict was a return path, not state. |
| N-D | **FIXED** | `spell`: the relative spelling, the absolute spelling through a directory symlink and a `..`/`.` spelling all adopt (VERIFIED, `file_check` = 0). A symlink leaf is not followed: the link survives. | Correct: spellings were compared. |
| umask | **FIXED** | Real kernel: the value equals the live umask. `/proc` not mounted: 077. Planted `status` files, each giving 077: 5 digits, an empty value, a missing line, no trailing newline, a non-octal digit, and `Umask:` on the first line. `7777` gives 777 (masked to 0777). `grep umask( src/` finds no call. | Correct. |

Stale verdict (attack 5c): it cannot survive a later publish. `temp_reset` runs at `file_save_begin`, in
`temp_prepare_core` and in `file_temp_recover`, and each of them zeroes `pair`. In the probe, a later Save As from the same object
was refused only by MF-E below. N11, which keeps a stale pair across the reset, is not caught by any test.

## Mutant tables

### X1–X18 against round 3

| # | Mutation | Result |
|---|---|---|
| X1 | the rebaseline accepts any pair difference | caught: `t_r3_source_conflict` :1884 |
| X2 | the rebaseline ignores the falling link count | caught: `t_r3_source_conflict` :1884 |
| X3 | the rebaseline does not require before == baseline | caught: `t_r3_rebaseline_needs_baseline` :1923 |
| X4 | decode ignores the epoch | caught: `t_r3_decode_identity` :1986 |
| X5 | decode ignores the generation | caught: `t_r3_decode_identity` :1989 |
| X6 | the witness check skips the content CRC | caught: `t_r3_verify` :1698 |
| X7 | the witness check skips the stable re-stat | caught: `t_r3_verify` :1692 |
| X8 | `temp_unlink_own` drops the identity check | caught: `t_r3_unlink_identity` :2039 |
| X9 | the HELD retry skips the installation proof | caught: `t_r3_held_retry_proof` :1776 |
| X10 | the new-file mode ignores the mask | caught: `t_r2_new_mode` :1387 |
| X11 | the metadata open blocks | caught: `t_r2_fifo_destination` :1417 |
| X12 | discard of HELD unlinks the temp name | **not caught** (regression; see below) |
| X13 | the path stability recheck is skipped | caught: `t_r3_verify` :1687 |
| X14 | adoption for a save to another path | caught: `t_r3_saveas_keeps_baseline` :1968 |
| X15 | close readiness ignores the reload slot | caught: `t_r3_close_waits_reload` :2062 |
| X16 | the installation proof ignores the `fd_stat` mtime | caught: `t_r3_installed_touched` :2089 |
| X17 | the `file_check` fd leg ignores ctime | caught: `t_r2_keep_hardlink` :1347 |
| X18 | the null-ctx validator accepts any backing | caught: `t_r3_null_ctx_validator` :2111 |

All 14 in scope are caught, each by the test the decision file names (P, a count of the rows).

X12 was caught in round 2, by `t_r2_uncertain_rename` :1092. Round 3 changed that test's uncertain case so the temp moves
to a third name, which leaves no temp name for the mutant to unlink. Under round 3 the mutant is close to equivalent:
- HELD after RENAME_UNKNOWN keeps a temp name only when both names show our fd, or when a look failed.
- `temp_unlink_own` re-checks identity before it unlinks.

No test now pins erratum 4's rule that nothing is unlinked on uncertainty. One test would: a HELD-UNKNOWN whose temp
name still names our fd. Backlog material.

### The 23 round-1 mutants

All 23 are still caught (M1–M23; M2 and M18 re-expressed). No regression.

### New mutants of round 3's own changes

| # | Mutation | Result |
|---|---|---|
| N1 | two-name look: the temp naming our fd alone gives NOT_DONE | **not caught** |
| N2 | two-name look: UNKNOWN treated as DONE | caught: `t_r2_uncertain_rename` :1148 |
| N3 | two-name look: NOT_DONE treated as UNKNOWN (round 2) | caught: `t_r3_rename_not_performed` :1809 |
| N4 | two-name look: DONE treated as UNKNOWN | **not caught** |
| N5 | two-name look: the temp's presence counts without identity | **not caught** |
| N6 | two-name look: the destination's presence counts without identity | caught: `t_r2_uncertain_rename` :1148 |
| N7 | two-name look: an unobservable name gives NOT_DONE | **not caught** |
| N8 | kept verdict: `pair_foreign` forgets FOREIGN | caught: `t_r3_source_conflict` :1884 |
| N9 | kept verdict: FOREIGN returns before the barrier (round 2) | caught: `t_r3_source_conflict` :1885 |
| N10a | kept verdict: the HELD retry clears it | caught: `t_r3_source_conflict` :1891 |
| N10b | kept verdict: the INSTALLED_UNSYNCED retry clears it | **not caught** |
| N11 | kept verdict: survives `temp_reset` into the next save | **not caught** |
| N12 | `canonical_dest` is a no-op | caught: `t_r3_relative_own_path` :1944 |
| N13 | `canonical_dest` only for relative spellings | **not caught** (probe `spell` shows the real code adopts) |
| N14 | `canonical_dest` follows the leaf too | **not caught** |
| N15 | MF-B: present but unreadable gets the new-file mode | caught: `t_r3_unreadable_dest` :1647 |
| N16 | MF-B: FORCE over another inode gets the new-file mode | **not caught** (probe: the real code gives 0640) |
| N17 | MF-B: special bits travel on the unreadable path | **not caught** |
| N18 | MF-B: a vanished destination (NOENT) keeps the old mode | **not caught** |
| U1 | umask: the fallback becomes 022 | caught: `t_r3_creation_mask` :2121 |
| U2 | umask: no newline terminator required | **not caught** (probe: the real code gives 077) |
| U3 | umask: a missing line gives 0 | **not caught** (probe: the real code gives 077) |
| U4 | umask: parsed in base 10 | caught: `t_r2_new_mode` :1388 |
| U5 | umask: an empty value gives 0 | **not caught** (probe: the real code gives 077) |
| U6 | umask: the key matches without its leading newline | **not caught** (equivalent on a real kernel) |

10 of 25 are caught (P, a count of the rows). Each survivor marks a branch the code gets right that no test pins. The
probes show the real code is correct on every survivor checked. None is a trial-use defect by itself; they belong in
the review's one backlog bead (Law 8). The cheapest pins:
- a rename seam that renames and then reports EIO (N4);
- a FOREIGN pair whose barrier fails, then a retry (N10b);
- FORCE over another inode with a distinct mode (N16);
- an `openat` wrap that serves a fixture `status` file (U2, U3, U5).

## New findings: MUST-FIX

**MF-E. A copy-mode buffer cannot be saved anywhere after any foreign change to its original inode.** This is the
common small-file case, not only the mapped one. I missed it in round 2: my MF-A text said "files at or above the map
threshold", and §10 and v2-hy8 inherited that. v2-hy8 does not cover it, because its design is for mapped sources
only ("the existing `file_mode` selects private copies outside this enum").
- Input (probe `copy`):
  1. Open a small file (copy mode, `file_open_mode` = COPY) and edit it.
  2. Another tool does an atomic save over the path (write a new file, then `rename` it over).
  3. Optionally `file_resolve_keep`, which returns OK.
- Wrong result. Each of these returns FILE_ERR_CHANGED (pub PRE_RENAME), and nothing is written:
  - Save to the own path with a fresh observation;
  - Save with FORCE;
  - Save As to a fresh name.
- The only way out is a reload, which discards the edits. Data loss for the user who chose to keep their version.
- Cause:
  - `file_save_begin` always lends the backing's `file_source` (file.c:2041).
  - Prepare validates it in full (file.c:617–619).
  - For COPY, `file_source_validate` still compares the held fd's stat, ctime included, against the acquisition
    baseline.
  - The foreign rename drops the old inode's link and moves its ctime. Nothing ever re-baselines a copy source, so
    every later prepare refuses.
  - By construction, any ctime move does the same: an in-place write, `chmod`, `touch`, or adding a hard link. Only
    the rename-over case was probed.
- It is pre-existing. Round 2's `file.c.r2orig` has the same validation at :614 and the same `file_source_validate`.
  Round 3 did not cause it. It is outside round 3's scope, and it needs the advisor, because it changes what Keep
  means for copy buffers.
- Simplest fix, with a counter-example check:
  - Validate the retained source in prepare only for `FILE_MODE_MMAP`. A copy buffer's bytes are private
    (`backing_poll_changed` already returns false for COPY), so its source fd's drift cannot change the text being
    written.
  - Counter-example sought: does any consumer of a COPY source need its fd to still match? The undo and journal
    references of a copy buffer point at private bytes, and the null-ctx validator never sees a copy (there is no
    piece backing). I found none.
  - The rename pair's FOREIGN verdict for a COPY source is also meaningless. In probe `verdict` (copy), our text was
    installed and still reported CONFLICT. That is the N-C conflation, and it applies to copy as well.

## New findings: NOTEs

- **R3-A. A foreign rename between the failed rename and the two-name look can turn "installed and then moved" into
  PRE_RENAME, and file then unlinks the moved inode.**
  - Probe `rename` (R_BACK): the rename is performed and EIO is returned. A foreign rename then moves the destination
    back onto our exact temp name.
  - The look sees "the temp is ours, the destination is not" and gives PRE_RENAME. `temp_unlink_own` unlinks it, and
    the destination is now ABSENT. The displaced original and our new inode are both gone from disk. The buffer still
    holds the text.
  - It needs a same-user process to rename onto a random temp name inside one syscall gap, so it is adversarial
    rather than accidental.
  - Cheap fix: NOT_DONE also requires the destination to still match `t->expect` (classifier NONE). Without FORCE,
    that is the same observation the recheck made. Otherwise the result is UNKNOWN, which unlinks nothing. N1, N5 and
    N7 are the untested edges of the same function.
- **R3-B. MF-B under FORCE keeps the stale observation, not "what is there now".**
  - Probe: observe 0060, then `chmod 0040`; FORCE gives 0060.
  - §4 says "what is there now under FORCE". The unreadable branch uses `expect.stat.perm` because the open failed.
    But `os_stat_at(..., NOFOLLOW)` needs no read permission, so the current mode is observable.
  - Low incidence: FORCE plus an unreadable destination plus a mode change in between.
- **R3-C. Recovery of an owner-unreadable installed file is an I/O error forever.**
  - MF-B now (correctly) installs 0060 as 0060. Probe `noread`: `file_temp_recover` then gives FILE_ERR_IO with oserr
    ACCESS, because its open is `READ`. Savectl classes I/O as "retry later" and keeps the obligation for as long as
    the file stays unreadable.
  - The same class, also pre-existing: recover of an **absent** name gives FILE_ERR_IO (NOENT, probe `recover`).
    It is a definite mismatch, since the candidate is not installed, and N-F's own reasoning makes it CHANGED.
  - Rare (a crash during a save of such a file), but it is a recovery obligation that never resolves.
- **R3-D. X12 regressed to "not caught".** See the X table: no test now pins "nothing is unlinked on uncertainty".

## Attacks requested by the brief

1. **A foreign rename between the failure and the look.** Yes, in one direction: R3-A. The other foreign moves fall
   to the safe side or classify correctly:
   - a sweeper moving our temp away gives UNCERTAIN;
   - a foreign file at the temp name after a real rename gives DONE;
   - a foreign hard link at either name gives UNKNOWN.
2. **`/proc` not mounted.** The mask is 077 and new files are 0600: the documented private side. Every malformed or
   missing line also gives 077. The read happens once, so a later mount of `/proc` is not seen, as documented.
3. **A stale verdict after a later successful publish.** Not possible: the three resets above. Untested (N11).

## Lane results (final, all mutations restored)

Stamp: BAT0 = Not charging; load1 0.01 at the check start and 0.96 at the tsan start (M); 2026-11-25 06:38–06:43 CET.
`cmp` against the saved originals passed before and after the runs. Logs are `build/review-probe/r3-lane-*.log`.
- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:190-:195`: exit 0, `check: ok`.
  - check-layers: 6 isolated layer links;
  - check-san: 33 binaries, `file_test: ok (56 tests)`, `os_test: ok`;
  - check-law2: 9 release suites, `law2_file_test: ok`;
  - contract: PASS 64.
- `nice make -j2 check-tsan`: exit 0. 13 threaded suites passed, `file_test: ok (56 tests)`. No race is reported on
  the worker's rewrite of `save_dest`.
- `nice make -j2 fuzz-smoke`: exit 0. 16 fuzzers replayed their seeds, `file_fuzz` included.

## Outside scope (report only)

- MF-A, MF-C: v2-hy8, unchanged. Note MF-E above: v2-hy8's scope (mapped only) does not reach copy buffers.
- N-C's conflation of "the destination is foreign" with "the displaced source drifted" remains, as ruled. MF-E shows
  it also reaches copy buffers, where source drift cannot matter.
- N-H (custody hand-over to savectl) and the busy spin (1.32) are unchanged, as scoped.
