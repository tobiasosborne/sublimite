# edit-601 worker report (session s8, 2026-10-18)

Bead: edit-601 [BUG] P2, recoverability of the qct lazy restore. Decisions are in
`docs/decisions/edit-601.md`.

## Files touched

- `src/editor/restore.c`:
  - (1) The source is never unlinked or set aside without a checkpoint that returned 0. With no launch journal it is kept, together with its lock name.
  - (2) `install_all` changes, all before the source is retired:
    - a capacity re-check (`fits`, shared with `prepare_all`);
    - `e->recovering` held while tabs change, so no metadata or session publish runs before the checkpoint;
    - `p->retired` is set only after a durable checkpoint;
    - on any failure, `rollback` (drop installed tabs, give the placeholder its tab back, reactivate) and then `restore_fail`;
    - save setup moved after the checkpoint.
  - (3) `restore_fail` always returns 0.
  - (4) TABS ids are deduped in the worker scan, and install bounds its order at `r->count`.
  - Test seam: `r->fault`.
- `src/editor/private.h`: `int restore_fault` test seam, in the `fail_request` pattern.
- `src/editor/editor.c`: comment at the `editor_restore_service` call that states the new contract. No code change.
- `src/tabs/tabs.h`, `src/tabs/tabs.c`: new `tabs_drop`, the undo of `tabs_open`. Rollback needs it so it does not evict a user's closed tab.
- `tests/editor_session_test.c`: `e601_restore_failures`, cases (a)-(d) plus a journal consistency checker.
- `tests/tabs_test.c`: `drop()`.
- `docs/decisions/edit-601.md`, this report.

## Red run (pre-fix restore.c, tests and seam field only)

build/san/tests/editor_session_test, ASAN_OPTIONS=detect_leaks=1, on :99 under the display lock.
Not charging, load average 68.58 (08:54). df -h / before: 376G used, 80G available. After: the same.

```
editor_session_test:1162: FAIL rc == 0 && live && kept                       (a) source deleted with no launch journal
editor_session_test: restore step returned -103 (stats.error_cause=-103)      (b) checkpoint failure -> fail()
editor_session_test:1134: FAIL rc == 0
editor_session_test:1201: FAIL rc == 0 && settled && inert && notice && kept && consistent == 0
editor_session_test:1221: FAIL rc == 0 && settled && notice && kept          (c) seam not yet consumed (see note)
==2060848==ERROR: AddressSanitizer: heap-buffer-overflow ...                  (d)
    #0 in install_all src/editor/restore.c:478:77
SUMMARY: AddressSanitizer: heap-buffer-overflow src/editor/restore.c:478:77 in install_all
exit=1
```

Note on (c): before the fix, a scan failure already went to `restore_fail`, which returned 0 unless the journal append failed. The red result for (c) only shows that the seam was not consumed yet. (b) is the real reproduction of item (3): the checkpoint failure reached `fail()` (-103). The ASan abort in (d) left its sparse fixture behind. I removed it by hand (`rm -rf build/editor-601-*`).

`tests/tabs_test.c drop()` was red as a compile failure: `tabs_drop` was undeclared before the API existed.

## Green run

build/san/tests/editor_session_test, leaks on, :99 under the display lock (EDIT_DISPLAY_LOCK_TIMEOUT=3600, because the lock is shared with other workers).
Not charging, load average 36.59 (09:19) and 49.44 (09:40). df -h /: before 382G used, 74G available; after 386G used, 70G available. Other workers share the disk. No editor-601 fixture was left behind.

```
edit-601 (a): no launch journal rc=0 restored_tab=yes len=1073741824 source_kept=yes notice=large session restored; its journal is kept (this launch has no session journal)
edit-601 (b): checkpoint failure rc=0 settled=yes placeholder_fallback=yes source_kept=yes notice=large session journal could not be restored; journal retained
edit-601 (c): scan NOMEM rc=0 settled=yes source_kept=yes notice=large session journal could not be restored; journal retained
edit-601 (d): duplicate TABS ids rc=0 text=yes orphan_retired=yes
edit-601: no-journal restore keeps the source; checkpoint failure and scan NOMEM are notices; duplicate TABS ids stay in bounds
editor_session_test: headless save/repeated/new-target, ... allocation guard passed
exit=0
```

In (b), the launch journal was checked after the failure, and again after a later successful checkpoint. Neither time does any record name an id without a BASE.

## Verification

| command | exit |
|---|---|
| `make -j3 all` (gcc release, -Werror) | 0 |
| build/san/tests/editor_session_test (leaks on, :99, lock) | 0 |
| build/san/tests/editor_checkpoint_test (leaks on, :99, lock) | 0 |
| build/san/tests/journal_test (leaks on) | 0 |
| build/san/tests/journal_retire_test (leaks on) | 0 |
| build/san/tests/dataloss_test (leaks on; two_process_save passed on the first run) | 0 |
| build/san/tests/tabs_test (leaks on) | 0 |
| build/tests/tabs_test (release, guard=active, mallocs=0) | 0 |
| build/tests/editor_test (release, :99, lock): 10 guard lines, all mallocs=0, none nonzero | 0 |

Timings in these logs are tracked only. Box state: Not charging (AC), load average 28-68 during the runs.

## What remains

- Nothing in scope.
- The multi-buffer rollback path (installed extras dropped by `tabs_drop`, closed extras reopened and then dropped) is covered only by the tabs unit test. The fixtures have a single buffer.

## Out of scope (reported, not fixed)

- **No-journal restore opens as a conflict.** buffers.c sets `origin` only when `e->journal` exists or `recovering` is set, so with no launch journal `editor_restore_apply` sees an origin mismatch. Case (a) measured it: the restored length is the BASE length 1073741824, not BASE + 1 (M), so the edits are not shown. The source is kept, so nothing is lost.
- **Leftover directory from another test.** A full green run leaves `build/editor-session-XXXXXX/.sublimite-base-*` behind (4 bytes). It comes from an existing test (retained-base litter, probably crash_after_evicted_checkpoint), not from the edit-601 cases. I removed it by hand.
- **Display lock wait too short under load.** The default 600 s timed out twice under load from other workers. I re-ran with a longer EDIT_DISPLAY_LOCK_TIMEOUT.

## Follow-up after the rebase onto main (edit-4da landed)

edit-4da now restores a differences-only session on a mapped GB BASE eagerly at startup. On the rebased tree, the e601 cases therefore failed at the `qct_listed` deferral assertion (`e->recovery_deferred == 1`). Fix, test only: a new helper `e601_start` launches with `copy_threshold = UINT64_MAX`. This is 4da's own "differences with COPY acquisition deferred" shape, and it keeps the qct orphan deferred. The helper then sets `e->cfg.copy_threshold = 0` ((P) zero selects the configured default), so the lazy restore under test opens the BASE mapped, as it did before 4da. The intent of the four assertions is unchanged.

Verification:
- `make -j3 all`: exit 0.
- `make -j3 build/san/tests/editor_session_test`: exit 0.
- `ASAN_OPTIONS=detect_leaks=1 XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:97 EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh build/san/tests/editor_session_test`: exit 0.

Box state: Discharging [bat], load average 13.7. df -h /: 67G available before and 67G after.

```
edit-601 (a): no launch journal rc=0 restored_tab=yes len=1073741824 source_kept=yes notice=large session restored; its journal is kept (this launch has no session journal)
edit-601 (b): checkpoint failure rc=0 settled=yes placeholder_fallback=yes source_kept=yes notice=large session journal could not be restored; journal retained
edit-601 (c): scan NOMEM rc=0 settled=yes source_kept=yes notice=large session journal could not be restored; journal retained
edit-601 (d): duplicate TABS ids rc=0 text=yes orphan_retired=yes
```

The run again left the pre-existing `build/editor-session-*/.sublimite-base-*` litter, which I removed. The no-journal conflict quirk is unchanged: (a) still restores length 1073741824, which is the BASE length, not BASE + 1.
