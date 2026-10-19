# edit-xdro: a deferred Reload/Keep choice is not runnable work

`editor_check_sources` (buffers.c) runs a pending `source_choice` only when no
save is active and the edit-ivv journal tick is due:
`!e->source_choice || editor_save_active(e) || !editor_checkpoint_due(e)`.
`runnable()` (editor.c) counted any pending choice as runnable. While the choice
waited, `wait_timeout` returned 0, `editor_step` returned `EDITOR_MORE`, and the
UI loop polled with a zero timeout for the whole wait: the length of the save,
or up to one `JOURNAL_DEFAULT_SYNC_NS` tick after a refused checkpoint.

## Choice

`runnable()` now uses the same gate as the consumer:
`e->source_choice && !editor_save_active(e) && editor_checkpoint_due(e)`.
No new wake source is added. Both existing wakes already cover the deferred
cases:

- **Save in flight.** The save worker's completion arrives as a mailbox message.
  The mailbox is in the poll set, so `pump` wakes, `editor_poll_sources` drains
  it (`editor_save_tick`), and `editor_check_sources` runs the choice in the
  same call. If the save clears later in the step, `runnable()` is true at the
  end of that step, so the step returns `MORE` and the next poll does not sleep.
- **Journal tick.** `wait_timeout` already sleeps until a future `busy_due_ns`
  (edit-ivv). Once the tick is due, the gate is open and the choice runs on
  that step.

A choice whose buffer or generation no longer matches still counts as runnable,
as before. `editor_check_sources` drops it on the next step.

The gate is shared with the consumer and not copied into a helper. It is one
expression in each place, and the e3z5 change to `editor_reload_source` does not
touch either one.

## Numbers

(P) `EDITOR_MORE` means runnable work (editor.h). A deferred choice is none, so
the test allows zero `MORE` returns while it waits. (E) The test reuses
edit-ivv's values. The save is released `JOURNAL_DEFAULT_SYNC_NS / 2` after it
is held. The tick is `JOURNAL_DEFAULT_SYNC_NS`. The watchdog and step timeout
are `EDITOR_IDLE_NS`. No new duration, count or budget is introduced.
