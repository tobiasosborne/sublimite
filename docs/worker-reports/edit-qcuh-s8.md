# edit-qcuh (s8): retired named buffers never released (recovery-3 F3)

## Files touched
- src/file/file.h, src/file/file.c: `file_close_ready` (nonblocking "file jobs physically finished" probe).
- src/editor/buffers.c: `editor_retire_buffers` drops the unconditional `b->file` guard; waits on `file_close_ready` after the other leases.
- tests/editor_session_test.c: new `retired_named_release(copy|mmap)` (also `--edit-qcuh`); `buffer_by_id` helper; `evicted_saving_tab` and `checkpoint_during_evicted_save` no longer dereference the evicted owner after its save resolved (it is now freed).
- docs/decisions/edit-qcuh.md.

## Red (before the buffers.c change)
Test: tab_capacity 2, closed_capacity 1 (slots = 3, (P) config), open + close 4 x 3 = 12 distinct named files; assert every add succeeds, then slots == live + closed and /proc/self/fd count <= the count at round 2.
```
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/editor_session_test --edit-qcuh
editor_session_test: retired_named_release(copy) round 2 add rc -105 slots 3/3
editor_session_test:369: FAIL rc == 0
exit 1
```
(Red stops at the slot assertion, EDITOR_ERR_CAPACITY on the third open; the fd assertion is not reached in the red run.)

## Green
```
editor_session_test: retired_named_release(copy) slots 2 (live 1 + closed 1) fds 17 baseline 17
editor_session_test: retired_named_release(mmap) slots 2 (live 1 + closed 1) fds 19 baseline 19
```
First full-suite run after the fix hit a heap-use-after-free in `evicted_saving_tab` (test read `owner->save_error` after the save resolved and the buffer was, correctly, released); tests adjusted as above, then green.

## Verification (Charging, load1 3.4-16 during runs; df -h / 56G avail before and after, unchanged)
- `make -j3 all` (gcc -Werror): exit 0 (after fix and after test edit).
- ASan/UBSan, detect_leaks=1, DISPLAY=:97 under tools/with_display_lock.sh, EDIT_DISPLAY_LOCK_TIMEOUT=1800, private XDG_RUNTIME_DIR:
  - build/san/tests/editor_session_test: exit 0
  - build/san/tests/editor_checkpoint_test: exit 0
  - build/san/tests/journal_test: exit 0
  - build/san/tests/file_test: exit 0
  - build/san/tests/dataloss_test: exit 0
  - build/san/tests/editor_test: exit 0
- Release build/tests/editor_test: exit 0, malloc guard active, mallocs=0 for null / hl-on / raster 10000 keys.

## Remaining / notes
- No direct file_test unit for `file_close_ready`; it is covered through the editor suite (COPY and MMAP acquisitions).
- A retired buffer whose file save transaction never resolves (result message lost) still stays resident, as before; not reachable in the tested paths.

## Out of scope (reported, not fixed)
- `dataloss_test` prints `stranded_orphan leftover session-* 8192 bytes` and still passes; pre-existing output, not investigated.
- Display guard prints "real display :0 refused, using :99" when run directly without the lock wrapper (it falls back to :99 even with the null backend).
