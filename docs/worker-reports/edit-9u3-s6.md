# edit-9u3 (s6): evicting the saving tab removed the leaf that save finish dereferences

Worker: Claude Opus 5.5. Worktree .wt/edit-9u3-s6 (branch wt/edit-9u3-s6 from 88d0ef3).
Usage at start: 5-hour 25.0 %, 7-day 82.0 %. At end: 5-hour 29.0 %, 7-day 83.0 %.
Power: BAT0 "Not charging" for every run (no timings are claimed).

## Change

- `src/editor/session.c` `editor_session_metadata()`: a retired (evicted) buffer that
  is `e->session->saving` keeps its leaf. If the leaf is still live, it is republished
  as closed (`live=false`), so the TABS ranks and `live_count` match the real tabs.
  Other retired buffers are forgotten as before. When the save completes and
  `saving` clears, the next metadata pass forgets the leaf.
- `capture()`: a NULL or mismatched leaf now gives a NULL request snapshot, which
  savectl refuses. It no longer dereferences.
- `tests/editor_session_test.c` `evicted_saving_tab()`: closed capacity 1, three tabs.
  Type into the file tab, hold the write at FILE_STEP_TEMP_WRITTEN after the hand-off
  (`journal_leased` false), close the saving tab (it becomes closed[0]), close one
  more tab (the saving tab is evicted and `owner->retired`), then release. Asserts:
  disk is "xbase", no save, editor or journal error, and editing still works.

Design: docs/decisions/edit-9u3.md. I tried the alternative first: finish tolerates a
forgotten leaf, using a snapshot from the tree and a NULL-safe prepare. It does not
work. `journal_save_finish()` returns JOURNAL_INVALID unless the finish checkpoint
carries the saving buffer's BASE, so the save stayed stuck needing a finish
(observed: state=3, jerr=2, needs_finish=1). Retaining the leaf is required.

## Red (before the fix; same test)

```
src/editor/session.c:302:70: runtime error: member access within null pointer of type 'session_node' (aka 'struct session_node')
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior src/editor/session.c:302:70
==309293==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000038 ...
    #0 capture src/editor/session.c:302:70
    #1 editor_save_tick src/editor/session.c:366:9
    #2 editor_poll_sources src/editor/buffers.c:678:5
    ...
    #7 evicted_saving_tab tests/editor_session_test.c:202:40
SUMMARY: AddressSanitizer: SEGV src/editor/session.c:302:70 in capture
```

## Green (serial, display suites through tools/with_display_lock.sh, DISPLAY=:99)

```
make all exit 0
RESULT editor_session_test exit 0
editor_session_test: headless save/repeated/new-target, cutoff edits, shared targets, inactive failure, evicted saving tab, interrupted-save startup, Saving acknowledgement, allocation guard passed
RESULT editor_test exit 0      editor_test: all passed
RESULT editor_p4_test exit 0   editor_p4_test: all passed
RESULT savectl_test exit 0     savectl_test: ok
```

## Found, not fixed (outside this bead)

- `editor_checkpoint()` (checkpoint.c) skips every retired buffer, including a retired
  saving buffer with an in-flight token. journal.h requires complete checkpoints to
  retain every in-flight token's BASE. A full checkpoint that runs during such a save
  could break a later finish in the same way.
- `editor_save_active()` is false while blocked, so `editor_retire_buffers()` can reach
  `editor_session_forget()` for a blocked saving buffer. Today `b->file` and
  `can_recover_cancel` keep it alive (see finding 15).
