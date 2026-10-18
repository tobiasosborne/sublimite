# edit-ivv worker report: JOURNAL_BUSY retried on every checkpoint path

The design is in docs/decisions/edit-ivv.md.

## Files touched

- src/editor/checkpoint.c: the shared helper. On BUSY, `editor_checkpoint`
  schedules the retry one tick later (`busy_due_ns = now +
  JOURNAL_DEFAULT_SYNC_NS`) and counts `busy_refusals`. The notice appears
  only on the second consecutive refusal. Success clears the retry and calls
  `editor_recovery_owed_done`. Also new here: `editor_checkpoint_due` and
  `editor_checkpoint_owed`.
- src/editor/private.h: new fields `busy_due_ns`, `busy_refusals`,
  `busy_checkpoint`, `busy_source_lease` and `busy_source`, plus three
  prototypes.
- src/editor/recovery.c: when `install_session`'s checkpoint is refused, the
  install stands and a checkpoint is owed. The source is neither set aside nor
  unlinked; its path and lease move to the editor and discovery stops. New
  function `editor_recovery_owed_done` retires the source once a checkpoint
  lands.
- src/editor/restore.c: new `RESTORE_RETRY` state. On BUSY the restore rolls
  back and drops its replacements. It keeps the program, the source and its
  lease, then runs `prepare_all` again when the tick is due.
  `editor_restore_free` counts the new state.
- src/editor/buffers.c: `reload_source` reports a refusal; `editor_reload_source`
  keeps its signature. A refused Keep or Reload is re-armed. The choice block
  and the automatic refresh wait for the tick. A refused clean refresh is not
  turned into a stale prompt.
- src/editor/editor.c: `wait_timeout` wakes for a future `busy_due_ns`.
  `editor_step` calls `editor_checkpoint_owed`. `editor_flush` takes the
  checkpoint branch while a checkpoint is owed. New function
  `editor_flush_settle`: flush, and on BUSY sleep one tick and flush once more.
- src/editor/editor.h: declares `editor_flush_settle`.
- src/editor/open.c: `editor_close` uses `editor_flush_settle` and closes a
  held source lease. Initialises `busy_source_lease = -1`.
- src/main.c: the final flush uses `editor_flush_settle`.
- src/savectl/savectl.c: new `busy_wait`. On the worker it sleeps one tick and
  retries once, for prepare, cancel, recover-finish rotation and finish. A
  copy retained by a refused prepare is unlinked before the second prepare.
- tests/editor_session_test.c: new `--edit-ivv` option and `ivv_all`, also run
  in the default suite. It covers cases (a) recovery install, (b) lazy restore
  on the 1 GiB sparse qct/e601 fixture, (c) save at prepare and save at finish,
  and (e) `editor_close` and the final flush.
- tests/dataloss_test.c: new `--edit-ivv` option and `ivv_choice`, also run in
  the default suite. It covers case (d) Keep and Reload, and checks the notice
  once the refusal has outlasted the bound.
- tests/cli_test.c: interposes `editor_flush_settle` instead of `editor_flush`,
  because main now calls `editor_flush_settle`.
- docs/decisions/edit-ivv.md.

## Red run

Clang ASan/UBSan, detect_leaks=1, before any implementation. Only the new
fields and an `editor_flush_settle` stub that called plain `editor_flush`
existed.

```
edit-ivv (a): guard held editor_open=0 refusals=0 owed=0 notice=session recovery failed; its journal was set aside aside=build/editor-ivv-Y75R0W/session-OnPcDq.set-aside-1792284234984874599 source_kept=no len=0
editor_session_test:1803: FAIL e->busy_refusals > 0 && e->busy_checkpoint && !*e->recovery_aside && !access(first, F_OK)
edit-ivv (b): guard held rc=0 restore_active=0 refusals=0 placeholder=no source_kept=yes notice=large session journal could not be restored; journal retained
edit-ivv (b): guard released rc=0 restored=no len=0 source_retired=no refusals=0
editor_session_test:1855: FAIL waiting && live && retired && consistent == 0
edit-ivv (c): save prepare guard: modified=yes save_error=4 operation_error=4
editor_session_test:1894: FAIL disk_is(f.path, "xbase") == 0
edit-ivv (c): save finish guard: modified=yes save_error=4 operation_error=4
editor_session_test:1894: FAIL !modified && !f.e->buffer->save_error && !f.e->buffer->operation_error
edit-ivv (e): editor_close with guard held: flushed=0 rotated=no
edit-ivv (e): final flush with guard held: flushed=4 rotated=no
editor_session_test:1923: FAIL flushed == 0 && after.st_ino != before.st_ino && file_contains(log, "z")
dataloss_test: edit-ivv keep with guard held: refusals=0 choice=0 stale=1
dataloss_test:949: FAIL e->busy_refusals > 0 && e->source_choice && e->buffer->source_stale
```

Each line matches the failure the brief predicts: (a) the session is set
aside, (b) restore_fail leaves the placeholder empty, (c) the save is reported
failed even when the file was written (the finish case), (d) Keep is silently
abandoned, (e) the close checkpoint is skipped. `4` is `JOURNAL_BUSY`.

## Green run

```
edit-ivv (a): guard held editor_open=0 refusals=1 owed=1 notice=(none) aside=(none) source_kept=yes len=2
edit-ivv (a): guard released refusals=0 source_kept=no lock_kept=no
edit-ivv (b): guard held rc=0 restore_active=1 refusals=1 placeholder=yes source_kept=yes notice=restoring large session journal
edit-ivv (b): guard released rc=0 restored=yes len=1073741825 source_retired=yes refusals=0
edit-ivv (c): save prepare guard: modified=no save_error=0 operation_error=0
edit-ivv (c): save finish guard: modified=no save_error=0 operation_error=0
edit-ivv (e): editor_close with guard held: flushed=0 rotated=yes
edit-ivv (e): final flush with guard held: flushed=0 rotated=yes
edit-ivv: busy directory guard: recovery install, lazy restore, save prepare/finish, close and final flush retry on the journal tick
dataloss_test: edit-ivv keep with guard held: refusals=1 choice=1 stale=1
dataloss_test: edit-ivv keep past the bound: refusals=2 notice=session journal directory busy (another editor is tidying it); retrying
dataloss_test: edit-ivv keep landed after the guard was released
dataloss_test: edit-ivv reload with guard held: refusals=1 choice=2 stale=1
dataloss_test: edit-ivv reload landed after the guard was released
```

Disk use for the GB fixture in (b), which is sparse and removed on every exit
path. The 1 G drift was other load on the shared box, not the fixture.

- red: before 396G used / 60G avail; after 396G / 60G.
- green: before 397G / 59G; after 397G / 59G.

## Verification

Box state: BAT0 "Charging", then "Not charging" during the suites. Load
average 4.97 at the build, 13.55 at the suites. None of these runs is a timing
claim.

| Command | Exit |
|---|---|
| `make -j3 all` (gcc release, -Werror) | 0 |
| `build/san/tests/editor_session_test` (full) | 0 |
| `build/san/tests/editor_checkpoint_test` | 0 |
| `build/san/tests/dataloss_test` (full) | 0, first run (the edit-1wc two_process_save race did not show) |
| `build/san/tests/journal_test` | 0 |
| `build/san/tests/editor_close_test` | 0 |
| `build/san/tests/cli_test` on :99 under the lock | 0 |
| `build/tests/editor_test` release on :99 under the lock | 0 ("all passed"; malloc guard lines show 0 allocations) |
| `build/san/tests/editor_test` on :99 under the lock | first run 1 (`raster_blink_damage`, see below); rerun 0 ("all passed", no sanitizer reports; load average 47) |

All sanitizer suites ran with ASAN_OPTIONS=detect_leaks=1. No LeakSanitizer,
AddressSanitizer or UBSan reports appeared in the headless runs.

The first sanitizer editor_test run failed at `editor_test:1563` inside
`raster_blink_damage`: after a focus-out and one step, `!blinking &&
!pending` did not hold. The four leaks reported after it are that test's
backend, left alive by the early return. This bead changed nothing on that
path: no checkpoint is refused in editor_test, so `busy_due_ns` stays 0 and
`editor_checkpoint_owed` returns at once. The run was at load average 13-16, and the rerun passed unchanged at load
average 47, so the failure is load-sensitive timing in that test, not this
bead.

## What remains

Nothing in scope.

## Found outside scope (reported, not fixed)

1. `commands.c` `save_as` treats BUSY as a hard error. It covers Save As onto a
   path that other tabs also have open: it reloads them and checkpoints, and
   BUSY from that checkpoint, or from a refused `editor_reload_source` (`rc ||
   !other->retired`), becomes `EDITOR_ERR_IO`. The file is already written by
   then. This is the same bug class, at a site the brief does not list.
2. `editor_reload_source` silently abandons an explicit Reload in three other
   cases: no free buffer slot, `file_save_busy`, or `e->op_count`. All three
   predate this bead.
3. During the window before an owed recovery checkpoint lands, edits to
   recovered buffers append records with no BASE in the new journal. A crash
   inside that window means the new journal is set aside on relaunch. The
   source is intact and is recovered. Details are in the decision's "Known
   limits".
