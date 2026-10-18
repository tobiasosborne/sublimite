# edit-9ev6: quiesce BASE retention before the journal closes

Bead edit-9ev6 (review recovery-3 F2, BLOCKER). Worker session s8,
2026-10-18. Code: src/editor/retain.c (`editor_retain_quiesce`),
src/editor/open.c (`editor_close`), src/editor/private.h. Test:
tests/dataloss_test.c `retain_close` (`--retain-close`, also in the default run).

## Bug

An edit-fi2 retention job borrows the journal's IO callbacks
(`journal_retain.io = &j->io`) and runs on the journal pool. `editor_close`
freed the journal (its arena is munmapped) and only then shut down the journal
pool; buffer retention was stopped last, in `editor_buffer_destroy`.
`journal_flush` does not join retention: it returns at once when the journal
prefix is already durable. A copy still running at close then reached
`metadata_sync`/`sync_parent`/`write_at` through the unmapped `j->io`.

The journal pool has one worker (`work_pool_init(&e->journal_pool, 1, 0)`), so
when the journal still has non-durable bytes at close, the close flush queues
behind the copy and in effect waits for it; the crash needs a durable prefix
at close, which is the finding's sequence (and the test's).

## Choice: the review's smallest fix

`editor_close` calls `editor_retain_quiesce(e)` after `editor_session_close`
(save workers finished, their checkpoints done) and before the journal is
closed, its lease released and its pool shut down. `editor_retain_quiesce`
runs the existing `editor_retain_stop` on every buffer, so the
referenced-copy preservation policy is unchanged: a RUNNING copy is cancelled,
joined (`work_handle_finished`) and removed if it completed (no checkpoint
names it); a READY copy is kept only if the durable journal references it and
BASE has not changed since; every other copy is removed. The later
`editor_buffer_destroy` finds IDLE retention and does nothing, which also
removes its former `work_handle_finished` call on the already shut-down
journal pool.

Placed after the close-time `editor_flush`: that flush may write a FULL/IO
checkpoint that references READY copies and marks them used; stopping
retention before it would drop copies that checkpoint is about to name.

Not chosen: making `journal_flush`/`journal_close` join retention. The journal
does not track asynchronous retention jobs (only the synchronous save
retention, `j->retention`), and the job's result (copy kept or removed)
belongs to the editor's policy in retain.c.

## Red test

`retain_close`: a metadata_sync hook (used only by retention) holds the
first retention job after its copy until the job's slot epoch changes (a
cancel or pool shutdown). The edit is made with `retain_limit = 0` and flushed
(`editor_flush`) so the prefix is durable, then retention is allowed and
started; the test closes the editor with the job held. Red: ASan SEGV on the
unmapped journal in `io_sync` <- `sync_parent` <- `retain_generation` (worker
thread). Green: close cancels and joins the job; no `.sublimite-base-*` is left.
