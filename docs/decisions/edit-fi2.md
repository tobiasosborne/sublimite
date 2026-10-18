# edit-fi2: recovery survives an external rewrite of the original

Bead edit-fi2 (review editor-2 #2, PRD §3.5/§7). Worker session s8,
2026-10-18. Code: src/journal/journal.{h,c} (async retention),
src/editor/retain.c (new), checkpoint.c, session.c, recovery.c, buffers.c,
editor.c, open.c, private.h. Tests: tests/dataloss_test.c (fi2 cases),
fixture adjustments in editor_checkpoint_test.c, editor_test.c,
editor_session_test.c.

## Bug

The journal names BASE by identity only. Before any checkpoint after the
first edit (all sizes), and in every edit-28u differences checkpoint (BASE
> FILE_PREFIX_MAX), the INSERT/DELETE records need BASE's bytes. A rewrite of
the file by another process (git checkout, a build step) followed by a crash
made relaunch set the journal aside (edit-btj): the inserted bytes survived,
the kept original text did not. Red run (2 MiB + 7 B fixture, "XY" typed):
both rename-over and in-place rewrite came back as the rewritten disk text,
clean, notice "...its unsaved edits were set aside".

## Choice: mechanism (2), asynchronous retention, bounded by size

On the first modification of a named buffer, a WORK_BULK job on the journal
pool reflinks (FICLONE) or copies BASE to `.sublimite-base-XXXXXX` beside the
journal, with the save path's existing validation (`retain_generation`:
identity before/after, prefix CRC of the copy, full fsync of the copy and its
directory). The UI does no I/O for it: the job creates the temp itself
(`journal_retain_start` only forms the name and submits), and the UI polls
`work_handle_finished` in source maintenance (`editor_retain_tick`, called
from `editor_poll_sources` before `editor_check_sources`). The job's
JOURNAL_RETAIN_MESSAGE is only a wake-up. Once the copy is READY, the next
complete checkpoint writes BASE = the copy, TARGET = the real path, and the
text as its edit-28u/f3i differences from BASE (the copy holds BASE's bytes,
so ORIGINAL spans / saved-revision spans stay references; against an
immutable copy, small texts reference it too). Recovery already replays a
retained BASE plus TARGET (the interrupted-save path).

Why (2) and not (1) (keep the mapping alive, write a self-contained checkpoint
when the change is seen):
- (1) cannot handle an in-place truncate+write (the mapping sees the new
  bytes or SIGBUS; a COPY-mode original survives only in memory); the brief's
  test (b) needs (2).
- (1)'s self-contained checkpoint for a large file is O(file size) and must be
  written off the UI thread; the only off-thread checkpoint writer today is
  the save worker (session.c/savectl), bound to a save request. A generic
  worker checkpoint is a separate piece of work.
- (2) reuses the save path's retention code and recovery's retained-BASE
  path, covers rename-over and in-place rewrites alike, and costs one copy per
  modified buffer, made once, off the UI thread.
(1) is therefore not implemented; it remains the way to protect BASEs above
the bound (see Remaining).

No RETAIN_PENDING record: the journal names the copy only after it is
complete and durable, so a crash mid-copy leaves an unreferenced temp, which
startup retirement (edit-rfq) already treats as an orphan `.sublimite-base-*`.
A new record type would also break rollback to an older build (28u decision
2: unknown types truncate the journal).

## Bound and disk cost

`e->retain_limit = JOURNAL_STARTUP_SCAN_BYTES` (105,000,000 B, the existing
fm4 bound, (G) G4a x (M) page-cache read rate). Reason: a journal whose BASE
is a retained copy is recovered by the materialised path, whose cost scan
charges the whole BASE against that same bound (no mapped replay when TARGET
differs from BASE); a larger retained BASE would be deferred to a closed tab
at startup anyway. Copy cost at and above the bound (all (M), 64 KiB
pread/pwrite + fsync like `retain_copy`):

| measurement | result | stamp |
|---|---|---|
| standalone copy, 256 MiB page-cached | 723-924 ms (290-371 MB/s) | AC "Not charging", load1 25.4 |
| standalone copy, 105,000,000 B | 268-425 ms (247-392 MB/s) | same |
| in editor, sparse 105,000,000 B BASE, first edit -> copy READY | 104.4 / 162.1 / 127.4 ms | AC "Not charging", load1 9.9 (release) |
| same, longest UI turn (includes the reference checkpoint) | 7.9 / 8.5 / 21.2 ms | same |

(P) a 1 GiB BASE would cost 2^30 B / (247..392 MB/s) = 2.7-4.3 s of journal
worker time and 1.07 GB of disk per first edit: above the bound nothing is
retained (test (d) asserts no copy for 1 GiB).

Disk cost (P): at most base.size per modified named buffer whose BASE is <=
the bound and is still referenced by the checkpoints (28u original or f3i
saved revision); a recovered whole-text buffer needs none. The copy is
removed: after a durable checkpoint that does not reference it (text equal to
BASE again, buffer reloaded/retired); once BASE is replaced (save, Save As,
Keep: their checkpoints rotate without it before BASE changes) and no save is
in flight; at teardown if never referenced or BASE was replaced; by recovery
after the recovered checkpoint is durable (existing `restore_source`); by
startup retirement when unreferenced (existing cap rule). It is kept on a hot
exit with unsaved edits: the journal names it.

## UI-thread cost (deviation to note)

The copy never touches the UI thread. Referencing it does: the tick runs
`journal_flush` + `editor_checkpoint` (a complete rotation with fsync) under
edit-ret's refresh gate (no queued input, no frame in flight, no staged
journal ops, no save owning the journal); measured 7.9-21.2 ms (above). This
is the same mechanism and cost class as the checkpoint every open, reload and
Keep already runs on the UI thread, but it is a wait on I/O. Not the typing
path (release editor_test malloc guard: mallocs=0). A failed attempt retries
after the next mutation, not every tick. Follow-up if this shows up in use:
append BASE(copy) + TARGET + differences as one journal group instead of
rotating (no fsync on the UI; needs batch-capacity fallback).

## Recovery

- BASE = copy, TARGET = file. If the file still holds the copy's bytes
  (chunked compare, startup only, within the scan bound), the logged ops are
  applied onto the opened file (f3i install, ORIGINAL spans kept). Otherwise
  the whole recovered text replaces it: a modified tab against the changed
  disk file, notice "file changed on disk since this session; its unsaved
  edits were recovered". The copy is unlinked after the recovered checkpoint
  is durable.
- No copy referenced yet (crash before the copy finished, BASE above the
  bound): unchanged edit-btj set-aside.

## Session worker (session.c)

Published nodes carry the READY copy; worker checkpoints write it like the UI
does, except for the buffer being prepared (journal_save_prepare checks and
substitutes its own previous BASE) and replaced buffers (new BASE). The worker
re-checks the copy (`journal_check_base`) and falls back to the real BASE if
the UI has since removed it.

## Test adjustments

Two existing tests pin the edit-btj set-aside after a referencing checkpoint
(dataloss changed_base, editor_checkpoint (c)); their children set
`e->retain_limit = 0` so they keep covering the fallback deterministically.
dataloss shared_save_as waits for retention to settle before
`journal_set_io` (its contract: no active retention operation; the rename hook
otherwise fired on the reference checkpoint). editor_test o20/ck4 and
editor_session_test fixtures remove the hot-exit copy before rmdir.

## Remaining

- BASEs above the bound (105 MB) stay unprotected against an external
  rewrite + crash: mechanism (1) (self-contained checkpoint from the live
  mapping after a rename-over, written off the UI thread) is not done.
- Window: edits made before the copy is READY and referenced (measured up to
  162 ms at the bound) still depend on the original.
- A failed or cancelled save rotates its cancel checkpoint without the copy
  for the saved buffer while its state stays "referenced"; protection returns
  at the next checkpoint (open, Keep, reload, exit with FULL/IO) or modification
  after BASE changes. Trial-irrelevant edge, not handled.
- A crash mid-copy leaves a partial unreferenced temp; retirement removes
  unreferenced copies only above the artifact cap (existing edit-rfq rule).
- The external rewrite *deleting* the file (dataloss --probe changed_base
  deleted) is unchanged.
