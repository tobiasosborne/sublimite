# edit-qcuh: retired named buffers release slot, file, watch, mapping and arena

Finding: docs/reviews/recovery-3.md F3. `editor_retire_buffers` skipped every
retired buffer with `b->file != NULL`, so a named buffer evicted from the
closed stack was never destroyed until editor shutdown, and add_buffer ran out
of its live + closed slots.

## Choice: the review's smallest fix

- `src/file`: new `file_close_ready(file *)`, a nonblocking physical-retirement
  probe. While a save transaction is unresolved (`!save_installed`,
  commit/abort pending, or any of save/prepare/abort `_fin` clear) it returns
  0 and touches nothing. Otherwise it logically cancels the prefix/open/check
  jobs and returns 1 once every handle in `jobs[]` and every named handle is
  `work_handle_finished`. A finished handle is exactly what makes each
  `wait_job` in `file_close` return without sleeping (fin set, slot not busy,
  or slot epoch moved on), so the following `file_close` does not wait.
- `src/editor/buffers.c`: the `b->file` term leaves the lease guard; after all
  other leases (session, clip, savectl recoverable cancel, find/hl/map, warm,
  index) are gone, a named buffer waits for `file_close_ready` (and for not
  being `e->opening`) and is otherwise retried on a later maintenance turn.
  Destruction then goes through the existing `editor_session_forget`,
  `savectl_destroy` (BUSY while its write is active) and
  `editor_buffer_destroy` -> `file_close`.

Save jobs are deliberately not cancelled by the probe: maintenance must not
cut an in-flight file transaction; the buffer simply stays retired until it
resolves (same as before the fix for that case). Stale completions for the
closed file are dropped by the mailbox because `file_close` still cancels
(epoch-bumps) every `jobs[]` handle before freeing.

## Test consequence

`evicted_saving_tab` and `checkpoint_during_evicted_save` read the evicted
owner after its save resolved. The owner is now released then (that is the
fix), so they look the buffer up by id and assert it is gone instead of
dereferencing it; the old `needs_finish` read is implied by release
(destruction requires no session retention, no recoverable cancel and an idle
savectl).
