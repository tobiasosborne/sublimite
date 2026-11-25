# Review: L2-1 file (Muse), adversarial pass (Opus)

Change reviewed: `git diff 907cbef review/l2-1b`, applied to a v2 worktree at 907cbef. Scope: `src/file/file.{c,h}`,
`src/file/file_test.h`, `tests/file_test.c`, `tests/law2_file_test.c`, `fuzz/file_fuzz.c`, `bench/file_bench.c`,
`docs/decisions/L2-1-file.md`. Read against `docs/briefs/L2-1-file.md` (GAPs 1–3, the temp-name rule), the savectl
seam table, `docs/briefs/L2-3-journal-save.md`, A22, A23 rules 1–7, A28, and CLAUDE.md (A15, Law 2, no UI-thread
sleeps or spins).

Method. I read the code, then confirmed each MUST-FIX below with a probe program under `build/review-probe/` that links
the san archives (plus `file_test_hooks.o` and `--wrap` where needed). Then I ran 23 mutations of `src/file/file.c`
against the san file suite. Every mutation was reverted, and `cmp` against the saved original passed before the final
lanes. Measurement stamp: BAT0 = Not charging, load1 = 0.08 (M), 2026-11-25 03:17 CET.

## MUST-FIX

**MF1. The borrowed prefix dangles after a failed open: SEGV on wrong text, and a data race.**
`install_acquired(prefix_only)` makes `f->prefix` borrow `open_result.prefix` (file.c:1160). The job's failure path
(file.c:967–979) then frees that arena (`base_arena_free`, which is `os_vm_release`, i.e. munmap) and rewrites the whole
record (`*r = keep`) while the UI may be reading it. `prefix_ready` stays true after OPEN_FAILED, so
`file_prefix()` returns a pointer into unmapped memory.
- Input: a mapped open (size ≥ threshold) whose registration fails with NOSPC after the UI has decoded PREFIX_READY.
  Any later failure does the same: a premature EOF in the copy read, a changed post-validation, or a cancel.
- Probe `uaf`: after OPEN_FAILED(status 10), `prefix_ready=1`, `len=29`, and reading the prefix gives an ASan SEGV on
  the unmapped arena page.
- In the app, the first viewport is drawn from the prefix while the open is still pending. That draw races the
  worker's munmap: it crashes or shows wrong text.
- This is also the coordinating-boolean failure CLAUDE.md forbids. `prefix_ready`/`open_ready`/`attached` together
  form v1's `_ready/_installed` triple, which the brief §1.5(e) asked to replace.

**MF2. A RENAME_UNCERTAIN result strands custody: the descriptors and the temp file leak for good.**
After `FILE_PUB_RENAME_UNCERTAIN`, publish keeps the temp SYNCED (file.c:629–633), and decode sets the phase to IDLE
(file.c:1272). The custody can then no longer be reached:
- `file_save_abandon` is a no-op in IDLE.
- No reconcile entry point exists. The seam table says "Reconcile uncertain rename by observation", and Q9 says
  "preserve recovery evidence".
- The next `file_save_begin` memsets `save_temp` (file.c:1639) over live descriptors.
- Probe `unc` (rename wrapped to return EIO once): status 1, pub 2, phase IDLE, +2 fds and 1 temp on disk. After
  abandon, the next save and `file_close_finish`, the temp is still on disk and at least one fd is leaked.
- No test covers this class (mutation M18 survives).

**MF3. Close can hang forever: a cancelled open or reload result keeps its slot in RESULT_READY.**
`slot_finished` requires `state == IDLE` (file.c:1313), and only decode returns the open and reload slots to IDLE.
`file_close_begin` cancels any slot that is not IDLE (file.c:1322–1325). Once the job has stored RESULT_READY but its
final publish is dropped, nothing can ever move the slot back:
- the drop happens on a full mailbox (`publish_or_continue` returns once `work_should_stop` is true);
- or on a cancel landing between `job_ready` and `work_publish`.
- Then `file_close_ready` is false forever, and `file_close_finish` would ASSERT.
- Probe `hang`: fill the worker mailbox, open, wait for RESULT_READY, close_begin, drain for 3 s. close_ready never
  becomes true (slot state 2).
- `save_reap` handles this case for the save slot only.

**MF4. Abandoning or closing during PUBLISHING loses the classified outcome; our own save then reads as external.**
The cancelled publish job's SAVE_DONE is dropped as stale. `save_reap` (file.c:1127–1146) then sets the phase to IDLE
whatever `save_outcome.pub` says:
- On VERIFIED, the baseline is not adopted.
- On RENAME_UNCERTAIN, the reaper's `file_temp_discard` closes the fd and unlinks the temp, which is the evidence
  savectl must keep.
- Probe `abpub` (block at DIR_SYNCED, abandon, release): phase IDLE, no message, outcome VERIFIED, and `file_check`
  returns true with reasons `0x27`. Our own verified rename is reported as an external change.
- The header says completion is "reported through file_save_busy going false with phase back to IDLE and no message".
  That is exactly how the classified evidence gets lost. The seam requires that effects already exposed are settled.

**MF5. The undo validator refuses the intact mapped backing after every verified save of that file.**
The rename drops the old inode's last link, which moves its ctime. `file_source_validate` compares the full stat,
ctime included (file.c:290 through `file_expectation_diff`). Decode of VERIFIED never adopts the retired ctime. v1 had
`file_source_adopt_retired`; the decision doc §3 keeps that rule only inside Keep.
- Probe `val`: a mapped open, an edit and an async save give VERIFIED. Then `file_backing_validate` returns 0 while the
  mapped bytes are intact.
- Every undo by reference on a large (mapped) document after its first save will fail as a CONFLICT.
- No test covers it: `t_open_mapped` validates before any save.

**MF6. Keep accepts foreign bytes in a mapped inode, and `file_check` misses later in-place writes (A23 rule 7).**
- `file_resolve_keep` adopts any ctime move once the path no longer names our inode (file.c:1533–1537). It reads that
  move as "the dropped link". The move cannot tell a dropped link apart from an in-place write with mtime restored,
  made through a second hard link or a held fd.
- After Keep, the fd leg of `file_check` compares identity, size and mtime only (file.c:1482–1484), never ctime.
- Probe `keep`: map a.txt, `ln a b`, rename a replacement over a, write b in place at the same size, then restore its
  mtime.
  - `resolve_keep` returns FILE_OK, the validator returns true, and the mapped text reads "ZZZZZZZZZZ" where
    "AAAAAAAAAA" was acquired.
  - A second same-size write with mtime restored then gives `file_check=0` (reasons 0).
- The watch filters on our leaf name (file.c:1598), so writes through the other link raise no event either.
- A sound rule: on a mapped backing, any ctime move that is not explained by a link count that went down (with size
  and mtime unchanged) refuses Keep, or forces a reload. Add ctime to the fd leg of `file_check`.

**MF7. A NEW destination gets mode 0600, not 0666 & ~umask (advisor ruling, merge item).**
For an ABSENT expectation, no fchmod is applied: the temp's private 0600 (`os_posix.c:261`) becomes the final mode.
- Probe `mode`: with umask 022, the saved new file is 0600; the ruling gives 0644.
- PRESENT destinations are fine: `os_file_copy_metadata` copies the mode, and the probe's temp beside a 0644 file was
  0644.
- `os` has no seam to apply the umask default; the fix needs an `os` call.
- Decision doc §7 and §8 are stale. They say permission bits are not preserved and that no fchmod seam exists, which
  1.28 contradicts.

**MF8. Input can reach an ASSERT in the save path, aborting the process (A15).**
`temp_prepare_core` checks only the total length and that a slash exists, then calls `split_path`, which ASSERTs.
- Probe `slash`: a destination ending in `/` gives `file.c:35: ASSERT failed`.
- Probe `long`: a leaf of 300 bytes gives `file.c:37: ASSERT failed`.
- Both can come from a Save As name. On the async path the ASSERT fires on the worker and takes the editor down.
- These must be return codes (OS_E_NAMETOOLONG / FILE_ERR_IO); the same applies to a relative path.

**MF9. A FIFO (or another special file) at a PRESENT destination blocks the bulk worker forever.**
The metadata copy opens the destination with `OS_OPEN_READ` and no `OS_OPEN_NONBLOCK` (file.c:489). It does so before
any kind check, and the expectation's kind is never checked.
- Probe `fifo`: `file_save_write` onto a FIFO with its observed expectation is still blocked when SIGALRM arrives at
  3 s.
- With the configured single bulk worker, every open, save and journal job stalls, and close never becomes ready.
- The acquisition path already uses NONBLOCK plus a kind check; the save path needs the same.

**MF10. Acceptance lines that no test checks (brief: "a test per result class and per cleanup path").**
- *EXIST → CHANGED is never exercised.* `t_save_absent_race` creates the foreign file before publish, so the pre-rename
  recheck refuses first and the NOREPLACE branch never runs. Mutations M12 (plain REPLACE) and M4 (leak on EXIST)
  both survive. The foreign file must appear between the recheck and the rename (wrap `os_rename_at` or
  `os_stat_at`).
- *No RENAME_UNCERTAIN test* (M18).
- *No async barrier-retry test*: `file_save_retry_barrier` has no test, and M11b, which re-runs the whole publish on a
  retry, survives.
- *No leak assertions on cleanup paths.* M3 (fsync failure), M5 (pre-rename CHANGED), M7 (reap of an abandoned
  prepared temp), M8 (`close_finish`) and M9 (cancel/write failure in prepare) all survive. No test counts descriptors.
- *The fault-spec integration row is not exercised.* `t_save_async_cancel_blocked` and `t_close_while_saving` open with
  the default threshold, so they run in copy mode with no mapping and no registration. No test checks that "the
  release child sees zero unmaps/frees of that backing".
- *The closing fault poll in `file_check` is untested* (M15).
- *VERIFIED baseline adoption is untested.* M20 survives: `!file_changed` is vacuous without a check.
- *Retained-source validation in prepare is untested* (M21).

## NOTES

- **N1. Recheck-then-REPLACE window.** For a PRESENT expectation, a foreign rename between the pre-rename recheck and
  `rename(2)` is clobbered, and verification reports VERIFIED. This cannot be closed with plain rename. RENAME_EXCHANGE
  could close it: exchange, check the swapped-out inode against the expectation, and swap back on a mismatch. At
  least state it in the decision doc beside the NOTSUP rule.
- **N2. `file_temp.fd_stat` is dead.** It is set at file.c:527 and never read. `temp_verify` compares only the path's
  identity and size with the current fstat. A foreign in-place write into our installed inode between rename and
  verify is VERIFIED, and the baseline absorbs it. Compare fd_now's size and mtime with `fd_stat` (the rename changes
  ctime only).
- **N3. `file_check` ignores a failed fd stat** (file.c:1481). That contradicts its own rule that "a failed observation
  is a change".
- **N4. FSM rule.** Coordinating booleans across lifecycles: `prefix_ready`, `open_ready`, `attached`,
  `reload_pending`, `save_abandoned` and `watching`. `save_op` is an `int` handled with `default: ASSERT`
  (file.c:1104). `tree_from_backing` has a `default:` on `file_mode` (file.c:1396).
- **N5. Blocking syscalls on the UI thread** (no sleeps or spins: grep finds no `os_sleep_ns`, and the one `for (;;)`
  in `file_watch_poll` ends when the queue drains):
  - unlinkat and close in `file_save_abandon(PREPARED)`;
  - the same in `save_reap`, which runs from the const queries `file_save_busy` and `file_save_phase_of` through a
    cast;
  - munmap of a mapping that may be large in `file_reload_tree` and `file_close_finish`;
  - the two stats in `file_check`.
  These are syscalls, not waits. Reaping I/O from a query is surprising API behaviour.
- **N6. Metadata copy is skipped silently** when the destination cannot be opened for reading or no longer matches the
  expectation. A FORCE save then ships mode 0600 and no xattrs, with no outcome bit saying so.
- **N7. The synchronous core never validates the mapped source.** `file_temp_prepare` and `file_save_write` pass no
  `source`, so only the closing poll runs. A same-size in-place overwrite of the mapped source during a synchronous
  save is written out as our text. Only the bench and tests call it today; the header should say so, or the call
  should take a source.
- **N8. Decode trusts `w.f`.** `file_msg_decode` dereferences `w.f` from the payload and never compares the
  generation. A message still in the mailbox after `file_close_finish` reads freed or reused caller storage. The
  dispatcher contract should be written down with the generation rule, or decode should check `f->generation`.
- **N9. Nits:**
  - the empty `if` in `install_acquired` (file.c:1152);
  - unused `state` and `generation` parameters;
  - `file_crc32c` rebuilds its table on every call;
  - the garbled `file_save_publish` comment in file.h;
  - the temp-name `room` reserves no NUL for two 20-digit counters (unreachable: the stamp has at most 19 digits).

## The two questions

- **Selective receive.** No. `file.c` calls only `work_submit`, `work_cancel`, `work_handle_finished`, `work_publish`,
  `work_should_stop` and `work_continue`. It never calls `work_mailbox_receive` or `work_mailbox_drain`.
  `file_msg_decode` is a callback for the one UI dispatcher, which is consistent with Q7. Caveat: N8.
- **Permissions.** No: see MF7. For a NEW destination no fchmod follows the 0600 creation, so the final mode is 0600
  (measured with umask 022). For an existing destination, `os_file_copy_metadata` applies the predecessor's mode
  before the sync, which is correct.

## Mutation table (san `build/san/tests/file_test`; each one reverted)

| # | Mutation | Result |
|---|---|---|
| M1 | skip `os_fault_register` before publication | KILLED: `t_open_mapped` (piece.c:1052 ASSERT on an unregistered backing) |
| M2 | VERIFIED without the identity check | KILLED: `t_save_foreign_after_rename` file_test.c:655 |
| M3 | leak the temp on a temp-fsync failure | **SURVIVED** |
| M4 | leak the temp on NOREPLACE EXIST | **SURVIVED** (the branch is never reached) |
| M5 | leak the temp on a pre-rename CHANGED | **SURVIVED** |
| M6 | leak the temp on discard of a SYNCED temp | KILLED: `t_temp_custody_discard` file_test.c:686 |
| M7 | leak the temp when reaping an abandoned prepared job | **SURVIVED** |
| M8 | leak temp custody at `file_close_finish` | **SURVIVED** |
| M9 | leak the temp on cancel, write failure or changed source in prepare | **SURVIVED** |
| M10 | skip the directory sync | KILLED: `t_save_dirsync_retry` file_test.c:613 |
| M11a | sync barrier retry calls rename again (error ignored) | **SURVIVED** (no rename counter; harmless only because the temp name is gone) |
| M11b | async barrier retry re-runs the whole publish (FORCE) | **SURVIVED** (no async retry test) |
| M12 | absent target renamed with plain REPLACE (EXIST never possible) | **SURVIVED** |
| M13 | drop the high inode word from identity | KILLED: `t_expectation` file_test.c:233 |
| M14 | skip retire before unmap at final release | KILLED: `t_open_mapped` file_test.c:295 |
| M15 | `file_check` drops the closing fault poll | **SURVIVED** |
| M16 | validator: fault poll only, no fd identity | KILLED: `t_change_in_place` file_test.c:359 |
| M17 | Keep ignores a ctime move while the path is ours | KILLED: `t_change_in_place` file_test.c:361 |
| M18 | every rename error classed PRE_RENAME and the temp unlinked | **SURVIVED** |
| M19 | post-rename mismatch reported as a plain failure | KILLED: `t_save_foreign_after_rename` file_test.c:655 |
| M20 | a verified async save does not adopt the installed baseline | **SURVIVED** |
| M21 | prepare skips the retained-source validation | **SURVIVED** |
| M22 | metadata copy skipped | KILLED: `t_save_metadata_xattr` file_test.c:705 |
| M23 | reload NOSPC falls back silently to a copy | KILLED: `t_reload_nospc` file_test.c:452 |

11 killed and 12 survived (P, a count of the rows). Every survivor is listed under MF10 or MF2.

## Lane results (final, all mutations restored)

Stamp: BAT0 = Not charging, load1 = 0.08 (M), 2026-11-25, 03:25–03:29 CET.
- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:190-:195`: exit 0, `check: ok`.
  - lints and layers: 6 isolated layer links;
  - check-san: 33 binaries, `file_test: ok`;
  - check-law2: 9 release suites, `law2_file_test: ok`;
  - contract: PASS 64.
- `nice make -j2 check-tsan`: exit 0. 13 threaded suites passed, `file_test: ok`.
- `nice make -j2 fuzz-smoke`: exit 0. 16 fuzzers replayed their seeds, `file_fuzz` included.

Every lane is green on the change as submitted. The MUST-FIX items above are what those lanes do not test.

## Outside scope (report only)

- `os_file_copy_metadata` refuses on fchown EPERM. Saving a group-writable file owned by another user therefore fails
  every time (v1 behaves the same: v1 `file_posix.c:291`). That is an `os` contract question, not one for L2-1.
- `os` has no seam for the umask-default mode a new file needs (MF7).
