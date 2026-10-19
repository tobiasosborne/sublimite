# edit-dxas: the target watch waits for the controller's own save

## Cause, verified
After Save As the buffer is `target_rebound` and its target is watched by a
path watch (`b->target_watch`). A plain Ctrl+S runs the rebound controller's
save job on a worker; the job renames the replacement over the target. The
inotify event for that rename is queued at the rename syscall, while the
controller's new baseline (`savectl_target_identity`, set in `savectl_tick`
from the job's result) arrives later through the work mailbox. The watch
block in `editor_poll_sources` (src/editor/buffers.c) took the event in the
turn it arrived, stat'ed the target, compared it with the *old* baseline,
and set `operation_kind = FILE_MSG_OPEN_FAILED` / `FILE_ERR_CHANGED`. Once the
save finished the buffer was clean, so `editor_check_sources` took the clean
refresh path (`reload_source`): a new editor_buffer with empty undo history
replaced the old one. Ctrl+Z then undid nothing.

Red: `tests/editor_session_test.c` `--edit-dxas` (Save As, type, Ctrl+S,
Ctrl+Z) reproduced in all eight variants: journal on/off x COPY/MMAP x new
or existing target (`replaced=yes undone=no`).

## Fix
Do not take the target watch while the buffer's own save controller has a
job in flight (`savectl_get_model(b->save).busy`). The event stays pending;
the first turn after the result is adopted takes it and compares the target
with the baseline the job wrote. One condition, no new state.

## Why this, not ignoring the event
- An external writer during the save is still detected: after the result,
  the baseline is our write's identity, so any later rewrite differs; a
  rewrite before our rename fails the job's own validation (FILE_ERR_CHANGED).
- No identity is relaxed (unlike edit-fkpk, which had to adopt a ctime on a
  retired inode; here the baseline is exact once the result is in).
- No I/O or allocation added; the check reads controller state on the UI
  thread, which owns it.

## Numbers
None introduced. Test variant counts derive from the boolean choices
(journal, MMAP, existing target); the MMAP threshold is `sizeof ""`, the
smallest nonzero value ((P), as edit-ut0m).
