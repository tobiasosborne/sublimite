# edit-e3z5 worker report: Save As retries a refused checkpoint; a dropped Reload says why

The design is in docs/decisions/edit-e3z5.md. It follows edit-ivv out-of-scope
items 1 and 2.

## Cause

1. **Save As.** `commands.c` `save_as` writes the file, then reloads the peer
   tabs open on the target and checkpoints.
   - If the shared directory guard refused the peer reload's checkpoint
     (`JOURNAL_BUSY`), the peer was not retired, so `!other->retired` made the
     command return `EDITOR_ERR_IO`.
   - If the guard refused the command's own checkpoint, the BUSY result also
     returned `EDITOR_ERR_IO`.
   - In both cases the user saw "save as failed" although the file was
     written. In the peer case the peer was also left marked modified with the
     overwritten text.
2. **Reload.** `buffers.c` `reload_source` returned 0 without saying why in
   three cases: no free buffer slot, `file_save_busy`, or `op_count != 0`. It
   did the same when acquisition failed. The explicit Reload choice was
   consumed and nothing happened.

## Files touched

- src/editor/private.h: new `editor_reload_outcome` enum (DONE, FAILED,
  REFUSED, DEFERRED, NO_SLOT). `editor_reload_source` takes an outcome
  pointer. The comment on `busy_checkpoint` now covers Save As.
- src/editor/buffers.c:
  - `reload_source` reports its outcome.
  - The clean refresh skips only on REFUSED, as it did before.
  - An explicit Reload re-arms on REFUSED or DEFERRED.
  - On NO_SLOT or FAILED it shows a notice, does not re-arm, and keeps the
    prompt. Either notice is cleared when the next choice is consumed.
- src/editor/commands.c `save_as`: a refused peer reload makes the peer clean
  and stale. The existing clean refresh then replaces it on the journal tick.
  After one refusal no further attempt is made in this tick. A refused or
  skipped command checkpoint sets `busy_checkpoint`. Other errors are
  unchanged.
- src/editor/checkpoint.c: comment only.
- tests/dataloss_test.c: new tests, also run in the default suite.
  - `e3z5_save_as(peer)` covers Save As with the guard held, with and without
    a clean peer tab.
  - `e3z5_no_slot` covers an explicit Reload with all slots full: the notice
    appears, the prompt stays, and R lands after a tab is closed.
  - New selector: `--edit-e3z5`.
- docs/decisions/edit-e3z5.md, and this report.

## Red run

Clang ASan/UBSan, `build/san/tests/dataloss_test --edit-e3z5`, run before any
source change. The selector runs every case, so each failure shows.

```
dataloss_test: edit-e3z5 save_as(peer) with guard held: notice=save as failed refusals=1 owed=0 path_moved=1 owner_modified=0 peer_stale=0 peer_modified=1
dataloss_test:1067: FAIL !e->recovery_notice
dataloss_test: edit-e3z5 save_as(alone) with guard held: notice=save as failed refusals=1 owed=0 path_moved=1 owner_modified=0 peer_stale=-1 peer_modified=-1
dataloss_test:1067: FAIL !e->recovery_notice
dataloss_test: edit-e3z5 reload with no free slot: notice=(none) stale=1 choice=0
dataloss_test:1111: FAIL e->recovery_notice && strstr(e->recovery_notice, "reload") && e->buffer->source_stale
exit=1
```

## Green run

ASAN_OPTIONS=detect_leaks=1:

```
dataloss_test: edit-e3z5 save_as(peer) with guard held: notice=(none) refusals=1 owed=1 path_moved=1 owner_modified=0 peer_stale=1 peer_modified=0
dataloss_test: edit-e3z5 save_as(peer) landed after the guard was released
dataloss_test: edit-e3z5 save_as(alone) with guard held: notice=(none) refusals=1 owed=1 path_moved=1 owner_modified=0 peer_stale=-1 peer_modified=-1
dataloss_test: edit-e3z5 save_as(alone) landed after the guard was released
dataloss_test: edit-e3z5 reload with no free slot: notice=reload needs a free tab slot: close a tab, then press R stale=1 choice=0
dataloss_test: edit-e3z5 reload after a slot freed: notice=(none) stale=0
exit=0
```

## Verification

Box state: BAT0 "Charging". Load average 24 to 33 throughout, because the box
is shared. None of these runs is a timing claim. All sanitizer runs used
ASAN_OPTIONS=detect_leaks=1. No LeakSanitizer, AddressSanitizer or UBSan
reports appeared in any log.

| Command | Exit |
|---|---|
| `make -j3 all` (gcc release, -Werror) | 0 |
| `build/san/tests/dataloss_test` (full default suite, includes e3z5 and ivv) | 0 |
| `build/san/tests/editor_session_test` (full; includes the ivv 1 GiB sparse fixture) | 0 |
| `build/san/tests/editor_checkpoint_test` | 0 |
| `build/san/tests/journal_test` | 0 |
| `build/san/tests/editor_close_test` | 0 |
| `build/san/tests/editor_test` on :99 under the lock | 0 ("all passed"; no sanitizer reports) |
| `build/tests/editor_test` release on :99 under the lock | 0 ("all passed"; the malloc guard lines report 0 allocations, e.g. edit-g11 `mallocs=0`) |
| `build/san/tests/cli_test` on :99 under the lock (extra; not in the brief's list) | 1 on both runs; see below |

Both cli_test failures were about the display, not this bead:

- The first run failed at `cli_test:129`: `xcb_connect` to the private display
  failed.
- The second run failed at `cli_test:555` ("window not created within 0 s
  (load1=18.85) [forced test timeout]") and at `cli_test:395` (`xcb_connect`
  failed again).
- `xdpyinfo -display :99` answered right afterwards.
- cli_test drives `src/main.c` launches. This bead changes neither `main.c`
  nor the flush path that cli_test interposes (`editor_flush_settle`).
- I read this as display contention and load on the shared box (load average
  around 29). The coordinator should re-run cli_test on a quieter display.

Disk use around editor_session_test, whose 1 GiB fixture is sparse and
removed: before 385G used / 71G avail; after 385G used / 71G avail.

## What remains

Nothing in scope.

- The `op_count` and `file_save_busy` deferrals have no dedicated test. Both
  share the re-arm branch with REFUSED, which `ivv_choice(true)` covers.
  Neither state can be reached deterministically from the choice path without
  white-box edits to staged journal ops: that path returns early on
  `editor_save_active`.

## Found outside scope (reported, not fixed)

1. **The UI loop spins while a choice waits.** `editor.c` `runnable()` counts
   any pending `e->source_choice` as runnable work. That includes a choice
   waiting on `editor_save_active` or, since edit-ivv, on `busy_due_ns`. While
   it waits, `wait_timeout` returns 0 and the UI loop polls with a zero
   timeout for up to one journal tick, or for the length of the save. That is
   CPU spin, not a hang. A gate such as `e->source_choice &&
   editor_checkpoint_due(e) && !editor_save_active(e)` in `runnable()` would
   avoid it. Not changed here.
2. **A Save As that cannot reload a peer for another reason** (DEFERRED,
   NO_SLOT or FAILED) still returns `EDITOR_ERR_IO` ("save as failed") after
   the write. The peer is then left modified with the old text. This predates
   the bead, which covered only the BUSY case.
