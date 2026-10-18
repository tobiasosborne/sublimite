# edit-fkpk (s8) worker report: recovery-3 F5, second save of an MMAP original

## Result
Verified and fixed. Shape the existing case missed: `repeated_save` uses a
4-byte file under the default copy threshold, so it is COPY; the bug needs
FILE_MODE_MMAP (forced here with copy_threshold 1, no GB fixture needed).

## Files touched
- tests/editor_session_test.c: `open_fixture_threshold` (open_fixture_at now
  delegates with threshold 0), new `mapped_repeated_save(journalling)` (open as
  MMAP, edit/save three times, assert disk bytes and clean tab), wired into
  main and a focused `--edit-fkpk` option.
- src/file/file.h, src/file/file.c: `file_source_adopt_retired`.
- src/savectl/savectl.c: `save_replaced` takes the controller, refreshes the
  owned source right after the rename.
- docs/decisions/edit-fkpk.md.

## Red (before the fix)
    $ DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_session_test --edit-fkpk
    editor_session_test: fkpk second save error=5 journal=1
    editor_session_test:139: FAIL !f.e->buffer->save_error
    editor_session_test: fkpk second save error=5 journal=0
    editor_session_test:139: FAIL !f.e->buffer->save_error
    exit=1
(5 = FILE_ERR_CHANGED; first save succeeded, disk held "xbase".)

## Green (after the fix)
    $ DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_session_test --edit-fkpk
    exit=0

## Verification (power Charging, load1 23-70 during runs; no timing claims)
- make -j3 all (gcc release, -Werror): exit 0
- sanitizer, ASAN_OPTIONS=detect_leaks=1:
  - build/san/tests/file_test: exit 0
  - build/san/tests/savectl_test: exit 0
  - build/san/tests/journal_test: exit 0
  - build/san/tests/dataloss_test: exit 0
  - build/san/tests/editor_checkpoint_test (:99, display lock, timeout 1800): exit 0
  - build/san/tests/editor_test (:99): exit 0
  - build/san/tests/editor_session_test (:99): first full run exit 1 at
    `existing_journal` line 527 `rmdir(root)` (a leftover 4-byte
    `.sublimite-base-*` retained copy; COPY path, untouched by this change,
    load1 ~57-70); rerun exit 0.
- release build/tests/editor_test (:99, malloc guard active): exit 0
  ("mallocs=0", "malloc guard passed"); release editor_session_test exit 0.
- No GB fixture used; df -h / 55G free before, 54G after (other trees active).

## Out of scope (reported, not fixed)
1. Flake: `editor_session_test` `existing_journal` can leave a
   `.sublimite-base-*` retained copy in its fixture directory under heavy
   load, failing `rmdir(root)`. Looks like fi2 async retention finishing after
   save/close (adjacent to recovery-3 F2/F10); passed on rerun.
2. Unverified sibling of F5: after an external rename-over of an MMAP original
   followed by Keep, `file_resolve_keep` tolerates the retired inode's new
   ctime but the savectl's `file_source` is not refreshed, so the save after
   Keep is likely refused with FILE_ERR_CHANGED (review coverage table names
   "after Keep"). This fix covers only the editor's own replacement.
