# edit-1vun (s8) worker report

Bead: layout's upward row query, editor_full_layout's continuation query and
unwrapped Select All no longer resolve a lazy file's line counts from byte 0
on the UI thread. Decisions: docs/decisions/edit-1vun.md.

## Files touched

- src/layout/layout.h: optional probe `uint64_t *line_query_probe`.
- src/layout/wrap.c: probe counting in `scan_row`; `above_top` (backward LF
  walk from the top, at most LAYOUT_BYTE_BUDGET = 65,536 B (P)) and
  `on_top_line`, used by `layout_visual_row` when there is no resident seed.
- src/editor/editor.c: `editor_full_layout` asks
  `layout_visual_row_anchored(visual_byte, first_byte, lay.first_line)` when
  the continuation top is more than LAYOUT_BYTE_BUDGET past first_byte.
- src/view/view.c, src/view/view.h: `view_relative_command` accepts unwrapped
  VIEW_SELECT_ALL (phase P_SETUP, relative follow).
- src/editor/input.c: Select All routes relative whenever relative_repair holds.
- tests/editor_large_test.c: per-key layout line-query probe in wv7_case
  (asserted 0); unwrapped Ctrl+A in ptn PTN_KEYS (line_scanned asserted 0);
  new `vun_case` (EDIT_1VUN_REPRO=1): wrapped long last line without LF
  (continuation top), Ctrl+End and PageUp, 0 layout line queries.
- tests/wrap_test.c: `above_top_queries`: every query at or above a top equals
  a twin laid out from byte 0, 0 piece line queries; a >64 KiB previous line
  keeps the piece query.
- docs/decisions/edit-1vun.md, this report.

## Red (release, before the fix; 11:40, power Charging, load1 3.80)

```
edit-wv7 wrap=1 key=page-up line_scanned=0 seek_scanned=256 ... layout_line_queries=3 key_settle=216.732 ms (M)
editor_large_test:1854: FAIL queries == 0
edit-1vun continuation-top key=ctrl+end ... seek_scanned=1048832 layout_line_queries=1 ... row0_continuation=1 key_settle=220.260 ms (M)
editor_large_test:1993: FAIL queries == 0
edit-ptn key=ctrl+a probe_bytes=1073741825 ... key_settle=712.744 ms (M)
edit-1vun unwrapped select-all line_scanned=1073741825 (P: 0)
editor_large_test:1815: FAIL rc == 0
```
wrap_test with the new layout paths disabled (results equal, probe not):
`FAIL wrap_test:473 compared>0 && probe==0`.

## Green

Release, repro cases (11:43, Not charging, load1 5.59; ptn 11:41, load1 4.97):
```
edit-wv7 wrap=1 key=page-up line_scanned=0 seek_scanned=256 bound=4096 layout_line_queries=0 key_settle=0.117 ms (M)
edit-1vun continuation-top key=ctrl+end ... layout_line_queries=0 ... key_settle=0.770 ms (M)
edit-1vun continuation-top key=page-up ... layout_line_queries=0 ... key_settle=0.714 ms (M)
edit-ptn key=ctrl+a probe_bytes=256 ... key_settle=0.105 ms (M)
edit-1vun unwrapped select-all line_scanned=0 (P: 0)
```
All wv7 keys: layout_line_queries=0, worst line+seek probe 512 B <= VIEW_SCAN_BOUND 4096.
Full release suite (11:50:56, Not charging, load1 18.09): PageUp 0.105 ms (M).
ASan full suite (11:51:41, Charging, load1 13.54): PageUp 0.423 ms; continuation
Ctrl+End 24.571 ms, PageUp 25.748 ms (M) (wv7 measured 311-1135 ms ASan before).

## Verification

| command | exit |
|---|---|
| make -j3 all (final tree) | 0 |
| ASAN_OPTIONS=detect_leaks=1 build/san/tests/layout_test | 0 |
| ASAN_OPTIONS=detect_leaks=1 build/san/tests/view_test | 0 |
| ASAN_OPTIONS=detect_leaks=1 build/san/tests/wrap_test | 0 |
| ASAN_OPTIONS=detect_leaks=1 build/san/tests/editor_large_test (full, 3rd run) | 0 |
| build/tests/editor_large_test (full, 3rd run) | 0 |
| ASan editor_large_test repro cases WV7, 1VUN, PTN 0/1/2, DZ0, DZ0 wrap | 0 each |
| XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh build/san/tests/editor_test (leaks on) | 0 |
| same, release build/tests/editor_test (malloc guard active, mallocs=0 everywhere) | 0 |

df -h /: 395G used / 61G available before the first GiB fixture; 400G / 56G at
the end. The disk is shared with other workers; every editor-large-* fixture
root was gone after each run (`ls build | grep editor-large` printed nothing).

Earlier full runs failed before reaching this bead's cases, in fixtures this
bead does not touch: release twice at `editor_large_test:714 FAIL
!e->buffer->pending_open && editor_large_status_get(e).mapped`
(wrapped_byte_jump, load1 about 21); ASan once at `:802 FAIL
file_snapshot_bytes(...)` (cold_bracket_paint, the same failure edit-52o-s7
reported) and once at :714. wrapped_byte_jump then passed 4 of 4 times alone
with the fix, and passed with the fix reverted too. Both full suites passed on
rerun.

## What remains / out of scope (reported, not fixed)

- If the line before the top starts more than 65,536 B above it (a previous
  line longer than LAYOUT_BYTE_BUDGET), the piece query stays. On an unindexed
  lazy file that is still the one-time lazy count. wrap_test pins this.
- The editor_large_test flakes above (mapping/pending_open right after
  open under load) are worth a backlog look. They are timing races in
  fixtures, not something this bead caused.
- Not addressed (the brief listed it as possible only): layout_visual_row_anchored
  may read stale published column checkpoints after an edit before the cursor
  on the same line.
- Observation: in vun_case PageUp moves the cursor 30 B (one visual row) on the
  long approximate line, both before and after the fix. With the 3-row
  config the page may be one text row; I did not investigate further.
- The probe adds one pointer field to the public layout struct. It is test
  instrumentation, NULL unless a caller sets it.
