# edit-y315 (s9) worker report

## Cause
`editor_flush` (exit) paid an owed Save As checkpoint (`publish_owed`, edit-ze59)
with `editor_checkpoint` on the UI thread: `journal_rotate` ran there with no
deadline. A worker publication already running at exit was also waited for
without a deadline (`editor_flush`'s save loop, `editor_session_close`).

## Fix (docs/decisions/edit-y315.md)
- The exit flush starts the edit-ze59 worker publication (`editor_publish_start`)
  for an owed Save As checkpoint once `journal_flush` has made the journal durable.
  A refused start falls back to the former UI checkpoint. FULL/IO/error/busy
  checkpoints are unchanged.
- New `editor_publish_wait` (session.c, exit paths only) adopts the job, waiting at
  most one `JOURNAL_DEFAULT_SYNC_NS` tick (E, the tick flush_settle/close_result
  already wait) from the first wait on that job (`publish_deadline_ns`, reset in
  `editor_publish_start`). Past it `editor_flush` returns `EDITOR_MORE`;
  `close_result` treats that as closed, not `fail()`.
- `editor_close` leaves an overdue job to process exit. It closes, joins and frees
  nothing, and the session lock stays held. The journal file stays for recovery.

## Files touched
- src/editor/editor.c (editor_flush, close_result)
- src/editor/session.c (editor_publish_wait; deadline reset in editor_publish_start)
- src/editor/open.c (editor_close)
- src/editor/private.h (publish_deadline_ns field, editor_publish_wait decl)
- src/editor/editor.h (editor_flush comment)
- tests/dataloss_test.c (y315_healthy, y315_stall; `--y315`, `--y315-stall`, default run)
- docs/decisions/edit-y315.md, docs/worker-reports/edit-y315-s9.md

## Red (before the fix; power Full, load1 9-11, sanitized)
```
$ build/san/tests/dataloss_test --y315
dataloss_test:1246: FAIL flushed == 0 && !owed && rebound && atomic_load(&io.ui_rotations) == 0 && atomic_load(&io.worker_rotations) > 0
dataloss_test: edit-y315 owed Save As checkpoint at exit, healthy journal: flush=0 owed=0 UI rotations=1 worker rotations=0
exit=1
$ build/san/tests/dataloss_test --y315-stall
dataloss_test:1222: FAIL r.ui_ns <= Y315_EXIT_BOUND_NS && !r.timed_out
dataloss_test: edit-y315 owed Save As checkpoint at exit, stalled rotation: UI exit=10007.694 ms bound=2000.000 ms (G) flush=0 UI rotations=1 worker rotations=0 held=1 timed_out=1 [power=Full load1=8.65 build=sanitized]
exit=1
```

## Green (power Full, 04:42 load1 6.76)
```
$ build/san/tests/dataloss_test --y315
dataloss_test: edit-y315 owed Save As checkpoint at exit, healthy journal: flush=0 owed=0 UI rotations=0 worker rotations=1
dataloss_test: edit-y315 owed Save As checkpoint at exit, stalled rotation: UI exit=1002.088 ms bound=2000.000 ms (G) flush=1 UI rotations=0 worker rotations=1 held=1 timed_out=0 [power=Full load1=6.76 build=sanitized]
dataloss_test: edit-y315 recovered 1 tab(s): .../build/dataloss-WrO4CF/source modified=1
exit=0
```
(G) bound = 2 * JOURNAL_DEFAULT_SYNC_NS: the deadline tick plus the retry tick
flush_settle already allows. (E) The watchdog is EDITOR_IDLE_NS.

## Verification (power Full, load1 6.3-7.0)
- `make -j3 all` (gcc, -Werror): exit 0
- ASAN_OPTIONS=detect_leaks=1 build/san/tests/dataloss_test: exit 0 (y315 lines included; stall UI exit 1002.504 ms)
- build/san/tests/editor_checkpoint_test: exit 0
- build/san/tests/journal_test: exit 0
- build/san/tests/editor_close_test: exit 0
- XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=3600 sh tools/with_display_lock.sh build/san/tests/editor_session_test: exit 0
- same, build/san/tests/editor_test: exit 0
- same, build/tests/editor_test (release, malloc guard active): exit 0 (12 "mallocs=0" lines; "clipboard malloc guard: 0 allocations (active)")

## Rebase against edit-9fzc
No line overlaps 9fzc's hunks. 9fzc changes retain.c, commands.c, buffers.c,
private.h near line 680, and dataloss_test after `ze59_save_as` plus the main
lines after `--ze59-save-as` and `T(ze59_retain_stall...`. This bead changes
private.h near lines 339 and 575 and inserts its test before the `ze59_save_as`
block. Its selectors follow `--open-gib`, and its default-run call follows the
`external_rewrite` line. Duplication is functional only: `y315_rename` counts
rotations by thread like 9fzc's `followup_rename`. After the rebase, the last
paragraph of docs/decisions/edit-9fzc.md ("The exit path remains synchronous")
is superseded by edit-y315.md.

## Remaining / out of scope (reported, not fixed)
- The exit flush still waits without a deadline on a journal worker that is stalled
  with staged edits or metadata pending (`journal_flush` before the checkpoint), and
  on a Save As or Ctrl+S write stalled in storage (the `editor_save_active` loop).
  This bead bounds only the worker checkpoint.
- `editor_find_close`/`editor_hl_close` run before the deadline check in
  `editor_close`. If they wait for pool jobs queued behind an overdue publication
  on the same bulk lane, they would block (not observed).
- After an abandoned close, main's EDIT_TRACE_DUMP comment ("editor_close joins
  workers") does not hold: the held worker may still emit trace events.
- main.c reports `sublimite: error 1; journal: <path>` and exit status 1 when
  the deadline expired (flush_settle returns EDITOR_MORE). main.c is unchanged.
- Flake risk: tests that call `editor_flush`/`editor_close` while a healthy
  retention publication runs now wait at most one tick (1 s). Under extreme
  load a rotation slower than that would return EDITOR_MORE or skip teardown
  (LSan would report the leak). (M) ze59 measured the whole publication at
  7.9-21.2 ms.
