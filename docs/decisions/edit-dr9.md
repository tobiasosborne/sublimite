# edit-dr9: preserve failed rotation checkpoints and defer directory contention

A rotation that begins with either the worker-failed or append-failed state
can contain accepted edits absent from the original inode. Its temporary
basename carries `!` immediately after `.sublimite-journal-`, before `from-`
or the historical fallback spelling. A clean start carries `+` in that same
character position. Both flags are outside the hexadecimal alphabet. The
journal bytes and the rest of the basename grammar stay the same. Older
unflagged names cannot prove that their original was clean and are set aside.

Retirement parses the flag and only unlinks an identified matching original's
temp when the explicit clean flag is present. A failed or unflagged dead temp follows the existing
set-aside path even when the original generation matches. Inventory still
excludes the entire reserved namespace from content scanning and retention
caps, and preserves any potentially referenced BASE artifacts. Clean dead
temps keep the existing metadata-only sweep behavior.

The shared directory flock uses `LOCK_NB`. Retry `EINTR`; map `EAGAIN` and
`EWOULDBLOCK` to `JOURNAL_BUSY`, other failures to `JOURNAL_IO`. Acquire this
guard before creating a temp, preserving the existing shared/exclusive lease
protocol. A refusal changes neither the original journal nor its state.

In this tree the checkpoint call in `editor.c` belongs to flush fallback,
including close. A busy close checkpoint retains the close request, editor
text, queued operations, and open journal; it returns a continuation instead
of calling `fail()`. The next attempt uses the existing journal sync interval
as its tick. The editor's normal poll deadline waits for that tick, avoiding
a checkpoint retry spin. Explicit flush callers receive `JOURNAL_BUSY` and
can retry. Successful close or a different error clears the retry deadline.
The restore transaction and startup deferral policy are outside this bead.

Numeric derivations: (P) zero is the unset deadline/success/empty sentinel;
one is a sole worker, buffer, record, or pipe message; pipe endpoints are the
POSIX read/write pair. Payload sizes come from the offset's `uint64_t` and
literal bytes; paths come from the public schema. (P) nanoseconds divided by
the nanoseconds per millisecond convert the existing sync interval for poll.
(E) The lock regression's watchdog reuses `JOURNAL_DEFAULT_SYNC_NS`, rather
than inventing a response budget. The editor retry regression reuses
`EDITOR_IDLE_NS` as its existing settle watchdog. Case ordinals enumerate
clean, missing-original, append-failed, worker-failed, and older unflagged
failed-checkpoint protocol states.
No new retention, duration, retry-count, or allocation budget is introduced.
