# edit-ze59 (recovery-3 F10): worker report, session s8

Decision doc: docs/decisions/edit-ze59.md.

## What changed

- **Retention publication runs on a worker.** `editor_publish_start`
  (session.c) pins the published session graph and submits one WORK_BULK job.
  The job encodes the complete checkpoint with the save path's two-pass
  builder (new `plain` mode), then runs `journal_flush` + `journal_rotate`.
  While it runs, `e->publish` leases the journal: `editor_save_leased` and
  `editor_save_active` report it, so the UI parks exactly as it does behind a
  save prepare. The UI adopts the result in maintenance (`editor_publish_poll`,
  then `editor_checkpoint_published`). `editor_retain_tick` starts the job only
  when the journal is quiescent: no batch in flight, everything accepted
  already durable. A stalled journal worker therefore never leases the journal.
- **Save As writes on the worker.** A journal-less `savectl` writes the
  snapshot taken at the command. The BASE capture runs on the same worker
  (release callback). `editor_save_as_tick` (maintenance) adopts the result:
  it runs the former synchronous tail, then the replacing checkpoint goes
  through the worker checkpoint (`publish_owed`).

## Files touched

- src/editor/session.c: worker checkpoint (`editor_publish_*`); `plain`
  capture; the lease flags in `editor_save_active` / `editor_save_leased`;
  Ctrl+S ignored during a Save As; save start waits for the publish job;
  teardown waits for it.
- src/editor/retain.c: poll/adopt the job; quiescence gate; start the job
  instead of the UI flush + checkpoint; keep the former BASE copy while a
  Save As checkpoint is owed.
- src/editor/checkpoint.c: `editor_checkpoint_published`; a UI checkpoint
  also pays `publish_owed`.
- src/editor/commands.c: async `save_as`, `save_as_adopt`,
  `editor_save_as_tick`, `editor_save_as_runnable`, `editor_save_as_close`;
  file commands wait while a checkpoint is owed.
- src/editor/editor.c: ignore the wake-up message; `editor_flush` drives
  both and pays an owed checkpoint; `runnable` while a finished write awaits
  adoption; `stats.pending` covers both.
- src/editor/buffers.c: `editor_save_as_tick` in `editor_poll_sources`.
- src/editor/private.h: types, fields, prototypes, `EDITOR_PUBLISH_MESSAGE`.
- tests/dataloss_test.c: `ze59_retain_stall`, `ze59_save_as` (selectors
  `--ze59-stall`, `--ze59-save-as`, and in the default run).
- docs/decisions/edit-ze59.md, this report.

## Red run (unchanged src, release build/tests/dataloss_test)

Stamp: power "Not charging", load1 29.7 then 21.4 (12:52-12:56).

```
$ build/tests/dataloss_test --ze59-stall
dataloss_test:1047: FAIL !atomic_load(&stall->timed_out) && atomic_load(&stall->hold)
dataloss_test: edit-ze59 stall: retain_state=2 retain_used=1 blocked=1 longest UI turn=10017.777 ms timed_out=1 [power=Not charging load1=29.72 build=release]
rc=1
$ build/tests/dataloss_test --ze59-save-as
dataloss_test:1123: FAIL landed < saved
dataloss_test: edit-ze59 Save As 100000000 B sparse: Z input->submit=161.504 ms command->rebound=161.290 ms longest UI turn=161.204 ms (M) G1 p99=2.000 ms written=100000000 B [power=Not charging load1=21.43 build=release]
rc=1
```

In the stall run, the UI turn that saw READY blocked in `journal_flush`
until the test's watchdog (EDITOR_IDLE_NS) released the held fdatasync. In the
Save As run, the key typed after Return reached the screen only after the
whole 100 MB write.

## Green run

Release, full `build/tests/dataloss_test` (13:01, "Not charging", load1 20.8): rc=0

```
dataloss_test: edit-ze59 stall: retain_state=2 retain_used=0 blocked=1 longest UI turn=0.225 ms timed_out=0 [power=Not charging load1=20.84 build=release]
dataloss_test: edit-ze59 keystroke under stalled journal worker + READY retention: input->submit=0.228 ms (M) bound=2.000 ms (G1 p99) held=1 timed_out=0 [power=Not charging load1=20.84 build=release]
dataloss_test: edit-ze59 retention published off the UI thread after the stall released
dataloss_test: edit-ze59 Save As 100000000 B sparse: Z input->submit=4.663 ms command->rebound=391.914 ms longest UI turn=18.984 ms (M) G1 p99=2.000 ms written=100000000 B [power=Not charging load1=20.84 build=release]
```

Repeated `--ze59-save-as` runs, release, load1 22.9-23.1, "Not charging": Z
input->submit was 6.3 / 7.7 / 11.5 / 16.4 / 11.4 ms against command->rebound of
442 / 235 / 149 / 410 / 155 ms, and the longest UI turn was 0.57-6.27 ms.

ASan/UBSan (leaks on), `build/san/tests/dataloss_test` (13:05, load1 24.6): rc=0

```
dataloss_test: edit-ze59 stall: ... longest UI turn=3.980 ms timed_out=0 [... build=sanitized]
dataloss_test: edit-ze59 keystroke under stalled journal worker + READY retention: input->submit=0.378 ms (M) ...
dataloss_test: edit-ze59 Save As 100000000 B sparse: Z input->submit=2.222 ms command->rebound=314.725 ms longest UI turn=2.049 ms (M) ...
```

Enforced: the stall key lands while the stall is still held, and on release
builds within G1 p99 = 2 ms (G). In the Save As test, Z lands before the
rebind, the written size is the snapshot at the command, and the tab stays
modified. The Save As key latency is not a single-keystroke G1 measurement:
Z waits behind the Return frame and the Save As command start (file commands
park input until they are consumed, as before). It includes that start and
is printed, not enforced.

## Verification (exit codes)

- `make -j3 all` (gcc, -Werror): 0
- `make -j3 build/san/tests/{dataloss,editor,editor_session,editor_checkpoint,journal,savectl}_test`: 0
- release `build/tests/dataloss_test`: 0
- release `build/tests/editor_test` (malloc guard, :99 under with_display_lock): 0 ("editor_test: all passed")
- san, `ASAN_OPTIONS=detect_leaks=1`: dataloss_test 0, journal_test 0, savectl_test 0
- san display suites on :99 under tools/with_display_lock.sh (EDIT_DISPLAY_LOCK_TIMEOUT=1800, private XDG_RUNTIME_DIR, leaks on), 13:05-13:11, "Not charging", load1 27.8 -> 12.9: editor_checkpoint_test 0, editor_session_test 0, editor_test 0
- `df -h /`: 72G avail before the fixtures, 71-72G after. Fixtures live under
  build/dataloss-*, are removed on every exit path, and none were left behind.

## Remaining

- Input still parks for the worker's rotation itself (temp write plus
  syncs). A disk that stalls once the lease has begun parks typing until it
  returns. This is the same bound as a Ctrl+S prepare; see the decision doc.
- `editor_flush` (exit) still writes an owed Save As checkpoint on the UI
  thread, as it does for FULL/IO.

## Found outside scope (reported, not fixed)

1. **Retention copy outlives the journal at editor_close.** A retention job
   (`retain_async_worker`) still RUNNING at `editor_close` writes through
   `&j->io` after `journal_close` freed the journal: SIGSEGV in `write_at`
   (journal.c:191) from `retain_copy`, while the UI is in
   `work_pool_shutdown(&e->journal_pool)`. It reproduced in 2 of 3 runs when
   the 100 MB Save As test closed the editor right after typing into the
   rebound buffer. It is pre-existing: `editor_close` order is journal_close →
   journal pool shutdown → buffer destroy (`editor_retain_stop`). recovery-3
   names this shutdown/retention lifetime conflict. The test now waits for
   retention to settle before closing.
2. Save As of a buffer whose target has an open clean peer still calls
   `editor_reload_source`, which checkpoints on the UI thread.
3. The graph checkpoint's TABS encoding indexes by tab rank. A derived or
   placeholder tab before others leaves a hole, which gives JOURNAL_INVALID.
   It affects saves and now the worker checkpoint. edit-3f2k
   (`editor_recorded_rank`, landing in parallel) addresses this. Expect a
   textual merge in session.c's `encode_all`/`publish` area. This bead did
   not touch `encode_all`.
4. `editor_retain_tick` submits retention copy jobs onto the journal pool
   while a Ctrl+S prepare holds the journal lease. The worker's
   `journal_flush` also submits and drains that pool, and work.h declares
   submit/drain UI-only. During the new publish lease the tick now does
   nothing; the save-lease case is unchanged.
