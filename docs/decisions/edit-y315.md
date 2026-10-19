# edit-y315: the exit flush writes an owed Save As checkpoint on the worker

Bead edit-y315 (edit-9fzc item 3, deferred). Worker session s9, 2026-10-20.
Code: src/editor/editor.c (`editor_flush`, `close_result`), src/editor/session.c
(`editor_publish_wait`, deadline reset in `editor_publish_start`),
src/editor/open.c (`editor_close`), src/editor/private.h, src/editor/editor.h
(comment). Test: tests/dataloss_test.c `--y315` (`--y315-stall`), also in the
default run.

## Bug

`editor_flush` at exit wrote an owed Save As checkpoint (`publish_owed`) with
`editor_checkpoint` on the UI thread: `journal_rotate` (temp write, syncs,
rename, directory sync) ran there with no deadline. A stalled rotation held
shutdown until storage answered. A worker publication already running at exit
was waited for without a deadline too (`editor_flush`'s save loop and
`editor_session_close`). Red: quit while a Save As write was in flight with
the rotation's rename held: UI exit 10007.694 ms (the test watchdog,
EDITOR_IDLE_NS, released it), UI rotations=1; healthy storage: UI rotations=1.

## Choice

- The exit flush starts the same worker publication the session uses
  (edit-ze59 `editor_publish_start`) when the checkpoint is owed only for Save
  As (no journal error, no FULL/IO, no edit-ivv busy checkpoint). At that point
  `journal_flush` has returned 0, so the journal is durable and quiescent, the
  start's precondition. A refused start (stale leaf, no memory) keeps the
  former UI `editor_checkpoint`; FULL/IO/error/busy recovery is unchanged.
- `editor_publish_wait` (exit paths only) drains the main pool's mailbox and
  adopts the result via `editor_checkpoint_published`, waiting at most one
  journal tick from the first wait on that job. (E) The deadline reuses
  `JOURNAL_DEFAULT_SYNC_NS`, the tick `editor_flush_settle` (edit-ivv) and
  `close_result` (edit-dr9) already wait at exit; no new duration. It is
  stored per job (`publish_deadline_ns`, reset by `editor_publish_start`), so
  the later exit flushes on the same job (close_result, main's
  flush_settle, editor_close's) return at once: the UI waits one tick in total.
- Past the deadline `editor_flush` returns `EDITOR_MORE`. `close_result`
  treats it as closed (not `fail()`: the state is recoverable). A JOURNAL_BUSY
  result keeps its retry path. `editor_flush_settle` returns it; main reports
  a nonzero status with the journal path (unchanged main.c).
- `editor_close` leaves an overdue job to process exit: it closes, joins and
  frees nothing (journal, session graph, buffers, pools, `e`), and the
  session lock stays held until exit. The worker owns the journal and pins
  the graph, the buffers it encodes and the pool it runs on, so any teardown
  would either block on it or free memory under it. The journal file stays:
  the rotation is crash-safe, so recovery sees the old journal (the text,
  modified, at the former path: ze59's crash window) or the new one.

Not chosen: abandoning without any wait (a healthy rotation is (M) 7.9-21.2 ms,
ze59, well inside the tick), or a bounded `journal_flush` on the UI (journal
protocol change).

## Numbers

(G) The test bound is 2 * JOURNAL_DEFAULT_SYNC_NS: the deadline tick plus the
retry tick flush_settle already allows at exit. (E) The stall watchdog reuses
EDITOR_IDLE_NS. (P) Fixture lengths come from literals; counters count rename
callbacks (one per rotation). Green: UI exit 1002.088 ms (M) [Full, load1 6.76,
sanitized], worker rotations=1, UI rotations=0; recovery: 1 tab, text Asource,
modified, at the former path; target holds Asource.
