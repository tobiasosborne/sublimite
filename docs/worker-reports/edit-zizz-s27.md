# edit-zizz worker report (session 27, Opus worker)

Bead: an over-capacity session journal was deferred on every launch, so its
unsaved untitled buffers could not be reached. Policy (Tobias 2026-10-29):
restore what matters first. Decision record: `docs/decisions/edit-zizz.md`.

## Restore policy as implemented

The new path runs only when `session_fits` is false for a discovered source.
Sessions that fit, and explicitly named journals, behave as before.

1. Every unsaved buffer is installed: as a live tab while live room remains,
   then as a closed tab. Unsaved buffers in the tab list go first, in tab
   order. Unsaved buffers that were closed in the source follow.
2. Unmodified tabs fill the remaining live room in recorded tab order.
3. Everything else is dropped. Its bytes are on disk. This includes
   unmodified buffers that were closed in the source.

Capacity rules are the same as `session_fits`:
- A closed buffer needs one temporary live slot, so live picks stop at
  `room - 1` whenever anything is closed.
- Closed picks never exceed the free closed pool, so no closed tab is evicted.
- Installed buffers never exceed the free buffer slots.
- The edit-dzk1 unpainted buffer is never live: closed if unsaved, dropped if
  unmodified.

If every unsaved buffer was installed, the source is retired as before. A
session with conflicts is set aside as before. If some unsaved buffer cannot
be installed, nothing is installed and the source stays whole and
discoverable. Its notice is
`additional session recovery deferred; journal retained (tab capacity: N unsaved tabs)`.

Other rules kept:
- `install_session` failure still sets the source aside and retries.
- The active tab survives, or its right (else left) live neighbour takes over,
  the edit-dzk1 skip rule.

Partial notice:
`restored N unsaved tabs[ (K closed: Ctrl+Shift+T)]; M unmodified tabs from the last session were not reopened`.
A session with conflicts appends the existing changed-on-disk text after "; ".

## How "unmodified" is decided (`unsaved_buffer`)

- Untitled: unsaved iff the replayed text is nonempty.
- File whose BASE is the file itself and that has no ops after the BASE:
  unmodified. Replay validated the BASE identity and read exactly those bytes.
- Any other file buffer: the replayed bytes are compared with the target file
  on disk (regular file, size, chunked pread + memcmp). This is the comparison
  `install_buffer` uses for its clean flag, so the result is exact, and a
  dropped buffer's text is exactly what is on disk.
- An edit-fi2 retained-copy BASE is compared like any other file. Forcing it to
  "unsaved" made the fixture nondeterministic: retention may or may not land
  before the checkpoint.
- An interrupted save always counts as unsaved. So does a mapped buffer with
  ops, which has no flat text to compare.
- A changed BASE (edit-btj conflict) ranks as unmodified. Its edits are never
  installed, and they stay in the source, which is set aside whole.
- Ops alone cannot decide: the real journal has INSERT+DELETE after almost
  every file BASE, yet their replay equals the BASE (M, shape scan of a copy).
- Cost: one extra read of bytes the startup cost scan already admitted under
  `JOURNAL_STARTUP_SCAN_BYTES`, and only on the over-capacity path.

## Files touched

- `src/editor/recovery.c`:
  - fields `pick`, `partial`, `unsaved`, `unsaved_closed`, `dropped`
  - new `disk_equals`, `unsaved_buffer`, `plan_partial`, `install_partial`
  - `install_session` branches to `install_partial`
  - `restore_source` plans on a misfit, writes the counted deferral and
    partial notices, and appends the conflict notice to the partial one
- `src/editor/private.h`: editor-owned `session_notice[]` (size derivation in
  its comment).
- `tests/editor_session_test.c`: new cases `zizz_partial(false/true)`,
  `zizz_deferred`, `zizz_changed`, selector `--edit-zizz`, and `zizz_all()`
  added to the default run. No other case edited.
- `docs/decisions/edit-zizz.md` (new), this report (new).

## Red run (before the implementation)

```
editor_session_test: zizz live: tabs=0 closed=0 active=18446744073709551615 notice=additional session recovery deferred; journal retained (tab capacity) source=1
editor_session_test:2760: FAIL tabs_count(&e->tabs) == 6 && tabs_closed_count(&e->tabs) == 0
editor_session_test: zizz closed: tabs=0 closed=0 active=18446744073709551615 notice=additional session recovery deferred; journal retained (tab capacity) source=1
editor_session_test:2783: FAIL tabs_count(&e->tabs) == 2 && tabs_closed_count(&e->tabs) == 2
editor_session_test: zizz deferred: tabs=0 closed=0 notice=additional session recovery deferred; journal retained (tab capacity) source=1
```

Red run for `zizz_changed`, added after the manual check showed that
conflicts must not count as unsaved. The run used the first rule
(conflict = unsaved), temporarily restored; exit 1:

```
editor_session_test: zizz changed: tabs=0 closed=0 notice=additional session recovery deferred; journal retained (tab capacity: 7 unsaved tabs) source=1 aside=0
```

## Green run (final code, `ASAN_OPTIONS=detect_leaks=1`, DISPLAY=:97, exit 0)

```
editor_session_test: zizz live: tabs=6 closed=0 active=4 notice=restored 4 unsaved tabs; 5 unmodified tabs from the last session were not reopened source=0
editor_session_test: zizz relaunch: tabs=6 closed=0 notice=(none) journals=1 [...]
editor_session_test: zizz closed: tabs=2 closed=2 active=1 notice=restored 4 unsaved tabs (2 closed: Ctrl+Shift+T); 7 unmodified tabs from the last session were not reopened source=0
editor_session_test: zizz relaunch: tabs=2 closed=2 notice=(none) journals=1 [...]
editor_session_test: zizz deferred: tabs=0 closed=0 notice=additional session recovery deferred; journal retained (tab capacity: 4 unsaved tabs) source=1
editor_session_test: zizz full: tabs=10 closed=1 notice=(none) source=0
editor_session_test: zizz changed: tabs=6 closed=0 notice=restored 4 unsaved tabs; 5 unmodified tabs from the last session were not reopened; file changed on disk since this session; its unsaved edits were set aside source=0 aside=1
zizz: unsaved buffers and clean tabs that fit restored in tab order; source retired; relaunch finds no stale source
zizz: unsaved buffers beyond live room restored as closed tabs; clean tabs dropped; source retired
zizz: more unsaved buffers than capacity keeps the source whole; a launch with room restores all
zizz: changed-on-disk tabs rank as unmodified; the source is set aside whole
```

`--edit-zizz` was repeated 10 times after the retained-copy fix: 0 failures.

## Manual check (private copy of the real journal)

Setup:
- The journal was copied into `mktemp -d` profiles under the session
  scratchpad. The original was never modified.
- Private XDG_DATA_HOME, XDG_RUNTIME_DIR and XDG_CONFIG_HOME.
- `EDIT_REFUSE_REAL_DISPLAY=1 DISPLAY=:97 build/sublimite`, run under
  `tools/with_display_lock.sh`.
- Only the started pid was signalled (SIGTERM, exit 0).

What the journal holds (M, read-only shape scan: record types and lengths
only, no text):
- 1013 buffers: 1000 in the tab list, 13 closed.
- The shipped binary runs with `tab_capacity = 1000` (src/main.c:135), not
  the 128 library default. 1000 live + 13 closed failed `session_fits` only
  because of the temporary-slot rule.
- Contrary to the brief, the 998 `/tmp/*.dot` targets no longer exist. One
  home `.md` changed on disk. The tolerant pass therefore marks 999 buffers as
  conflicts.

Result with the final ranking:
- Notice (status row, screenshot):
  `restored 3 unsaved tabs; 13 unmodified tabs from the last session were not reopened; file changed on disk since this session; its unsaved edits were set aside`
- Tabs: 1000 live, 0 closed. The temporary count was printed by a debug line
  since removed, and agrees with the clean-close checkpoint's shape scan: 1000
  buffers, 1000 tabs.
- The three untitled buffers (654, 29 and 3967 bytes, the coordinator's (M)
  sizes) are live tabs 1, 999 and 1000. Each shows its text:
  - alt+1: "What is the ..."
  - ctrl+PageUp from the last tab, which is also the tab active at launch:
    "xiaoming be ..."
  - alt+9: "Bennett.jl is certainly ..."
- The source journal was set aside (renamed `.set-aside-*`, not deleted),
  because of the conflicts. The set-aside file is byte-identical to the
  original (`cmp`).
- A last run with the final binary (after the retained-copy change) shows the
  same notice. Its clean-close checkpoint again has 1000 buffers and 1000
  tabs, with the three untitled buffers live at 654, 29 and 3967 bytes. The
  real journal has no retained-copy BASEs, so the result could not change.

Screenshots are in the private scratchpad profile (not in the repo).

## Verification

Stamp: BAT0 `Charging`; `uptime` 07:21 load average 3.59, 7.77, 7.06.

- `make -j4 all`: exit 0.
- `build/san/tests/editor_session_test` (full default run, includes
  `zizz_all`): exit 0 (23.4 s real).
- `build/san/tests/editor_recovery_test`: exit 0.
- `build/san/tests/dataloss_test`: exit 0.
- These ran with `ASAN_OPTIONS=detect_leaks=1` and DISPLAY=:97 through the
  display lock.
- Full suite: `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:97 make -j4 check-serial`
  prints `check: 117 test binaries passed`, then the replay CLI and
  display-lock contract steps PASS. Exit 0 (13m33 s real).
- `make check-serial` stands in for `make check`. Same suites, but serial on
  :97, because `make check` would start its own Xvfb servers.
- The "find fixture FAIL", "savectl fixture FAIL", "refwin_pairs: FAIL" and
  "gl_bench: FAIL driver_*" lines in that log are the suites' own negative
  self-checks, or GL being unavailable on Xvfb. The suites passed.

## Out of scope (reported, not fixed)

- Superseded by the follow-up below: missing-target clean tabs are now
  dropped on the over-capacity path. A session that fits still reopens them
  as empty file tabs (existing install behaviour).
- `make check` (tools/check_parallel.py) starts its own Xvfb servers from :100
  upward. A worker restricted to one display has to use `make check-serial`.
  I used `make check-serial` here.
- The brief's 128-live arithmetic does not apply to the shipped binary
  (1000 live, src/main.c:135).
- `.sublimite-base-*` copies can remain in the journal directory after a
  partial restore's relaunch (seen in the test listing). This is the existing
  edit-fi2 retention and orphan sweep; I did not investigate it.

## Follow-up (coordinator, same session): vanished files are not reopened

Change, on the over-capacity path only (`plan_partial`):
- New `target_vanished` helper: an unmodified file tab whose target stat
  returns `OS_E_NOENT` or `OS_E_NOTDIR` is never installed.
- It counts among the "unmodified tabs ... not reopened".
- Unsaved buffers are never dropped.
- Sessions that fit are unchanged.
- A conflict whose file was deleted falls under this rule. Its edits remain in
  the set-aside source (byte-identical to the original journal).

New test case `zizz_vanished` (in `--edit-zizz` and the default run):
- c0 and c1 are deleted before the relaunch.
- tab_capacity 10, closed_capacity 1: 11 buffer slots for 11 buffers, but
  10 live + 1 closed fails the temporary-slot rule.
- Expected: 9 tabs in tab order, active c2 at index 2, "2 unmodified tabs …
  not reopened", source set aside.

Red (before the change, exit 1):
```
editor_session_test: zizz vanished: tabs=10 closed=0 active=4 notice=restored 4 unsaved tabs; 1 unmodified tab from the last session was not reopened; file changed on disk since this session; its unsaved edits were set aside source=0 aside=1
```

Green (exit 0, detect_leaks=1, :97). All earlier zizz lines are unchanged, plus:
```
editor_session_test: zizz vanished: tabs=9 closed=0 active=2 notice=restored 4 unsaved tabs; 2 unmodified tabs from the last session were not reopened; file changed on disk since this session; its unsaved edits were set aside source=0 aside=1
zizz: clean tabs whose files vanished are not reopened and are counted
```

Verification after the follow-up:
- `make -j4 all`: exit 0.
- Under ASan with detect_leaks=1 on :97, all exit 0:
  - `build/san/tests/editor_session_test` (full default run)
  - `build/san/tests/editor_recovery_test`
  - `build/san/tests/dataloss_test`
- The full suite was not rerun, as instructed. The coordinator runs it at
  landing.

Manual check (fresh private copy of the real journal, final binary, :97):
- Notice: "restored 3 unsaved tabs; 1008 unmodified tabs from the last session were not reopened; file changed on disk since this session; its unsaved edits were set aside".
- Tabs: 5 live, 0 closed: untitled, config.toml, CLAUDE.md (changed on disk,
  shown as disk text), untitled, untitled. The arithmetic is 1013 - 5 = 1008.
  The clean-close checkpoint's shape scan agrees: 5 buffers, 5 tabs.
- The three untitled tabs show their text: "What is the …", "xiaoming be …"
  (active at launch), "Bennett.jl is certainly …".
- The source was set aside and is byte-identical to the original (`cmp`).

Background work: none left running. The one leftover log monitor (a
`tail -f` on the check log) was stopped.

## Round 2 (Codex review docs/reviews/edit-zizz.md, on top of 75ee874)

### 1. BLOCKER: comparison race could drop unsaved text and delete the source

Fixes in `src/editor/recovery.c`:
- **Set aside instead of delete.** A partial restore that dropped any buffer
  (`session.partial && session.dropped`, no conflicts, no failure) now sets its
  source aside with `editor_set_aside`. It is never unlinked.
  - restore_source returns before the retained-copy unlink loop, so retained
    BASE copies stay.
  - `journal_retire_directory` already scans set-aside sessions for BASE
    references and pins them (retire.c inventory: `a->aside`).
  - Notice suffix: "; the old session journal was set aside". It is not
    repeated when the conflict text already says the journal was set aside.
- **Revalidated comparison.** `disk_equals` now fstats after the read and
  stats the name. Identity, kind, size, mtime and ctime must all equal the
  pre-read fstat, otherwise the buffer ranks as unsaved.
- Sessions that fit, and partial restores that dropped nothing, are retired as
  before.

Deterministic tests in `tests/editor_recovery_test.c`:
- Seam: the file includes recovery.c (as the edit-fi0p F7 cases do), with
  macro hooks on `journal_replay_file`, `os_file_pread` and
  `editor_checkpoint`.
- Fixture: c0, c1 and m with "M" typed (unsaved "Mm"), relaunched at 2 live /
  1 closed.
- `zz_race(false)`: m is rewritten to "Mm" after replay, so the comparison is
  honestly equal. It is reverted to "m" at the checkpoint, before retirement.
  Assert: the set-aside source replays m as "Mm".
- `zz_race(true)`: the same, but the revert happens right after the comparison
  read, before revalidation. Assert: m is restored as a modified "Mm" tab, and
  the source is set aside.

Red (pre-round-2 recovery.c, exit 1): m's text is gone, no restored tab and no
source:
```
editor_recovery_test: zizz race before-retire: tabs=2 live_Mm=0 source=0 aside=0 aside_m=(none) notice=1 unmodified tab from the last session was not reopened
editor_recovery_test: zizz race during: tabs=2 live_Mm=0 source=0 aside=0 aside_m=(none) notice=1 unmodified tab from the last session was not reopened
```

Green (exit 0):
```
editor_recovery_test: zizz race before-retire: tabs=2 live_Mm=0 source=0 aside=1 aside_m=Mm notice=1 unmodified tab from the last session was not reopened; the old session journal was set aside
editor_recovery_test: zizz race during: tabs=2 live_Mm=1 source=0 aside=1 aside_m=Mm notice=restored 1 unsaved tab; 1 unmodified tab from the last session was not reopened; the old session journal was set aside
```

### 2. MINOR: rewritten retained-copy notice replaced the partial notice

The rewritten loop now appends to `session_notice` when a partial notice is
showing. `session_notice` is resized for the recovered and kept suffixes
together (derivation in private.h).

Test `zizz_rewritten` (editor_session_test):
- Setup: the child waits for m's edit-fi2 copy to be READY and used, so the
  checkpoint BASE is the copy. m is then rewritten in place before the
  relaunch at 6 / 1.
- Red: `zizz rewritten: tabs=6 notice=file changed on disk since this session; its unsaved edits were recovered`.
- Green: `zizz rewritten: tabs=6 notice=restored 4 unsaved tabs; 5 unmodified tabs from the last session were not reopened; file changed on disk since this session; its unsaved edits were recovered; the old session journal was set aside`.
- All bytes are verified too (m restored as "Mm", modified).

### 3. MINOR: relaunch checks only counted tabs

Changes to `zizz_partial(false/true)`:
- The partial restore runs in a child that `_exit`s right after `editor_open`
  returns: no settle and no clean close, so a crash right after the
  replacement checkpoint and the set-aside.
- The parent relaunches and verifies, with `zizz_bytes_live` /
  `zizz_bytes_closed`, every tab's complete bytes and modified state. That
  covers all four unsaved buffers, live and closed (closed via
  `tabs_closed_at`), plus the clean c0 and c1.
- It also verifies no notice, no deferral, and one session journal.
- The first launch now also asserts that the set-aside source exists and holds
  the closed untitled buffer's text.

The red run fails the updated cases (old notice, source deleted):
```
editor_session_test:2789: FAIL e->recovery_notice && !strcmp(e->recovery_notice, "restored 4 unsaved tabs; 5 unmodified tabs from the last session were not reopened; the old session journal was set aside")
editor_session_test:2807: FAIL ... "the old session journal was set aside")
```

Green (`--edit-zizz`, exit 0):
```
zizz: unsaved buffers and clean tabs that fit restored in tab order; source set aside; crash relaunch keeps every byte
zizz: unsaved buffers beyond live room restored as closed tabs; clean tabs dropped; source set aside; crash relaunch keeps every byte
zizz: more unsaved buffers than capacity keeps the source whole; a launch with room restores all
zizz: changed-on-disk tabs rank as unmodified; the source is set aside whole
zizz: clean tabs whose files vanished are not reopened and are counted
zizz: a rewritten retained-copy target appends its notice to the partial restore's
```

Not added: the review's further list (checkpoint BUSY and failure on the
partial path, interrupted saves, mapped edits, retained-copy conflicts,
occupied dirty closed pools, PAINT). These are outside the coordinator's three
items.

### Verification (round 2)

- `make -j4 all`: exit 0.
- Under ASan with detect_leaks=1 on :97, all exit 0:
  - `editor_session_test` (full default run, 6 zizz lines)
  - `editor_recovery_test` (4 lines)
  - `dataloss_test`
- The full suite was skipped, as instructed.

### Manual check (round 2, fresh private copy, final binary, :97)

- Notice: "restored 3 unsaved tabs; 1008 unmodified tabs from the last session were not reopened; file changed on disk since this session; its unsaved edits were set aside".
  - This journal has conflicts, so the set-aside text is the conflict one; the
    kept suffix is not repeated.
- Tabs: 5 live, 0 closed: untitled, config.toml, CLAUDE.md, untitled,
  untitled. The clean-close checkpoint agrees: 5 buffers, 5 tabs; untitled
  654, 29 and 3967 bytes.
- The three untitled tabs show their text: "What is the …", "xiaoming be …",
  "Bennett.jl is certainly …".
- The source was set aside and is byte-identical to the original.

### Notes

- Set-aside sources from partial restores accumulate, one per over-capacity
  launch that drops something. retire.c pins set-aside sessions that hold
  edits. Releasing one is the operator's `journal_replay --acknowledge`
  (edit-fi0p F12).
- Under a JOURNAL_BUSY replacement checkpoint, the kept source is set aside
  immediately, as the conflict branch already does. That is no loss, but if
  the owed checkpoint never lands, the restored state is reachable only
  through the set-aside file.

Background work: none running.

## Round 3 (Codex re-review docs/reviews/edit-zizz-r2.md)

### Deviation from the coordinator's rule (needs a decision)

"Droppable only without INSERT/DELETE records" cannot be applied as written.
Untouched small files are checkpointed as DELETE-all + INSERT-all
(edit-28u decision 5, checkpoint.c:101-104).
- With that rule, all 11 fixture buffers ranked unsaved: every case deferred
  ("11 unsaved tabs").
- Any over-capacity session of existing files would be deferred, which brings
  back the original bug.

Implemented instead:
- Droppable: no records.
- Droppable after comparison: self-contained records (first op deletes the
  whole BASE, or an empty BASE) whose bytes equal disk. The source journal
  holds every byte, and partial restores set the source aside.
- Always unsaved: delta records (files above FILE_PREFIX_MAX, retained
  copies). This closes the review's mutable-BASE hole.
- `disk_equals` (with revalidation) is kept only for the self-contained case.

If the coordinator still wants comparison gone, the alternative is a
different rule for whole-text records, e.g. trusting them without comparing.
That would drop user text, so I did not do it.

### Changes

- `src/editor/recovery.c`:
  - `unsaved_buffer` is reworked and a new `self_contained` helper is added.
  - The kept (set-aside) notice no longer replaces an install-set notice
    (edit-dzk1 first paint). `prior` is hoisted to restore_source scope.
- `tests/editor_recovery_test.c`:
  - The round-2 race seams and race tests are removed.
  - New `zz_classify` unit test: no ops = droppable, whole-text equal =
    droppable, whole-text differs = unsaved, deltas equal to disk = unsaved.
- `tests/editor_session_test.c`: new `zizz_paint`. A second launch dies in m's
  first paint; the relaunch at 6/1 must show the first-paint warning,
  m closed, and the source set aside.
- The edit-duuw tests are untouched.

### Red (round-2 recovery.c)

```
editor_recovery_test: zizz classify: none=0 whole_equal=0 whole_differs=1 delta_equal=0 (1 = unsaved)
editor_session_test: zizz paint: tabs=5 closed=1 notice=the old session journal was set aside aside=1
```

### Green

```
editor_recovery_test: zizz classify: none=0 whole_equal=0 whole_differs=1 delta_equal=1 (1 = unsaved)
editor_session_test: zizz paint: tabs=5 closed=1 notice=a restored tab died in its first paint last time; it is closed (reopen: Ctrl+Shift+T) aside=1
```

All seven zizz session lines pass.

### Verification

- `make -j4 all`: exit 0.
- Under ASan with detect_leaks=1 on :97, all exit 0:
  - `editor_session_test` (full default run)
  - `editor_recovery_test`
  - `dataloss_test`

### Manual check (fresh private copy, final binary)

- Notice: "restored 3 unsaved tabs; 1008 unmodified tabs from the last session were not reopened; file changed on disk since this session; its unsaved edits were set aside".
- Tabs: untitled, config.toml, CLAUDE.md, untitled, untitled (5 live, 0 closed).
- The three untitled buffers keep their 654, 29 and 3967 bytes in the
  checkpoint.
- The set-aside source is byte-identical to the original.

### Missing

No end-to-end test of a delta-encoded buffer (file above FILE_PREFIX_MAX)
through a relaunch. The rule is covered by the `zz_classify` unit test only.
