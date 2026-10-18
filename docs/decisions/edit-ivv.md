# edit-ivv: JOURNAL_BUSY is retried on every checkpoint path

edit-dr9 made the shared directory guard non-blocking: `journal_rotate` returns
`JOURNAL_BUSY` while another editor's `journal_retire_directory` holds it
`LOCK_EX`, and the refusal changes nothing. Before this bead only the close path
(`editor.c` `close_result`) retried. Every other caller treated BUSY as a hard
failure. The guard itself is unchanged.

## One rule, one bound

A refused checkpoint keeps its caller's state: the text in memory, the journal
open, and the restore, choice or recovery source pending. The caller asks again
one journal tick later. The tick is `JOURNAL_DEFAULT_SYNC_NS`, the same bound
`close_result` uses, so no new number is introduced. A refusal is never a hard
error and never reaches `fail()`. Only a refusal that outlasts that tick is
shown: on the second consecutive refusal, the status-row notice reads
"session journal directory busy (another editor is tidying it); retrying". A
successful checkpoint clears the notice.

The shared helper sits in `editor_checkpoint` (checkpoint.c). On BUSY it sets
`busy_due_ns = now + JOURNAL_DEFAULT_SYNC_NS` and counts `busy_refusals`. On
success it clears both. Callers test `editor_checkpoint_due(e)` before asking
again. `wait_timeout` wakes for a future `busy_due_ns` only. Once the tick has
passed, the retrying path runs on that step, so the loop cannot spin.
`checkpoint_retry_ns` (the close continuation) is untouched.

## Per path

- **Startup recovery** (recovery.c `install_session`). A refused checkpoint
  sets `busy_checkpoint` and the install stands. The source journal is not set
  aside. It is not unlinked either, because no durable replacement exists yet:
  its path and lease move to `busy_source` / `busy_source_lease`. Discovery
  stops, so later sources stay discoverable. `editor_checkpoint_owed` runs each
  step and retries under the refresh/retain gate (no save owning the journal,
  no staged ops). Any successful checkpoint calls `editor_recovery_owed_done`,
  which unlinks the source and its lock, fsyncs the directory and closes the
  lease. That is the tail `restore_source` runs after its own checkpoint.
  Retained generations the source named are left to the retirement sweep's
  orphan-BASE handling. `editor_flush` also takes the checkpoint branch while
  one is owed, so close and exit try to pay it.
- **Lazy restore** (restore.c `install_all`). On BUSY, `rollback` restores the
  placeholder and `drop_replacements` frees the unused buffers. The state
  becomes the new `RESTORE_RETRY`, which keeps the scanned program, the source
  and its lease. The service calls `prepare_all` again once the tick is due.
  The ops in the program are read-only, so a second prepare replays the same
  text. Rolling back keeps the edit-601 invariant: no record names a buffer
  whose BASE is not durable.
- **Save** (savectl.c worker). This worker has no later tick to come back on.
  `busy_wait` sleeps one `JOURNAL_DEFAULT_SYNC_NS` tick on the worker, never on
  the UI thread, then asks once more. That applies to prepare, cancel,
  recover-finish rotation and finish. A refusal that persists is reported like
  any other journal error. Before prepare runs again, the copy that the refused
  prepare retained is unlinked, because no checkpoint names it. The copy is
  removed only when its name differs from the token's path before the call.
- **Reload / Keep** (buffers.c). `reload_source` reports a refusal. An explicit
  R or K choice is re-armed (`source_choice`). The choice block and the
  automatic clean refresh wait for `editor_checkpoint_due`. A refused clean
  refresh is retried on the tick and the stale prompt is not shown.
- **Exit** (`editor_flush_settle`, editor.h). It runs `editor_flush`. On BUSY
  it sleeps one tick and flushes once more. `main.c`'s final flush and
  `editor_close` use it. `cli_test` now interposes `editor_flush_settle` where
  it used to interpose `editor_flush`.

## Numbers

(P) Zero is the unset deadline, no refusal and no descriptor (-1). One refusal
is within the bound, and the second consecutive refusal has outlasted it. The
`timespec` split is nanoseconds per second. (E) The test watchdogs reuse
`EDITOR_IDLE_NS`. The synchronous retries release the guard
`JOURNAL_DEFAULT_SYNC_NS / 2` after the refused attempt, between the attempt
and the retry. No new duration, count or allocation budget is introduced.

## Known limits

- During the recovery window, before the owed checkpoint lands, edits to
  recovered buffers append records whose BASE is not yet in the new journal. A
  crash inside that window makes the new journal fail replay, and it is set
  aside. The untouched source is still recovered.
- `editor_close` with a guard still held past the bound skips the owed
  checkpoint. The source then stays discoverable.
