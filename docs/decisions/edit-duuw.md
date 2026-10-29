# edit-duuw: one owner for "a journal job is in flight"

## Choice

The editor no longer keeps `journal_waiting`. The journal answers the
question itself: `journal_in_flight(j)` (src/journal/journal.h) is true from
a successful `journal_pump` submission until that batch's completion is
taken, by `journal_receive` or by the drain inside a blocking
`journal_flush` / `journal_rotate` / `journal_close`. The editor derives its
gate from it on every call:

    editor_journal_waiting(e) = e->journal && (editor_save_leased(e) || journal_in_flight(e->journal))

All the old readers (editor_step pump admission, wait_timeout's journal
deadline, the file-command gate, retain tick, save start/finish) call that
helper.

## Why not "clear the flag after every flush"

The brief prefers deriving; this was its acceptable alternative. Clearing at each caller would touch every
UI-thread flush or rotate today (buffers.c: protected_record, the
insert_step loop, tab replacement (reload), source Keep; restore.c; checkpoint.c's flush
and rotate; editor_close) and every future `journal_flush` caller would
have to remember it; the bug was exactly one forgotten site. The journal
already holds the true state (`j->active`), so a query costs one load and
cannot drift.

## The lease case

While a save or publish worker leases the journal, the UI must not read
journal state (the worker runs flush/rotate on it). The helper then reports
"waiting" without touching the journal. The gates that read it already
exclude leased states through `editor_save_active`, `e->publish` or
`model.busy`, so behaviour under a lease is unchanged; pump admission and
the wait deadline kept their explicit `!editor_save_leased` tests.

## runnable() and the file command

`editor_file_command_ready(e)` (commands.c) is the whole gate of
`editor_file_command_service`; `runnable()` calls the same function, so a
command refused by the service can no longer make `wait_timeout` return 0.
Each gate condition has its own wake: journal completion, worker op and save
results arrive through mailboxes; activation work, dirty frames and the
render backend are runnable clauses of their own; `publish_owed` is paid by
the retain tick, which runs after the journal becomes durable (mailbox) or at
`busy_due_ns` (a wait_timeout deadline).
