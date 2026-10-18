# edit-7i5 worker report (session s8, 2026-10-18)

Bead: edit-7i5 [BUG] P3. In a launch with no session journal, a lazy restore showed
the disk text instead of the restored edits. Decisions are in
`docs/decisions/edit-7i5.md`.

## Files touched

- `src/editor/buffers.c`: in `editor_buffer_prepare`, `origin` is now also bound
  while a lazy restore prepares. The condition changed from `e->recovering` to
  `e->recovering || e->restore_prepare`, with a comment.
- `tests/editor_session_test.c`: `e601_no_journal` (case (a)) now asserts:
  - length == BASE + 1;
  - the byte at offset 0 is 'x';
  - the tab is modified.

  It then relaunches with a journal and asserts:
  - the kept source restores once (BASE + 1, 'x', modified);
  - the source retires;
  - the new journal is consistent (every named id has a BASE).
- `docs/decisions/edit-7i5.md`, this report.

## Red run (test only, pre-fix buffers.c)

build/san/tests/editor_session_test, ASAN_OPTIONS=detect_leaks=1, on :99 under the display lock.
Box: Not charging (AC), load average 1.98 (11:34). df -h /: 62G available before and after.
No build/editor-601-* fixture was left behind.

```
edit-601 (a): no launch journal rc=0 restored_tab=yes len=1073741824 first=0x00 modified=no source_kept=yes notice=large session restored; its journal is kept (this launch has no session journal)
editor_session_test:1267: FAIL rc == 0 && live && kept && shown
editor_session_test:1787: FAIL e601_restore_failures() == 0
exit=1
```

## Green run

Same binary rebuilt with the fix, same command.
Box: Charging (AC), load average 3.13 (11:36). df -h /: 62G available before and after.
No build/editor-* fixture was left behind.

```
edit-601 (a): no launch journal rc=0 restored_tab=yes len=1073741825 first=0x78 modified=yes source_kept=yes notice=large session restored; its journal is kept (this launch has no session journal)
edit-7i5: relaunch with journal rc=0 len=1073741825 first=0x78 source_retired=yes
edit-601 (b): checkpoint failure rc=0 settled=yes placeholder_fallback=yes source_kept=yes notice=...
edit-601 (c): scan NOMEM rc=0 settled=yes source_kept=yes notice=...
edit-601 (d): duplicate TABS ids rc=0 text=yes orphan_retired=yes
editor_session_test: headless save/... allocation guard passed
exit=0
```

1073741825 = (P) 2^30 + 1. The large_restore conflict case (qct_conflict) is in
the same run and still passes, so a real BASE change is still treated as a
conflict.

## Verification

Box: Charging (AC), load average 4.76 (11:37). df -h /: 62G available before, 61G after.
Other workers share the disk. No fixture was left behind.

| command | exit |
|---|---|
| `make -j3 all` (gcc release, -Werror) | 0 |
| build/san/tests/editor_session_test (leaks on, :99, lock, EDIT_DISPLAY_LOCK_TIMEOUT=1800) | 0 |
| build/san/tests/editor_checkpoint_test (leaks on, :99, lock) | 0 |
| build/san/tests/dataloss_test (leaks on) | 0 |
| build/tests/editor_test (release, :99, lock): 10 `mallocs=0` guard lines, none nonzero | 0 |

## What remains

Nothing in scope.

## Out of scope (reported, not fixed)

- dataloss_test prints `stranded_orphan leftover session-XXXX 8192 bytes`. This is an
  existing diagnostic in `stranded_orphan` (tests/dataloss_test.c:652), and the
  test passes. It is unrelated to this change.
- If the user saves the restored buffer in a launch with no journal, the kept
  source conflicts on the next journalled launch and is set aside. The saved file
  holds the edits, so nothing is lost. The behaviour is unchanged (decision doc,
  known limits).
