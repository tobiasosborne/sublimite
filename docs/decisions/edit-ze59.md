# edit-ze59: retention publication and Save As leave the UI thread

Bead edit-ze59 (docs/reviews/recovery-3.md F10). Worker session s8,
2026-10-18. Code: src/editor/session.c (worker checkpoint), retain.c,
checkpoint.c, commands.c (Save As), editor.c, buffers.c, private.h. Tests:
tests/dataloss_test.c `--ze59-stall`, `--ze59-save-as` (also in the default run).

## Bug

`editor_retain_tick` ran `journal_flush` + `editor_checkpoint` on the UI
thread once a BASE copy was READY (edit-fi2). `journal_flush` polls the
journal worker with no deadline, so a stalled journal sync froze the UI
(red: the turn that saw READY took 10017.8 ms, until the test's watchdog
released the stall). Save As called `file_save_write` for the whole file on
the UI thread (red: 100 MB sparse, 161.2 ms UI turn; a key typed after Return
reached the screen 161.5 ms after input).

## Choice 1: a worker checkpoint under a journal lease

`editor_publish_start` (session.c) pins the published immutable session graph
(the one save prepare encodes), and submits one WORK_BULK job on the main pool.
The job encodes the complete checkpoint with the save path's two-pass builder
(`prepare`, new `plain` mode: no selected buffer, no replaced target), then
runs `journal_flush` + `journal_rotate` on the worker. While the job runs,
`e->publish` is set. `editor_save_leased` and `editor_save_active` report it,
so every gate that waits for a save prepare also waits for it: no append,
pump or drain of the journal on the UI, input parks (queue_pick), no
retirement, no file command, no Reload/Keep/restore checkpoint. This is the
save controller's ownership pattern. A wake-up message (`EDITOR_PUBLISH_MESSAGE`)
and an atomic `done` flag carry completion. The UI adopts the result in
maintenance (`editor_retain_tick` → `editor_publish_poll` →
`editor_checkpoint_published`), and reads the result only after `done`. The
worker reports per slot whether a leaf named its retained copy (`used`). That
sets `retain_building`, and `editor_retain_checkpointed` then runs as before.

**Started only while the journal is quiescent**: no journal job in flight
(`journal_waiting`), nothing accepted that is not durable, no pending batch,
no staged ops or metadata, plus fi2's existing gates (no queued input, no frame
in flight, no save) and edit-ivv's `editor_checkpoint_due`. The worker's flush
then has nothing to wait for. So a stalled journal worker never leases the
journal, and typing continues on the other batch (green: key under a stalled
journal worker with READY retention lands in 0.134-0.225 ms (M)). The
publication follows once the sync completes, at most one sync interval after
the last edit (`JOURNAL_DEFAULT_SYNC_NS`), which is the cadence that already
makes edits durable.

Graph freshness is checked at start: every live, closed and non-retired buffer
must have a leaf of its current revision, and the live leaf count must equal the
number of recorded tabs. A buffer still loading (`pending_open`) has no leaf, so
the start is refused and a later tick retries. The previous rule stands: a
failed reference checkpoint retries after the next mutation (`retain_retry`).
JOURNAL_BUSY additionally keeps edit-ivv's tick (`checkpoint_refused`). While
the job runs the retain tick does nothing else (no copy job submission onto
the journal pool, no READY transition, no copy removal). A READY copy that the
checkpoint did not reference is therefore exactly one that was READY at
capture.

## Choice 2: Save As writes on the worker

`save_as` validates as before (hw1 refusal, canonical target, baseline stat).
It then creates a journal-less save controller (`savectl`, the save path's
worker-owned write, epoch validator and mapped-source guard) and hands it the
snapshot taken at the command. The BASE capture of the written target runs on
the same worker, in the request's release callback. The UI keeps typing.
`editor_save_as_tick` (maintenance, between frames) adopts the result and runs
the former synchronous tail: replacement controller, rebind, saved reference,
tab rename, watch, peers. The history position of the written revision is
`save_cursor`/`save_tracking`, kept across later edits as for Ctrl+S, so keys
typed during the write stay modified. A closed or retired buffer is not
rebound, and the file stays written. A failed write shows "save as failed".
The replacing checkpoint is the worker checkpoint above (`publish_owed`).
Until it lands, the retained copy of the former BASE is kept, and a crash in
that window recovers the text to the old target, as modified. Ctrl+S is
ignored, and file commands wait, while a Save As or its checkpoint is in
flight. Peers on the target that were modified while the write ran are not
reloaded (their edits stay; the new BASE makes their checkpoint whole-text).

## Remaining (known limits)

- Input parks for the duration of the worker's rotation (temp write plus
  syncs): fi2 measured 7.9-21.2 ms (M) for the whole UI-side
  publication. A disk that stalls *after* the lease began (in the rotation
  itself) parks typing until the rotation returns. The UI stays live, and
  queued keys land afterwards. This is the same bound as a Ctrl+S prepare. Not
  parking would mean appending to the old journal during the rotation, which is
  a journal protocol change.
- A peer tab open on the Save As target is still reloaded with
  `editor_reload_source`, which checkpoints on the UI thread (pre-existing,
  rare).
- `editor_flush` (exit) still writes an owed checkpoint on the UI thread, as
  for FULL/IO.
- The graph checkpoint's TABS record needs dense ranks. While a derived (Find
  Results) tab sits before others, the worker checkpoint fails with
  JOURNAL_INVALID and retention retries after the next mutation. edit-3f2k
  (landing in parallel) fixes the ranks for saves and this path alike.

## Numbers

(G) G1 typing p99 = 2 ms (perf/01-perf-target.md §0.2), enforced in the stall
test on release builds only (the editor_large_test convention) and printed
everywhere. (G) The 100 MB fixture comes from the bead, as 100 * 1000 * 1000 B,
sparse. (E) Test watchdogs reuse EDITOR_IDLE_NS. No new duration, count or
allocation budget is introduced. The journal tick is the existing
`JOURNAL_DEFAULT_SYNC_NS`.
