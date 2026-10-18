# edit-7i5: a lazy restore without a launch journal shows the restored edits

Bead: edit-7i5 [BUG] P3. Worker session s8, 2026-10-18. Code: `src/editor/buffers.c`
(one condition). Test: `tests/editor_session_test.c` `e601_no_journal` (case (a)
extended, plus a relaunch check).

## Cause

`editor_buffer_prepare` set `b->origin` (the opened generation's BASE id) only
when the launch had a session journal (`e->journal`) or startup recovery was
running (`e->recovering`). In a launch with no session journal, the lazy restore
(edit-qct) opened its replacement with `origin` all zero. `editor_restore_apply`
then failed its edit-btj check (`origin` must equal the journalled BASE). It marked
the buffer as a conflict, so the tab showed the disk text and was clean. Case (a)
of edit-601 measured this: the restored length was 1073741824, the BASE length,
and not BASE + 1 (M).

## Choice

1. **Bind `origin` while a lazy restore prepares.** The condition is now
   `e->recovering || e->restore_prepare`. `e->restore_prepare` is non-NULL only
   inside `prepare_all`, and only for a buffer that is not already a conflict.
   The id comes from the same validated prefix and identity that
   `file_attach` installs as ORIGINAL. That is exactly the edit-f3i
   startup-recovery binding. The kept source's BASE is therefore checked against
   the generation that was actually opened, whether or not a launch journal
   exists. A real BASE change is still caught twice: by `journal_check_base`
   on the scan worker, and by this comparison.
2. **No other reader is affected.** Outside restore, `origin` is read only by the
   checkpoint encoder (`editor_checkpoint_references`, `checkpoint.c`), and that
   runs only with a journal. When a journal exists, the earlier branch already
   set `origin`. So the change only matters when `e->journal == NULL`.
3. **The source is still the origin until a launch journal exists.** edit-601
   keeps the source (and its lock name) when the launch has no journal, and
   nothing else records the edits. The next launch that has a journal therefore
   finds the source and applies it once, onto the unchanged BASE. The test checks
   this: the relaunch restores BASE + 1 (not BASE + 2), the source retires, and
   the new journal names no id without a BASE.

## Numbers

No new bound, count or duration. Test constants: the 1 GiB sparse BASE ((P)
2^30, FM4_GIB) and its one typed byte 'x' at offset 0 (qct_orphan presses it at
the opening cursor). The expected length is therefore (P) 2^30 + 1 = 1073741825.

## Known limits

- In the no-journal launch, saving the restored buffer rewrites the BASE. The kept
  source then conflicts on the next journalled launch, and edit-btj sets it aside
  with the "file changed on disk" notice. The saved file already holds the edits,
  so nothing is lost. This behaviour is unchanged and outside this bead.
