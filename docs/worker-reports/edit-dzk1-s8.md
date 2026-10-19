# edit-dzk1 (s8): render-rule violations no longer end the loop; a tab that dies in its first paint is restored closed

Design and limits: docs/decisions/edit-dzk1.md. The edit-2ghs font fix is not in this tree. The tests inject its zero-sized descriptor through private access to the font cache's bound table.

## Fix

1. **Render-rule violations.** `render_backend_check` (new, in render.c) is the adapter's front-end check, split out of `render_backend_submit` without changing behaviour.
   - When submit is rejected and that check also fails, `submit` calls `render_rejected` instead of ending `editor_run`.
   - `render_rejected` records `TRACE_RENDER_REJECTED`, updates `editor_stats.render_rejects` and `render_reject_cause`, and puts a status-row notice up. It then repairs the descriptor table and the cells (`font_async_repair` → new `render_grid_repair`) and resubmits the same frame once.
   - If the grid cannot be repaired, the frame is dropped. `dirty` is cleared and the next frame is full.
   - A backend's own failure (the check passes) still ends the loop, as before.
2. **Crash-loop guard.** New journal record type `JOURNAL_PAINT` (11, empty payload, id = the buffer being painted).
   - Before a buffer's first frame of the launch is composed, PAINT(id) goes into the page cache.
   - The first present of that frame writes PAINT(0), and so does a clean close. An error exit does not.
   - `editor_checkpoint` keeps a pending marker.
   - Recovery restores the buffer named by the last PAINT as the newest closed tab, with a notice; Ctrl+Shift+T reopens it. Its edits are kept.

## Files touched

- src/render/render.c, src/render/render.h: `render_backend_check`, `render_grid_repair`. Validation is split into helpers (pages, glyph fit, region); the checks themselves are unchanged.
- src/font/async.c, src/font/font.h: `font_async_repair` (blank = baked ASCII space, slot 0)
- src/trace/trace.h, trace.c, trace_fmt.h: `TRACE_RENDER_REJECTED`; the startup-stamp range now ends at `TRACE_STARTUP_END`
- src/journal/journal.h, journal.c: `JOURNAL_PAINT` type and payload rule
- src/editor/editor.c: `render_rejected`, the PAINT call in submit, the full frame after a drop, present → painted, PAINT(0) on clean close
- src/editor/editor.h: `editor_stats.render_rejects`, `render_reject_cause`
- src/editor/private.h: `editor_buffer.painted`, `editor.journal_paint`, `editor.render_dropped`, three declarations
- src/editor/checkpoint.c: `editor_journal_paint`, `editor_journal_painted`, `editor_journal_paint_clear`; checkpoint carries PAINT
- src/editor/recovery.c: replay keeps the last PAINT; `install_session` restores that tab closed with a notice; `session_fits` accounts for it
- src/editor/restore.c: lazy large-journal scan ignores PAINT
- tests/editor_test.c: `dzk1_render_reject` (`--dzk1`, also in the default suite)
- tests/editor_session_test.c: `dzk1_crash_loop` (`--edit-dzk1`, also in the default suite)
- tests/render_test.c: `repair_test` (written after the implementation, as module coverage; the bead's red tests are the two above)
- fuzz/journal_fuzz.c: the structured type range now includes PAINT
- docs/decisions/edit-dzk1.md, this report

## Red runs (unmodified source, tests added first)

```
$ build/tests/editor_test --dzk1
editor_test:63: FAIL rc == EDITOR_OK || rc == EDITOR_MORE
editor_test:5364: FAIL press(e, key('x', 0, "x")) == 0          (editor_step returned -2, RENDER_ERR_BOUNDS)
rc=1
$ build/tests/editor_session_test --edit-dzk1
editor_session_test:2147: FAIL rc == 0
editor_session_test:2171: FAIL dzk1_guarded(dir) == 0
dzk1: launch 3 tabs=2 closed=0 notice=none                      (the killed tab came back live)
rc=1
```

## Green runs

```
$ build/tests/editor_test --dzk1
editor_test: dzk1 render-rule violations repair or drop the frame and the loop survives passed
$ build/tests/editor_session_test --edit-dzk1
dzk1: launch 3 tabs=1 closed=1 notice=a restored tab died in its first paint last time; it is closed (reopen: Ctrl+Shift+T)
dzk1: launch 4 tabs=2 notice=none
dzk1: a tab killed in its first paint after restore is set aside on the next launch; its text survives
```

What the tests check:

- **editor_test:**
  - A zero-sized descriptor is repaired: the frame is submitted, the grid validates, and the notice is shown.
  - An unrepairable page (width 0) drops the frame, and the loop settles with no resubmission spin.
  - Once the page is restored, the next key paints, and the text is intact.
- **editor_session_test:**
  - Launch 2 restores two dirty tabs, paints alpha, then exits inside the backend submit of the restored bravo tab's first paint.
  - Launch 3 restores alpha live and bravo closed, with the notice. Reopening bravo shows its unsaved edit `ybravo`.
  - Launch 4 restores both tabs live with no notice, so the clear path works.

## Verification

Box stamp: BAT0 `Charging` at 12:42 and `Not charging` at 13:12 and 13:16. Load average was 35.4 at 12:42, 14.1 at 13:12 and 20.5 at 13:16. No timings are claimed.

| command | exit |
|---|---|
| `make -j3 all` (gcc, -Werror), final tree | 0 |
| `make -j3 build/san/tests/{editor,editor_session,editor_checkpoint,dataloss,journal,render,unicode_render,raster}_test` | 0 |
| `make build/fuzz/fuzz/journal_fuzz.o` | 0 |
| san `render_test`, under with_display_lock on :99, ASAN_OPTIONS=detect_leaks=1 | 0 |
| san `journal_test`, same | 0 |
| san `unicode_render_test`, same | 0 |
| san `editor_checkpoint_test`, same | 0 |
| san `dataloss_test`, same | 0 |
| san `raster_test`, same | 0 |
| san `editor_session_test`, same; the first full run failed in `existing_journal` (see below) and the rerun passed | 0 |
| san `editor_test`, full suite; prints "dzk1 ... passed" and "editor_test: all passed" | 0 |
| release `EDITOR_ALLOC_ONLY=null build/tests/editor_test`: "null 10000 keys mallocs=0 guard=active" | 0 |
| release `EDITOR_ALLOC_ONLY=raster build/tests/editor_test`: "raster 10000 keys mallocs=0 guard=active" | 0 |
| release `build/tests/editor_test`, full suite on :99 | 0 |
| release render, journal, editor_session, editor_checkpoint, dataloss, unicode_render, editor_hot_exit (100 kill trials) | 0 |

No GB fixtures were used. The dzk1 session test removes its `build/editor-dzk1-*` root on every path.

**`existing_journal` flake (pre-existing, not this bead).** Under ASan it sometimes fails at `rmdir(root)` (editor_session_test:572), leaving one `.sublimite-base-*` retained copy behind.

- Focused `--edit-djg` reruns with this tree's binary failed 1 of 4.
- A control san binary from a tree without these changes (.wt/edit-uzrb-s8, run read-only from a scratch directory) failed 1 of 8 the same way.
- Release: 8 of 8 passed.

Probable cause: the edit-fi2 retained copy is dropped by a later `editor_retain_tick`, and the test calls `editor_close` immediately after the save.

## What remains / out of scope (reported, not fixed)

1. **The `existing_journal` retained-copy race above.** The test or `editor_close` should drop a READY copy whose BASE was replaced by the save.
2. **Save checkpoints drop the marker.** The worker-built save checkpoints (session.c capture) do not carry a pending PAINT. A save that rotates inside a first paint allows one more crash before the guard engages.
3. **`painted` is per launch, not per backend.** A GL-only paint crash after raster painted the tab during bootstrap is not guarded.
4. **Downgrade data loss.** A build older than this one truncates a journal at the first PAINT record on recovery. Old binaries must not share a journal directory with this build.
5. **Merge with edit-2ghs.** After it lands, `font_async_repair` could also clear the new `inked` flag for repaired slots. It is not required: the blank is a valid space rectangle either way. The async.c hunks do not overlap.
6. **Globals checker.** `src/file/check_signal_globals.py build/libedit.a` fails on 110 pre-existing mutable symbols (tree-sitter, wayland listeners, and others). None come from this bead: `render_notice` is a `static const` in .rodata.
