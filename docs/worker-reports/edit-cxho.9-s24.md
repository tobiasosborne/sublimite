# edit-cxho.9 (session 24 worker report): Minimap B2: the miniature shows the window's real lines

Bead: edit-cxho.9. Worktree: `.wt/edit-cxho.9-s24`. Display: :95. Decision note: `docs/decisions/edit-cxho.9.md`.

## What changed

**`src/minimap/minimap.h` and `src/minimap/minimap.c`**
- The 256-sample striped path is deleted: `large_rows`, `sample_ink`, `MINIMAP_SAMPLE_BYTES` and `MINIMAP_MAX_SAMPLES`.
- **New `minimap_prepare_window(m, input, snapshot_source, anchor, height)`.** Row i is the real logical line `top + i`.
  - Each line is summarised from its first 80 bytes (MINIMAP_LINE_CAP).
  - Its end is found by a newline search capped at MINIMAP_SCAN_BYTES. (G) The cap is LINEIDX_CHUNK, the existing per-query scan bound.
  - With an exact window (index ready), the walk goes back `first_line - window.first_line` lines from the anchor's line start.
  - While the index builds, the window comes from `minimap_window_bytes`. The anchor byte is snapped to its line start and the walk goes back `above` lines.
- **New `minimap_anchor_moved`** reports that the window is due again because the viewport scrolled.
- **Legacy `minimap_prepare` on a large file** now builds the window at line 0, one real line per row. `minimap_hit` returns cached exact line starts and needs no index.
- **Staleness.** A window prepared while unindexed stays fresh for the same unindexed source. It becomes stale when the index completes.
- **Fill.** `minimap_fill` and `fill_cached` were refactored to an internal `fill(..., pending)`, so a fresh unindexed window paints as ready.
- **Small files are unchanged:** exact, with lines grouped per row.

**`src/editor/minimap.c`, `src/editor/private.h`, and one call in `src/editor/paint.c`** (the sync branch now calls `editor_map_fill_sync`)
- Windowed jobs carry an anchor built from `state.first_line`, `state.first_byte` and the visible line count.
- **Geometry is unchanged.** The window uses the painter's own 2 px per summary row over `map_grid.dims.rows * 2`. That keeps today's painter correct: row i paints at y = 2i, and the box lands on the viewport's rows at box_top + phase, where phase < 2 px.
- **Drag is unchanged.** Large maps are still `sampled`, so the drag stays on the byte-fraction path until edit-cxho.10.
- **When a job is due:** when the map is stale, when the height changed, or for a windowed map when the anchor moved.
- **File-backed first map.** It stays a synchronous compose prepare (edit-73j), now the bounded window.
- **File-backed refresh while indexing.** It is held and the map is marked stale, so the retained image and its box stay on screen. Such a refresh would queue on the bulk lane behind the index build and could only land stale after it. editor_large_test caught this as an extra frame.

**Tests, fuzz and bench**
- `tests/minimap_test.c`:
  - New tests: `window_lines_exact`, `no_stripes`, `window_unindexed`, `window_long_line`.
  - `large_and_errors` and `sample_cap_and_resize` were updated from the sampled model to window rows. Each new expectation carries its derivation.
- `fuzz/minimap_fuzz.c`:
  - The large model is now "line r per row".
  - New `window_rows_op`: random viewport line, byte inside it, scale and visible lines. It checks exact and byte-anchored windows against the line starts.
  - The `lineidx` use is gone, which removes the edit-cxho.29 crash path (`lineidx_seek_line` + `MUST(complete)` on inputs over 64 KiB). The harness now runs with `-max_len=1100`, which yields documents over 64 KiB, with no cap needed.
- `bench/minimap_bench.c`: the reference is updated, and a new tracked row `minimap_log_1g_window_prepare` was added.

## Red run (stub API: header declared, `minimap_prepare_window` returning ERR_ARG)

```
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/minimap_test; echo "exit $?"
display guard: real display :0 refused, using :99
minimap_test:744: rc == MINIMAP_OK && m.filled && !m.stale && m.windowed && m.window_exact && m.sampled
edit-cxho.8: fixed-scale window, constant box, round trip, clamps: ok
exit 1
```

Today's sampled legacy path, with `no_stripes` run first against the unchanged `minimap_prepare` on the 640943-byte file (10 chunks). Rows are grouped K lines each, not real lines:

```
minimap_test:725: r->first_byte == starts[top + i] && r->line_count == 1
```

## Green runs

Stamp: BAT0 "Not charging"; 12:59:21 up 1 day 15:02, load 4.93 3.93 3.85.

```
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/minimap_test   -> exit 0
edit-cxho.9 (M): window 705 rows, walk back 495, delivered 64512 bytes of 4799879, bound 103845, span calls 4032
edit-cxho.9: window at line 150000: row i is line first+i, bounded reads: ok
edit-cxho.9: 640943-byte file (10 chunks): adjacent rows are their own lines, no stripes: ok
edit-cxho.9: unindexed byte anchor snaps to line starts, walks above lines back: ok
edit-cxho.9: a line over MINIMAP_SCAN_BYTES ends the walk: ok
...
minimap_test: density, hit round trip, stale, malloc guard: ok

$ XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_test         -> exit 0  (editor_test: all passed)
$ ... build/san/tests/editor_scroll_test  -> exit 0
$ ... build/san/tests/editor_large_test   -> exit 0  (editor_large_test: ok)
```

The bound in the first line is (P): walk back w = 495 lines and rows r = 705, each line at most 52 bytes. The bound is (w+1)(52+82) + r(53) + 16 (fragment) = 103845. The 4799879 bytes are the file size.

The first editor_large_test run failed, before the file-backed hold was added:

```
editor_large_test:427: FAIL h.first_exact_frame == estimated_frame + 1u
```

The cause was a windowed refresh queued on the bulk lane behind the held index, which then published an extra estimated frame. Fixed by the hold described above. Re-run green.

**Fuzz.** Stamp: BAT0 "Not charging"; 12:53:01, load 2.64 4.75 4.19.

```
$ ASAN_OPTIONS=detect_leaks=1 build/fuzz/minimap_fuzz -runs=100000 -max_len=1100 -print_final_stats=1 -artifact_prefix=<scratch>/mmart/ <scratch>/mmcorpus
Done 100000 runs in 183 second(s)   -> exit 0
```

The run count of 100000 is a smoke bound I chose, not a budget. Why `-max_len=1100` (P): the body reads (size-4)/2 lines, at most 512, of up to 2048 bytes each. 1100 allows 548 → 512 lines, so documents well past 64 KiB, which exercises the large/windowed path.

**Bench (tracked only).** Stamp: BAT0 "Not charging" (tag [AC]); 12:56:30 up 1 day 14:59, load 1.46 3.08 3.63.

```
BENCH minimap_small_64k TRACK (M)[AC] load1=1.46 p50_ns=50867 p99_ns=63303 (G)gate_p99_ns=500000 pass=1 ... sampled=0
BENCH minimap_log_1g TRACK (M)[AC] load1=1.46 p50_ns=50359 p99_ns=56632 (G)gate_p99_ns=500000 pass=1 ... lines=8947842 sampled=1
BENCH minimap_log_1g_after_edit TRACK (M)[AC] load1=1.46 p50_ns=51744 p99_ns=61120 (G)gate_p99_ns=500000 pass=1 n=200
BENCH minimap_log_1g_window_prepare TRACK (M)[AC] load1=1.46 p50_ns=95604 p99_ns=168996 rows=705 view=45 n=2000 worker_side=1
minimap_cold_samples TRACK foreground_major_faults=0 source_calls=0 (M)[AC] load1=1.46 eviction=requested pass=1
exit 0
```

`window_prepare` is the worker-side prepare of a 705-row window. The research repro gives (M) a 1410 px strip and a 45-line view. There is no gate row for it.

## Verification

- `make -j4 all` (gcc, -Werror): exit 0.
- Sanitizer `minimap_test`, `editor_test`, `editor_scroll_test` and `editor_large_test`: exit 0 each, with detect_leaks=1. The display suites ran on :95 through `tools/with_display_lock.sh`.
- Full `make -j4 check` (DISPLAY=:95, ASAN detect_leaks=1): see below.

Stamp at start: BAT0 "Not charging"; 13:05:26 up 1 day 15:08, load 2.12 2.42 3.15. Stamp at end: 13:22:49, load 0.85 1.20 2.16.

```
$ XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 ASAN_OPTIONS=detect_leaks=1 make -j4 check; echo "make check exit $?"
check: 112 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
make check exit 0
```

Some lines in the log look like failures but come from negative cases inside passing tests:
- `refwin_pairs: FAIL incomplete run` sits inside `refwin_test`, which prints "incomplete/wrong-frame runs rejected ... PASS".
- `make[1]: *** Error 1` and the `display lock timeout` lines come from `tools/test_display_lock.sh contract`, which provokes them on purpose and ends with PASS.

**Screenshot.** Private Xvfb :95, `EDIT_REFUSE_REAL_DISPLAY=1`, private HOME/XDG dirs under the scratchpad, `build/sublimite --new-instance doc8000.md:4000`. The fixture is 8000 lines of generated Markdown, 648374 B, so it takes the windowed path (> 64 KiB): headings, paragraphs of 3 to 30 words, lists, fenced code.

- Full window: `/tmp/claude-1000/-home-tobias-Projects-sublimite/3dfb7436-2086-45df-b788-60ade1f4789a/scratchpad/shot/minimap-8000.png`
- Zoomed strip:
  - `/tmp/claude-1000/-home-tobias-Projects-sublimite/3dfb7436-2086-45df-b788-60ade1f4789a/scratchpad/shot/minimap-8000-crop-top.png`
  - `/tmp/claude-1000/-home-tobias-Projects-sublimite/3dfb7436-2086-45df-b788-60ade1f4789a/scratchpad/shot/minimap-8000-crop-bottom.png`

The strip shows continuous per-line ticks from top to bottom: paragraphs, indented code blocks and blank lines appear as themselves, with no stripes and no blank bands. The viewport box sits mid-strip over lines 4000 to 4076. The process exited 0 on SIGTERM. The fixture generator is `scratchpad/shot/mkmd.py` and the capture script is `scratchpad/shot/shot.py`.

## What remains (edit-cxho.10)

- **Drag.** The drag still maps y over the whole strip by byte fraction, while the box is drawn at the window position. They agree only to within box/strip height. The 1:1 thumb drag via `minimap_window_grab` and `_seek` is edit-cxho.10.
- **Phase.** The painter ignores `phase`, which is under 2 px of box offset.
- **Small files** (≤ 64 KiB and ≤ 20000 lines) still use the grouped whole-file map. A scrolling small map needs edit-cxho.10's drag, or today's absolute drag would run away.
- **File-backed huge files while indexing** show the first window, retained as pending with its last box, until the index completes. After that the window follows the viewport.

## Found outside scope (not fixed)

- `tests/editor_test.c:327` and `tests/editor_scroll_test.c:116` (`settle_map`) mirror the old `map_due` predicate: `index_ready && (stale || height)`. They do not know about windowed anchor moves or unindexed windows. That is harmless for their small fixtures, but a future large-file fixture that waits on `settle_map` would return before a window refresh lands.
- In `src/editor/minimap.c`, the sync first-map path on the UI thread reads file-backed pages. This was already true before this bead, for 256 dispersed samples. It now reads the window's bytes around the viewport, and it can do so during indexing. The bound is the same window bound, but on a cold mapped file it can major-fault on the UI thread. This was not measured here: `minimap_cold_samples` covers only `fill_cached`.
