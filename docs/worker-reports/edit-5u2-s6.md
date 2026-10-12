# edit-5u2 worker report (s6, Opus)

Worktree `.wt/edit-5u2-s6`, branch `wt/edit-5u2-s6` from main f477c02. Not committed.
Usage at start: 5-hour 9.0 %, weekly 87.0 %. At end: 5-hour 10.0 %, weekly 87.0 %.

## Finding

The bead's failure (finish fails JOURNAL_INVALID after a complete checkpoint
during an evicted tab's save) does not reproduce on main: since edit-9u3 the
finish checkpoint comes from the session graph, whose retained leaf carries the
BASE. `checkpoint_during_evicted_save` (hold write, evict, `journal_flush` +
`editor_checkpoint`, release, finish, disk == "xbase") passed before the fix.
The actual defect: that `editor_checkpoint()` drops the evicted saving buffer
from CURRENT, so a crash before the rename loses the Ctrl+S text (disk still
"base", journal no longer has "xbase"). Control run: with the checkpoint call
removed, the same crash recovers "xbase" as a closed tab, and the test passes.

## Red (before fix), build/san/tests/editor_session_test

```
editor_session_test:446: FAIL found
editor_session_test:673: FAIL crash_after_evicted_checkpoint() == 0
editor_session_test: three launches leave one nonempty 0600 journal; changed base opens and sets the source aside
```

## Green (after fix)

```
editor_session_test: three launches leave one nonempty 0600 journal; changed base opens and sets the source aside
editor_session_test: headless save/repeated/new-target, recoverable failure retry, cutoff edits, shared targets, inactive failure, evicted saving tab, checkpoint during evicted save and crash after it, interrupted-save startup, Saving acknowledgement, allocation guard passed
```

## Fix

- `src/editor/session.c`, `private.h`: `editor_session_retains(e, b, state)`
  (true iff b is the in-flight saving buffer; optionally returns its leaf view).
- `src/editor/checkpoint.c`: complete checkpoints include that retired buffer as
  a closed tab with an empty BASE + TARGET + full text (cancellation's shape) and
  its leaf view. Rationale in docs/decisions/edit-5u2.md.
- `src/editor/buffers.c`: `editor_retire_buffers()` explicitly refuses to forget
  the saving buffer. No red possible: blocked implies can_recover_cancel today.

Not on the typing path (checkpoint already allocates; flush/reload/exit only).

## Verification

- `make all`: exit 0 (gcc, -Werror -Wshadow -Wconversion).
- Sanitizer, serially, DISPLAY=:99 EDIT_DISPLAY=:99, private XDG_RUNTIME_DIR
  /tmp/claude-1000/xdg-5u2, via tools/with_display_lock.sh: editor_session_test,
  editor_checkpoint_test, editor_test, savectl_test, journal_test,
  dataloss_test: all exit 0.

## Found, not fixed

- A *live* (non-evicted) saving buffer in the blocked needs_finish state still
  makes every `editor_checkpoint()` fail with JOURNAL_BASE_CHANGED (its
  `b->base` names the replaced target), so a reload of another tab silently
  rolls back until the save is reconciled. Found by reading, not tested. No
  data loss; not fixed (scope).
