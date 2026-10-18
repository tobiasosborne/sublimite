# edit-9ev6 (recovery-3 F2) worker report, session s8, 2026-10-18

## Files touched
- src/editor/retain.c: new `editor_retain_quiesce(editor *e)` (runs the existing
  `editor_retain_stop` on every buffer and `e->empty`).
- src/editor/private.h: its declaration.
- src/editor/open.c: `editor_close` calls it after `editor_session_close`, before
  the journal close, the lease release and `work_pool_shutdown(&e->journal_pool)`.
- tests/dataloss_test.c: `retain_close` (selector `--retain-close`; also in the
  default run after `retain_lifecycle`).
- docs/decisions/edit-9ev6.md.

## Red run (before the fix)
`ASAN_OPTIONS=detect_leaks=1 build/san/tests/dataloss_test --retain-close` -> exit 1
```
==2567824==ERROR: AddressSanitizer: SEGV on unknown address 0x77a0c63f5658 (pc ... T3)
==2567824==The signal is caused by a READ memory access.
    #0 io_sync src/journal/journal.c:204:19
    #1 sync_parent src/journal/journal.c:463:12
    #2 retain_generation src/journal/journal.c:1034:16
    #3 retain_async_worker src/journal/journal.c:1086:18
    #4 worker_main src/work/work.c:82:13
SUMMARY: AddressSanitizer: SEGV src/journal/journal.c:204:19 in io_sync
```
(The journal lives in an arena that `journal_close` munmaps, so ASan reports a
SEGV on the unmapped page, not a heap-use-after-free.) Test design: a
metadata_sync hook (journal_io, used only by retention) holds the first copy
after its data is written until its work slot's epoch changes (cancel or pool
shutdown). The edit is made with `retain_limit = 0` and made durable with
`editor_flush` first: the journal pool has one worker, so a held copy would
otherwise queue the close flush behind it (an earlier version of the test
hung in `journal_flush` for that reason).

## Green run
`ASAN_OPTIONS=detect_leaks=1 build/san/tests/dataloss_test --retain-close` -> exit 0
```
dataloss_test: retain_close: closed with a held copy, retained after close=0
```

## Verification (AC "Charging"/"Not charging", load1 16-69 during runs)
| command | exit |
|---|---|
| make -j3 all (gcc 13, -Werror) | 0 |
| make -j3 build/san/tests/{dataloss,editor,editor_session,editor_checkpoint,journal}_test | 0 |
| san dataloss_test (full default run, includes retain_close), :99 lock, leaks on | 0 |
| san journal_test, :99 lock, leaks on | 0 |
| san editor_checkpoint_test, :99 lock, leaks on | 0 |
| san editor_session_test, :99 lock, leaks on | 0 |
| san editor_test, :99 lock, leaks on | 0 |
| release build/tests/editor_test, :99 lock (malloc guards: mallocs=0, "all passed") | 0 |

All display runs used `EDIT_DISPLAY_LOCK_TIMEOUT=1800 DISPLAY=:99
XDG_RUNTIME_DIR=$(mktemp -d) sh tools/with_display_lock.sh`. My first release
run named a wrong path (build/rel/tests/editor_test; release tests are at
build/tests/), so the lock wrapper's exec failed with ENOENT. The re-run on the
correct path passed. No GB fixture: the test file is FI2_BYTES = 2 *
FILE_PREFIX_MAX + 7 bytes.

The crashed and killed red/debug runs left four `build/dataloss-*` fixture
dirs in this worktree; I removed them.

## Remaining
Nothing for this bead.

## Out of scope (reported, not fixed)
- `journal_set_io` refuses only while the synchronous save retention
  (`j->retention`) or a worker is active. It cannot see edit-fi2 async
  retention jobs, which also borrow `&j->io`. The header contract ("no active
  retention operation") falls on callers (dataloss shared_save_as waits with
  `retain_settled`).
- With non-durable journal bytes at close, the close flush queues behind a
  running copy on the single-worker journal pool. It waits up to the copy
  time, which is 0.27-0.43 s (M) at the 105 MB bound per edit-fi2.md.
  That is a hang-shaped wait, but bounded.
