# Re-check: L2-1 file round 2 (Opus, adversarial)

Change re-checked: `git diff ef078df review/l2-1c`, applied to a v2 worktree at ef078df. Round 1's review is
`docs/reviews/wave1-L2-1-opus.md`. I read it against:
- the round 2 brief `docs/briefs/L2-1-file-round2.md`, with the advisor's rulings on findings 5, 7, 8 and 9;
- the revised `docs/decisions/L2-1-file.md`;
- signed errata §3–§5 of `docs/decisions/wave1-app-spec.md`;
- the savectl spec `docs/decisions/wave1-savectl-spec.md`.

Method:
- I re-ran my round 1 probes against round 2. Two changed signatures needed adapting: `file_save_write` gained
  `source`, and `file_job_slot.state` became `.progress`.
- I added attack probes, `build/review-probe/probe3.c`.
- I re-expressed the 23 mutants and 18 attack mutants (X1–X18) against round 2's `file.c`
  (`build/review-probe/mutate2.py`). Each mutant was applied, the san `file_test` and rel `law2_file_test` were rebuilt
  and run, and the mutant was reverted. `cmp` against the saved original passed after every batch.

Stamp: BAT0 = Not charging, load1 = 0.14 (M), 2026-11-25 04:44–04:55 CET.

## The ten round 1 MUST-FIX

| # | Verdict | Probe evidence on round 2 | Root cause stated by round 2 |
|---|---|---|---|
| MF1 | **FIXED** | `uaf`: after OPEN_FAILED, `prefix_ready=0` and `file_prefix` gives NULL, 0. No ASan report. | Correct. The prefix had two owners plus a coordinating boolean. Only the UI now frees the record. |
| MF2 | **FIXED** (one residue, see N-B) | `unc`: RENAME_UNCERTAIN leads to phase HELD and +2 fds. After abandon, +0 fds. The next save is VERIFIED, and close leaks nothing. One temp stays on disk, which is mandated evidence (erratum 4). | Correct: there was no "held after publish" custody state. |
| MF3 | **FIXED** | `hang`: close becomes ready within 3 s while the open slot sits in RESULT_READY (progress 1). | Correct: readiness was logical. It is now physical (`work_handle_finished`). |
| MF4 | **FIXED** | `abpub`: phase HELD, outcome VERIFIED, `file_check=0`. The baseline was adopted from the record. | Correct. |
| MF5 | **FIXED** | `val`: after our own VERIFIED save, the validator accepts the intact mapped backing. | Correct. The rename pair re-baselines it (but see MF-C). |
| MF6 | **FIXED** | `keep`: Keep returns FILE_ERR_CHANGED (5) and the validator returns 0. The fd leg of `file_check` now compares ctime (X17 is killed by `t_r2_keep_hardlink`). | Correct. The fix refuses mapped Keep outright (see MF-A). |
| MF7 | **FIXED** | `mode` under the process umask: 022 gives 0644, 077 gives 0600. (A umask set after start is not seen, by design.) | Correct (but see MF-B). |
| MF8 | **FIXED** | `slash` and `long`: both return rc=1 (FILE_ERR_IO), with no ASSERT. | Correct. |
| MF9 | **FIXED** | `fifo`: rc=3 (FILE_ERR_NOTREG), with no block. | Correct. |
| MF10 | **FIXED** for the listed lines | All 13 round 1 survivors are killed by named tests (table below). The mapped fault-spec row is `t_r2_mapped_cancel_lease`. | Correct. The new surface has new gaps (MF-D, N-G). |

## Mutant table (round 2 `file.c`; san `file_test`, then rel `law2_file_test`)

Adaptations to round 2's code:
- M3 now targets the fsync failure specifically. It returns early through `temp_release(t, false)`, because round 2
  routes every prepare failure through one exit, which M9 targets.
- M8 replaces the close-time discard with `file_temp_forget`.
- M17 has no "path is ours" branch any more. It becomes "mapped Keep validates with the poll only".
- M22 needed `(void)t` to compile under -Werror.
- M23 uses `!is_open` (`with_prefix_msg` is gone).

| # | Mutation | Result |
|---|---|---|
| M1 | skip `os_fault_register` | caught: piece.c:1052 ASSERT (`t_open_mapped`) |
| M2 | VERIFIED without the identity check | caught: `t_save_foreign_after_rename` :737 |
| M3 | leak the temp on a temp-fsync failure | caught: `t_r2_cleanup_paths` :1402 |
| M4 | leak the temp on NOREPLACE EXIST | caught: `t_r2_exist_race` :1381 |
| M5 | leak the temp on a pre-rename CHANGED | caught: `t_r2_cleanup_paths` :1410 |
| M6 | leak the temp on discard of SYNCED | caught: `t_temp_custody_discard` :768 |
| M7 | the reaper forgets an abandoned synced temp | caught: `t_r2_cleanup_paths` :1424 |
| M8 | close_finish leaks custody | caught: `t_r2_cleanup_paths` :1445 |
| M9 | the prepare failure exit leaks the temp | caught: `t_r2_fifo_destination` :1354 |
| M10 | skip the directory sync | caught: `t_save_dirsync_retry` :692 |
| M11a | the sync retry renames again | caught: `t_save_dirsync_retry` :700 (rename counter) |
| M11b | the async retry re-runs the publish (FORCE) | caught: `t_r2_barrier_retry_async` :1469 |
| M12 | absent target renamed with REPLACE | caught: `t_r2_exist_race` :1377 |
| M13 | drop the high inode word | caught: `t_expectation` :299 |
| M14 | skip retire before unmap | caught: `t_open_mapped` :361 |
| M15 | `file_check` drops the fault poll | caught: `t_r2_check_fault_poll` :1489 |
| M16 | validator polls only, with no fd identity | caught: `t_change_in_place` :425 |
| M17 | mapped Keep polls only (ignores ctime) | caught: `t_change_in_place` :427 |
| M18 | rename errors classed PRE_RENAME, temp unlinked | caught: `t_r2_uncertain_rename` :1085 |
| M19 | post-rename mismatch reported as a plain failure | caught: `t_save_foreign_after_rename` :737 |
| M20 | a verified async save does not adopt the baseline | caught: `t_r2_abandon_while_publishing` :1185 |
| M21 | prepare skips source validation | caught: `t_r2_source_validation` :1514 |
| M22 | metadata copy skipped | caught: `t_save_metadata_xattr` :787 |
| M23 | reload NOSPC falls back silently to a copy | caught: `t_reload_nospc` :530 |

All 23 rows are caught (P, a count of the rows), M11a and M11b included. Round 2's own table credits M20 to
`t_r2_barrier_retry_async`. My run caught it first in `t_r2_abandon_while_publishing`, an earlier test in the run
order.

Attack mutants on round 2's new code:

| # | Mutation | Result |
|---|---|---|
| X1 | the rebaseline accepts any pair difference | **not caught** |
| X2 | the rebaseline ignores the falling link count | **not caught** |
| X3 | the rebaseline does not require before == baseline | **not caught** |
| X4 | decode ignores the epoch | **not caught** |
| X5 | decode ignores the generation (N8) | **not caught** |
| X6 | `witness_check` skips the content CRC | **not caught** |
| X7 | `witness_check` skips the stable re-stat | **not caught** |
| X8 | `temp_unlink_own` drops the identity check | **not caught** |
| X9 | the HELD retry skips the installation proof | **not caught** |
| X10 | the new-file mode ignores the mask | caught: `t_r2_new_mode` :1324 |
| X11 | the metadata open blocks (no NONBLOCK) | caught: `t_r2_fifo_destination` :1354 |
| X12 | discard of HELD unlinks the temp name | caught: `t_r2_uncertain_rename` :1092 |
| X13 | recover skips the path stability recheck | **not caught** |
| X14 | VERIFIED adoption for a save to another path | **not caught** |
| X15 | close readiness ignores the reload slot | **not caught** |
| X16 | `verify_installed` ignores the `fd_stat` mtime (N2) | **not caught** |
| X17 | the fd leg of `file_check` ignores ctime | caught: `t_r2_keep_hardlink` :1284 |
| X18 | the null-ctx validator accepts any backing | **not caught** |

4 of the 18 are caught (P, a count of the rows).

## New findings: MUST-FIX

**MF-A. After a foreign replace-by-rename of a mapped file, the user's edits cannot be saved anywhere.** This is a
consequence of the MF5/MF6 ruling. It is wider than §10 of the decision file says, and it needs the advisor.
- Input (probe `saveas`):
  1. Map a file (threshold 1) and edit it.
  2. Rename another file over its path, as an atomic save by another tool, `git checkout` or logrotate would.
  3. The mapped bytes are intact (`intact=1`).
- Wrong result:
  - `file_resolve_keep` returns FILE_ERR_CHANGED;
  - an async **Save As to a fresh name** (ABSENT) returns FILE_ERR_CHANGED and writes nothing;
  - `file_save_write` with the source lent does the same.
- The link drop moves the old inode's ctime, and prepare's source validation (file.c:612–615) then refuses forever.
  Savectl's conflict checkpoint also needs "validated published text", so it cannot capture the mapped spans either.
  The user's remaining choice is reload, which discards the edits.
- Section 10 names Keep and by-value capture, but not Save and Save As.
- Round 2's counter-example is sound, so the rule cannot simply go back to adopting ctime. An inode watch does not
  rescue it: stores through another process's shared mapping raise no IN_MODIFY.
- A sound discriminator I can see: a content hash of the mapped file, taken off path after acquisition. On a ctime-only
  drift, re-hash off path; equal bytes re-baseline. A rewrite with identical bytes is harmless by definition.
- Alternatively, the advisor can accept "reload or lose edits" for files at or above the map threshold, since such
  files are rare in daily editing. Either way, it is a decision, not a worker fix.

**MF-B. An existing destination the user cannot read is replaced with the new-file mode, which can widen it.**
- Input (probe `noread`): an existing file of mode 0060 that we own and cannot open for reading. Save over it with its
  observed expectation and no FORCE.
- Wrong result: rc=0, VERIFIED, and the file is now **0644** (world-readable). The ruling says "an existing destination
  keeps its mode".
- Cause: file.c:484–487. "An unopenable destination keeps the new-file mode", and the pre-rename recheck does not
  catch it, because the stat is unchanged. With FORCE the recheck is skipped as well.
- Fix: for a PRESENT expectation whose metadata could not be copied, apply `expect.stat.perm` (the observed mode)
  through `os_file_set_perm`, never `0666 & ~mask`.
- Incidence is low (owner-unreadable files, ACL-denied reads), but this is a silent permission widening.

**MF-C. The rebaseline pair absorbs a foreign rewrite inside its window: wrong text through undo.**
- Input (probe `absorb`, made deterministic with the RENAMED step hook):
  1. a.txt and b.txt are hard links to one inode. Map a.txt, edit it, and save asynchronously to a.txt.
  2. Between the rename and the after-fstat, write b.txt in place at the same size and restore its mtime.
- Wrong result:
  - pub = VERIFIED;
  - `file_backing_validate` returns true with a non-null ctx and with a null ctx;
  - the mapped text reads "ZZZZZZZZZZ" where "AAAAAAAAAA" was acquired.
- The pair shows exactly "ctime moved, nlink fell", which is round 2's own §3 counter-example squeezed into the rename
  window. Undo by reference would then restore foreign bytes as original.
- The window is one rename syscall. The writer must also restore mtime, which is a deliberate act (`touch -r`,
  `cp -p` or `rsync -t` over a hard link). It is N1-class: inherent to the ruling, not to the code.
- I file it as MUST-FIX for an advisor decision. The choice is to accept and record the window beside N1, or to cover
  it with MF-A's hash. X1–X3 also show that no test exercises the rebaseline's refusal branch.

**MF-D. `file_temp_verify` and `file_temp_recover` have no test anywhere** (tests, fuzz, law2 and bench). This is the
new surface of erratum 4: the finish and crash-recovery path.
- X6 (no content CRC), X7 (no stable re-stat) and X13 (no path recheck) all go uncaught.
- My probe `recover` shows the happy paths work:
  - verify returns the right prefix CRC32C;
  - recover of a real installed file reaches INSTALLED_UNSYNCED, and retry then gives VERIFIED;
  - a FIFO gives NOTREG with no block.
- But nothing pins them. Law 1 and Law 9 require a red-first test.

## New findings: NOTEs

- **N-A. Publish backpressure is a busy spin. It is CPU, not a hang.** Probe `spin`: one bulk worker with its mailbox
  held full. With the UI asleep for 1.000 s, the process used 1.000 s of CPU. After a drain, the open completed. Cancel
  ends it (`work_should_stop`), so close is not blocked. It needs a wake-on-drain primitive in work, as round 2
  reports.
- **N-B. A rename that failed without renaming is misclassified, and its temp is never removed.** Probe `unr`, with
  rename wrapped to return EIO or EISDIR and do nothing:
  - publish gives RENAME_UNCERTAIN and HELD;
  - `file_save_retry_barrier` gives **CONFLICT**, although the destination is unchanged (`file_check=0`);
  - after abandon and close, the hidden temp is still on disk.
  
  Two fixes would help:
  - Definitive errnos (EISDIR, ENOTDIR, EACCES, EPERM, EBUSY, EINVAL, EXDEV, EROFS, ENOSPC, EDQUOT) mean "not
    performed" on a local filesystem, so they can be PRE_RENAME.
  - From HELD, an observation that the temp name still names the held fd and the destination still matches the
    expectation proves the save unpublished. Erratum 4 then permits the unlink ("identified, still-owned, definitely
    unpublished").
  
  Without the second fix, savectl reconciles into CONFLICT_CANCEL and shows a false external-change prompt.
- **N-C. A source CONFLICT from the rebaseline skips the directory barrier, and a retry turns it into VERIFIED.** Probe
  `absorb` with mtime moved: pub CONFLICT, HELD, no `os_dir_sync`. Then `file_save_retry_barrier` gives VERIFIED, and
  the baseline is adopted. The validator stays false, which is the safe direction. The outcome conflates "the
  destination is foreign" with "the displaced source drifted". Savectl's CONFLICT_CANCEL is defined for the former.
  One fix: run the barrier and verification, and report source drift as a separate bit.
- **N-D. A relative destination naming the file's own path skips the VERIFIED adoption.** Probe `rel`: status 0,
  VERIFIED, HELD, then `file_check=1` with reasons 0x27, so our own save reads as an external change. The adoption is
  `strcmp(save_dest, f->path)` (file.c:1402). Erratum 3 makes targets canonical, so savectl is unaffected. The file
  object should refuse a non-canonical `dest_path`, or adopt by identity (the installed fd identity is what `f->path`
  names).
- **N-E. `os_creation_mask` relies on an unchecked assumption.**
  - In this binary no thread can exist before main. The only `pthread_create` is in `os_thread`. Static xcb,
    xkbcommon and wayland-client spawn none at init. TSan starts its background thread lazily, on the first
    `pthread_create`. GL drivers are dlopened after main, and umask is never touched again.
  - If a future static library's constructor did start a thread that creates files, the set-and-restore window would
    create them under mask 0 (mode 0666). No constructor reading `os_creation_mask` may run before capture either, or
    it reads 0. No such constructor exists today.
  - Reading `Umask:` from `/proc/self/status` (Linux ≥ 4.7) has no window at all, and set-and-restore could remain
    the fallback. The ruling chose the constructor, so this note is for the record.
- **N-F. `file_temp_recover` reports a symlink at the destination as FILE_ERR_IO (OS_E_LOOP)**, not as a mismatch.
  Savectl treats an I/O failure as "retry later, preserve the obligation", so a foreign symlink never resolves to
  changed protection. CHANGED or NOTREG would match erratum 4's mismatch class.
- **N-G. Test gaps on round 2's own lines** (no trial-use impact on their own; one backlog item):
  - X4 and X5: N8's slot, epoch and generation checks are untested.
  - X8: the identity check in `temp_unlink_own`, which is erratum 4's unlink authority.
  - X9: the HELD retry's installation proof.
  - X14: adoption only for `file_path(f)`.
  - X15: the reload slot in close readiness.
  - X16: N2's "fd_stat used", which the decision file cites as the answer.
  - X18: the null-ctx validator's refusal of a backing file did not create.
- **N-H. No accessor for async custody.** Savectl's VERIFYING_FD must run `file_temp_verify` off path on the held fd,
  but the async object keeps custody in `f->save_temp`. A worker reading it while the UI abandons would read a closed
  fd. Either savectl drives the `file_temp_*` core itself, or file.h needs a "take custody" call (struct copy plus
  forget, phase to IDLE).

## Behaviour changes, against the signed savectl and L2-3 contracts

- **Mapped Keep refuses after an outside rename-over.** It is consistent with savectl: "If mapped ORIGINAL is stale,
  report SOURCE", and "Failed Keep leaves the prompt and text intact". The consequence reaches Save and Save As too
  (MF-A), which no signed text anticipates for intact bytes. Escalate.
- **Async saves end in HELD.** Consistent: WRITING → VERIFYING_FD → FINISHING keeps the post-rename fd, and fresh saves
  are BUSY until custody is released. Caveat N-H.
- **`file_temp_prepare` and `file_save_write` take `file_source *`.** Consistent with erratum 3 (the nullable
  file_source; "private text can use the temp core with no opened source") and it closes round 1's N7. The L2-3 brief
  names no conflicting signature.
- **Relative destinations are still accepted.** No signed contract is broken, because erratum 3 makes `file_target`
  canonical and savectl passes canonical names. Inside file, a relative self-save loses its VERIFIED adoption (N-D).
  Refuse it, or adopt by identity.

## Lane results (final, all mutations restored)

Stamp: BAT0 = Not charging, load1 0.11 at the check start and 0.77 at the tsan start (M), 2026-11-25 04:56–04:59 CET.
`cmp` against the saved original passed before the runs. Logs are `build/review-probe/r2-lane-*.log`.
- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:190-:195`: exit 0, `check: ok`.
  - check-layers: 6 isolated layer links;
  - `file_test: ok (41 tests)`, `os_test: ok`, `law2_file_test: ok`;
  - contract: PASS 64.
- `nice make -j2 check-tsan`: exit 0. 13 threaded suites passed, `file_test: ok (41 tests)`.
- `nice make -j2 fuzz-smoke`: exit 0. 16 fuzzers replayed their seeds, `file_fuzz` included.

Every lane is green. The findings above are what these lanes do not test.

## Outside scope (report only)

- work: a wake-on-drain or a backpressure primitive, so a full mailbox does not burn a bulk worker (N-A).
- The chown limitation (bead v2-jh7) and publish-by-exchange (N1) are unchanged, as ruled.
- `tests/os_test.c` `case_modes` calls `umask(2)` at runtime. That is fine in a test, but `tools/system-symbols.txt`
  grants umask to os only, and the lint may not cover tests.
