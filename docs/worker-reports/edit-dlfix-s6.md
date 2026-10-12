# edit-dlfix-s6: dataloss_test stranded_orphan(false, true) regression

Worker: Opus 5.5, 2026-10-13. Fix worktree `.wt/edit-dlfix-s6` (branch
`wt/edit-dlfix-s6`, base main bf196df = e171529 + worklog commit). Not committed.
Power: `/sys/class/power_supply/BAT0/status` = "Not charging" (AC attached) at
every measurement; load1 0.5 to 2.9 (M) while other workers were active.

## Bisect

Scratch worktree `.wt/edit-bisect-dl` (removed afterwards). Each step built
`make -j12 build/san/tests/dataloss_test` and ran
`dataloss_test --stranded-orphan` headless (DISPLAY/WAYLAND_DISPLAY unset,
private XDG_RUNTIME_DIR). That selector runs only the three stranded_orphan
cases, so the unrelated two_process_save flake (below) cannot mislead the bisect.

```
git bisect start 'e171529' '2f023ce'
# good: 6d4c19e edit-zw9
# bad:  150d768 edit-11v
# bad:  2a89e76 edit-97i
# good: 6daffa7 edit-g02
# first bad commit: [2a89e76d205b668a2f2d074bc9596191e8879f50] edit-97i
```

## Mechanism

edit-97i made `editor_add_buffer` reuse any live tab whose path resolves to the
same target (`path_tab`/`same_path`): it selects that tab and returns its ID.
Journal recovery (`src/editor/recovery.c` `install_buffer`) also calls
`editor_add_buffer` for every recovered buffer. Then it overwrites the active
buffer's piece tree with the journal's bytes and marks it modified.

In stranded_orphan(false, true), both instances edited the same file `a`
("xbase" from the crashed one, "ybase" from the survivor). On the next launch
the first recovered session installs a tab for `a`. For the second session's
buffer, `editor_add_buffer` returns the existing tab instead of a new one.
`install_buffer` then replaces that tab's text with the second journal's bytes.
The result is one tab where two are expected, and one instance's unsaved edits
are dropped. Recovery then deletes both source journals. Launch 0 printed
`tabs=1 seen_crashed=1 seen_survivor=0`.
That is real data loss for two instances with unsaved edits to one file, not
just a test artefact.

## Fix (minimal)

- `src/editor/buffers.c`: the body becomes `static add_buffer(..., bool reuse)`.
  `editor_add_buffer` calls it with `reuse = true`, so 97i's behaviour for
  CLI, Ctrl+O and commands is unchanged. The new
  `editor_add_recovered_buffer(e, path, id)` calls it with `reuse = false`.
- `src/editor/private.h`: declares `editor_add_recovered_buffer`. It is internal
  to the editor module.
- `src/editor/recovery.c` `install_buffer`: calls `editor_add_recovered_buffer`.

Decision: a recovered journal is an independent edit history, not an open
request, so path identity must not merge it into another tab. 97i's dedup still
applies to everything opened after recovery. edit-rzj's contract still holds:
every orphan surfaces as its own modified tab.

## Red / green

Red (fix worktree before the edit, default mode):
```
dataloss_test:601: FAIL seen_b
dataloss_test:635: FAIL stranded_orphan(false, true) == 0
```
That is 2 of 3 runs (M). The third run died earlier in two_process_save with
`editor_open returned -103`; see the separate finding below.

Green (after the fix):
- stranded_orphan: 0 failures in 3 + 5 default runs, 1 + 5 --probe runs and
  1 `--stranded-orphan` run (M). Every launch printed
  `tabs=2 seen_crashed=1 seen_survivor=1`, except the capacity-limited launch 0,
  which printed `tabs=1`, matching rzj's deferral contract.
- dataloss_test default: exit 0 in 6 of 8 runs. --probe: exit 0 in 5 of 6 runs.
  Every non-zero exit was the two_process_save flake below.
- editor_test, editor_session_test and cli_test (sanitizer build) exit 0 under
  `tools/with_display_lock.sh` with DISPLAY=:99, EDIT_DISPLAY=:99 and a private
  XDG_RUNTIME_DIR.
- `make -j12 all`: exit 0.

## Separate finding (not fixed; predates the bisect range)

Symptom: `two_process_save` fails intermittently. `editor_open returned -103`
(EDITOR_ERR_IO) appears at dataloss_test:501 (child) or :519 (parent).

Rate:
- 6daffa7 (pre-97i): 3 of 5 runs (M).
- Fixed tree: 2 of 8 default runs and 1 of 6 probe runs (M).

Mechanism, captured with strace (ASAN_OPTIONS=detect_leaks=0):
1. The test pre-creates both empty session files with mkstemp, then starts two
   editors concurrently.
2. The parent's startup housekeeping, `journal_retire_directory`
   (`src/journal/retire.c`, from edit-rfq 5145c0c, called from
   `editor_session_restore`), took `.session-<child>.lock` before the child had
   locked it (parent flock at 24.714799, child at 24.724551).
3. The parent then retired the child's empty placeholder: it renamed it to
   `.retired` and unlinked it.
4. The child's journal no longer exists, so its open fails with
   EDITOR_ERR_IO.

`src/main.c` has the same window: it calls mkstemp on the session, and
`editor_open` takes the lease later. Two instances starting at the same moment
in one journal directory can therefore make one of them fail to start. No
edits are lost, because the placeholder was empty.

Candidate fix (not implemented): create the session file and take its lease
while holding the directory flock that retire already uses as its guard, or
take the lease before the file becomes discoverable.

The lander's earlier green runs and the coordinator's 3 runs that showed only
the stranded failure are consistent with the timing window being load
dependent.
