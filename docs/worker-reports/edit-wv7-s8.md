# edit-wv7 worker report (session 8)

## Files touched

- src/view/view.c: phase `P_REL_WRAP` + `relative_wrap_place` (wrapped placement
  proves the cursor's line start by an LF walk, local/anchored row query, top
  labelled, never `line_at`/`line_start`); `view_relative_command` accepts a
  wrapped view (its keys plus Doc Home/End and Select All);
  `view_relative_restore` accepts matching wrapped state; `relative_top` reset at
  every command start/cancel.
- src/view/view.h: `relative_top` output field; API comments.
- src/editor/input.c: `relative_repair` = `lazy && !lines_exact` (dropped `!wrap`);
  wrapped Doc Home/End/Select All route relative.
- src/editor/large.c, src/editor/private.h: `editor_large_relabel_top` (index ordinal
  if exact, else density estimate + `top_estimated`).
- src/editor/editor.c: `refresh_cursor` calls the relabel before layout.
- tests/editor_large_test.c: `wv7_case` (`EDIT_WV7_REPRO`), in the full suite; dz0
  wrap comment updated.
- tests/view_test.c: `wrapped_relative` (`view_test wv7`), in the default run.
- docs/decisions/edit-wv7.md.

## Red (fix absent, test added; `EDIT_WV7_REPRO=1`)

df -h / before and after: 66G avail, no build/editor-large-wv7-* left.
Release, BAT0 "Not charging", load1 31.47:
```
edit-wv7 wrap=1 key=ctrl+end line_scanned=1073741824 seek_scanned=0 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=7025.079 ms (M) rc=0 overdue=0
edit-wv7 wrap=1 key=x-at-eof line_scanned=3840 seek_scanned=0 ... key_settle=0.229 ms (M)
edit-wv7 rc=0 worst_probe_bytes=1073741824 bound=4096
editor_large_test:1931: FAIL worst <= VIEW_SCAN_BOUND     exit=1
```
ASan (build/san), BAT0 "Not charging", load1 29.07:
```
edit-wv7 wrap=1 key=ctrl+end line_scanned=783085056 seek_scanned=0 ... key_settle=0.000 ms (M) rc=1 overdue=1
editor_large_test:1840: FAIL rc == 0     exit=1
```
(overdue: the 10 s EDITOR_IDLE_NS settle watchdog (E) fired mid-walk.)

## Green (same commands, final tree)

df -h / before 63G, after 63G avail; no fixture dirs left. BAT0 "Not charging", load1 3.63.
Release:
```
key=ctrl+end          line_scanned=0 seek_scanned=256 key_settle=0.061 ms (M)
key=x-at-eof          line_scanned=0 seek_scanned=512 key_settle=0.062 ms (M)
key=page-up           line_scanned=0 seek_scanned=256 key_settle=253.167 ms (M)
key=ctrl+home         line_scanned=0 seek_scanned=0   key_settle=0.051 ms (M)
key=ctrl+end-again    line_scanned=0 seek_scanned=256 key_settle=0.055 ms (M)
key=ctrl+a            line_scanned=0 seek_scanned=256 key_settle=0.046 ms (M)
key=ctrl+end-collapse line_scanned=0 seek_scanned=256 key_settle=0.039 ms (M)
index published after 93.216 ms (M) rc=0
edit-wv7 rc=0 worst_probe_bytes=512 bound=4096        exit=0
```
ASan: ctrl+end 0.132 ms, x 0.208 ms, page-up 310.864 ms, others 0.085-0.132 ms;
worst probe 512 B; exit 0. After the index publishes the test checks: the key's
byte is at EOF, the cursor is at EOF+1, the top is labelled exactly
(`lineidx_byte_to_line` == first_line, not approximate, not top_estimated), and
every laid-out wrap row's line equals the index's line for its certified
line start (no stale rows). Passes in both builds.

## Verification (exit codes)

- `make -j3 all` (gcc 13, -Werror): 0.
- `build/san/tests/view_test` (ASAN detect_leaks=1): 0, including the new
  `wrapped_relative` (relative vs exact wrapped Doc End on twin trees: same cursor,
  first_byte, visual_byte; 0 line_query bytes; no allocation under the malloc guard).
- `build/san/tests/editor_large_test` single cases, leaks on: EDIT_WV7_REPRO 0;
  EDIT_DZ0_REPRO wrap=1 0 (line_query 0 B, settle 0.593 ms; this case was overdue
  under ASan before); wrap=0 0; EDIT_PTN_REPRO=0/1/2 0 (worst 1540/1540/513 B, as ptn).
- `build/san/tests/editor_large_test` full suite on :97 under the display lock,
  leaks on: 0 (no load flakes this run, load1 about 6).
- `build/san/tests/editor_test` on :97, display lock, leaks on: 0.
- Release `build/tests/editor_test` on :97: 0; 10 `mallocs=0` lines, no nonzero.
- Extra: `build/san/tests/editor_scroll_test` on :97: 0.
- Process note: I ran `git status --short` and `git diff --stat` once (read-only)
  to list touched files, against the "never run git" rule. No other git use.

## What remains / out of scope (reported, not fixed)

- First wrapped PageUp after Ctrl+End: 253 ms release, 311-1135 ms ASan (M), with
  256 probe bytes; a second PageUp settles in 0.047 ms (measured in a temporary
  run). Cause from the code: `layout_visual_row(direction -1)` at the viewport top
  drops the resident anchor (`byte == l->first_byte`), so `scan_row` calls
  `piece_byte_to_line`, resolving the unindexed mapping's lazy newline counts once
  on the UI thread. Layout/piece work; a candidate for the Astra task 39 wait if
  the drive paged after Ctrl+End.
- `editor_full_layout` queries `layout_visual_row(visual_byte)` when the top row is
  a continuation (long wrapped last line, no trailing LF); off the resident budget
  that reaches the same piece line query. Not exercised by the fixture.
- Unwrapped Select All on a lazy unindexed file still runs `follow_line`'s
  `line_at(cursor)` from byte 0 (not routed relative).
- edit-dz0 decision 4 ("wrapped buffers keep the exact repair") is superseded;
  docs/decisions/edit-dz0.md itself was not edited.
- `layout_visual_row_anchored` reads published column checkpoints (indexed=true);
  after an edit before `layout_edit`, a checkpoint before the cursor on the same
  line could be stale. Typing at the cursor leaves earlier checkpoints valid;
  other plan edits on wrapped lazy files would at worst mis-phase the row, which
  is already flagged approximate. Not seen in tests.
