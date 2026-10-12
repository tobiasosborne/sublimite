# edit-dataloss-s6: adversarial data-loss campaign (worker report)

Worktree `.wt/edit-dataloss-s6` (branch `wt/edit-dataloss-s6` from main 35efb7b).
New suite `tests/dataloss_test.c` (auto-globbed by the Makefile; no Makefile edit).
Sanitizer build `build/san/tests/dataloss_test`, clang 18 ASan/UBSan, headless
null backend, every scenario forks a child editor. Box: battery "Not charging"
(AC), load1 10.7-22.4 (M) during the runs (Codex workers building). Runs took
3.7 s (M) each; no timing failure was seen, nothing was rerun for timing.

Scope was cut mid-bead by the coordinator (Fable quota) to scenarios 2, 3, 6, 9;
all ten were already written by then, so all ten ran once in one `--probe` run.

## Verdicts

Default mode (`make check` path) is green: `dataloss_test: ... passed`, exit 0.
`--probe` runs three findings-gated probes independently and reports each.

| # | Scenario | Test | Result |
|---|---|---|---|
| 1 | Key applied + journalled, SIGKILL before any pump/fsync (file-backed buffer) | `kill_after_append` | pass: recovered "basex", dirty, caret 5 |
| 2 | SIGKILL from the save worker at each of the 6 `FILE_STEP_*` phases | `killed_mid_save(phase)` x6 | pass: target is exactly "base" or "xbase" at every phase; restart shows "xbase" dirty; Ctrl+S lands "xbase" |
| 3/7 | Base truncated shorter than the journal offsets, mtime restored (size/ctime/CRC differ) | `changed_base(BASE_SHORTER)` | pass: launch rc 0, notice shown, tab 0 shows "ba" clean, second buffer "xkeep" dirty, "MK" in the set-aside journal |
| 3 | Base deleted under a set-aside journal | `changed_base(BASE_DELETED)` (probe) | pass (cleared): same as above, second buffer restored live |
| 4 | R / K chosen for a stale tab while another tab's save is held at TEMP_WRITTEN | `reload_during_save(true/false)` | pass: choice waits for the save; A saved "xbase"; R shows "external" clean, K keeps "ykeep" dirty and saves it |
| 5 | Ctrl+S while the R/K prompt shows | `save_during_prompt(true/false)` | pass: Ctrl+S swallowed, disk and buffer untouched; then R reloads / K + Ctrl+S writes "xbase" |
| 6 | Hot exit: two dirty file tabs, one dirty untitled, one dirty closed tab; SIGKILL | `hot_exit_mixed` | pass: bytes, dirty flags, carets (1/5/4), active tab, before and after first frame; both file tabs save; Ctrl+Shift+T reopens "wgone" dirty |
| 8 | SIGKILL between journal slices of an undo group's INSERT | `torn_undo_group` (probe) | **FAIL, finding F1** |
| 9 | Clean buffer's source rewritten, key typed in the same frame (0 and 1 turns between) | `reload_vs_keystroke(0/1)` | pass: key landed on the old text ("xbase"), dirty, prompt shown; never dropped |
| 10a | Two processes edit and save the same file | `two_process_save` | pass: A wins ("Abase"); B's save fails (err 5), B stays dirty with "Bbase" |
| 10b | Two instances share the journal dir; one crashes dirty, the other closes cleanly later | `stranded_orphan` (probe) | **FAIL, finding F2** |

## Findings, ranked by severity

### F1 (data loss, wrong text): a crash while an undo/redo group is being journalled replays a prefix of the group

Observed (`--probe`, two runs, identical): `torn_undo_group: killed at insert
progress 16384/262144, editor showed 262144 bytes, recovered 16384 bytes`.

Repro: untitled buffer of 256 KiB (G: 16 slices of `JOURNAL_INSERT_SLICE_BYTES`
= 16384, so the drain spans several turns), Ctrl+A, type `x` (buffer = 1 byte),
Ctrl+Z (buffer = 256 KiB again), SIGKILL while `e->ops[0]` (the staged tree
INSERT) has `0 < progress < len`. Restart: 16384 bytes. Expected: 1 byte
(pre-undo) or 262144 (post-undo).

Mechanism (src/editor/input.c `replay_slice` + `editor_journal_staged`): an
undo/redo is journalled as `DELETE(off, old)` followed by a tree INSERT drained
one 16 KiB record per `editor_journal_staged` call (`JOURNAL_BUSY` after each
slice); each record is page-cache committed on its own. The journal has no
group boundary, so replay applies the DELETE and whatever INSERT slices made it.
After restart the undo history is empty (`install_buffer` clears it), so the
remainder is unrecoverable from the UI. The same shape applies to any
multi-record action (replace selection = delete + insert; large paste via
`journal_insert_step`), with a window of microseconds for small edits and of
several UI turns for large ones. Trial relevance: Ctrl+Z after a large
paste/replace-all, then a crash or power loss inside that window.

### F2 (data loss, silent): an orphan journal older than the newest session artifact is never restored

Observed (`--probe`): `stranded_orphan launch 0/1/2: tabs=1 seen_crashed=0
seen_survivor=1`, leftover `session-3UDXl8 546 bytes` (the crashed instance's
journal, holding "xbase") after three launches.

Repro: two instances with the same journal directory (main.c's layout:
`journal_default_dir()/session-XXXXXX`). Instance 1 opens `a`, types `x`,
exits without close (crash). Instance 2 (already running) opens `b`, types `y`,
then closes cleanly, 20 ms later (its CURRENT checkpoint therefore has the
newer mtime). Every later launch (`editor_session_restore`, src/editor/recovery.c)
picks the single newest unlocked `session-*`, restores it, unlinks it and
writes its own newer checkpoint, so instance 1's journal is never the newest
and its edits never surface; no notice is shown. Expected: the crashed
instance's dirty "xbase" tab to come back within the three launches. Trial
relevance: two windows (`--new-instance`), one crashes, the other is closed
normally; or any crash while a second instance stays open.

### Cleared probes and notes (not findings)

* Deleted base under a set-aside journal (`changed_base(BASE_DELETED)`): no -103,
  notice shown, other buffer restored live, typed bytes in the set-aside file.
* `editor_get_stats().pending` does not include a queued R/K source choice
  (`e->source_choice` is only in `runnable()`), so a harness that waits on
  `pending` after pressing R returns one turn early. The suite's `choose()`
  spins on `e->source_choice`. Not a loss; `editor_run` does not sleep on it.
* Two-process save: the losing process gets `save_error` 5 and stays dirty;
  not silent.

## Quota readings (`~/.claude/scripts/claude-usage`)

1. start: 5-hour 14 %, 7-day weekly 80 %, 7-day Fable 70 %
2. ~20 calls: 17 % / 80 % / 71 %
3. before build: 18 % / 81 % / 72 %
4. after final run: 20 % / 81 % / 73 %

## Not done

* Scenarios beyond the brief's list that the code suggests but were not
  written (budget cut): crash during `journal_rotate` checkpoint rename at the
  editor level; hot exit with the journal FULL; parked keys for a large-file
  open dropped by a reload (`queued_owner_live` generation check); merged
  recovery of several orphans (F2 is the minimal case).
* No display (Xvfb) run; nothing here needed a window.
* No fixes were attempted (bead scope). F1 and F2 are left as `--probe` probes
  so `make check` stays green; each should become its own bead (both would
  affect trial use).
