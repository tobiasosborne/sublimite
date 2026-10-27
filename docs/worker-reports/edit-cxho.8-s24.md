# edit-cxho.8 (session 24): Minimap B1, pure fixed-scale window mapping

Worker: Opus, worktree `.wt/edit-cxho.8-s24`, display :95. Power: BAT0 "Not charging"; load 0.62–1.98 during the runs (`uptime` stamps below).

## Scope delivered

Pure Sublime-style mapping in `src/minimap`. No editor or rendering changes.

```c
#define MINIMAP_LINE_PX 2u            /* (G) perf §1 Layout: 2 logical px per line */
#define MINIMAP_LINE_UNKNOWN UINT64_MAX
typedef struct minimap_window {
    uint64_t first_line, range;
    uint32_t above, phase, rows, box_top, box_height, travel, scale, height;
    bool scrolls;
} minimap_window;
int minimap_window_lines(uint64_t lines, uint64_t visible, uint64_t first_line,
                         uint32_t scale, uint32_t height, minimap_window *out);
int minimap_window_bytes(uint64_t bytes, uint64_t visible, uint64_t first_byte,
                         uint32_t scale, uint32_t height, minimap_window *out);
bool minimap_window_grab(const minimap_window *w, int64_t y, int64_t *offset);
uint64_t minimap_window_seek(const minimap_window *w, int64_t y, int64_t offset);
```

The model:
- B = min(v*s, H), constant across N.
- R = N - v.
- D = min(R*s, H - B).
- The window scrolls when R*s > H - B. It then has box_top = floor(f*D/R).
- Otherwise box_top = f*s, and the box sits over its lines.
- The window draws line L at y = (L - first_line)*s - phase. The viewport's first line is therefore always at box_top, so the box and the miniature cannot drift apart.

Inverse: seek = ceil(clamp(y - offset, 0, D) * R / D). Grab: inside the box, the offset is y - top. Outside, it is B/2, which is the click warp. The byte variant is the same thumb over [0, bytes] with first_line = UNKNOWN. Arithmetic is integer with `unsigned __int128` products. Full design and consumer notes: `docs/decisions/edit-cxho.8.md`.

## Files touched

- `src/minimap/minimap.h`: API and contract comment, appended.
- `src/minimap/minimap.c`: implementation, appended.
- `tests/minimap_test.c`: `window_mapping()` and helpers, called first in `main`.
- `fuzz/minimap_fuzz.c`: `window_op()` bounds op, run on every input before the existing body.
- `docs/decisions/edit-cxho.8.md`: new.
- `docs/worker-reports/edit-cxho.8-s24.md`: this file.

## Red run

Header plus stubs that return a zeroed window. Tests written first.

```
$ make -j4 build/san/tests/minimap_test && ASAN_OPTIONS=detect_leaks=1 build/san/tests/minimap_test; echo "exit $?"
display guard: real display :0 refused, using :99
minimap_test:543: w->box_height == b && w->box_top + w->box_height <= h
exit 1
```

The display guard line comes from the shared test prelude. minimap_test opens no display.

## Green run

```
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/minimap_test; echo "exit $?"
display guard: real display :0 refused, using :99
edit-cxho.8: fixed-scale window, constant box, round trip, clamps: ok
edit-8m3: character positions, spaces, tab stops, clipping, malloc guard: ok
edit-8m3: translucent viewport perimeter and half-open geometry: ok
edit-8m3: pixel marks, line spacing, viewport wash/edge, stride, source/allocation guards: ok
review 9: UI deferred fill reads no dispersed samples: ok
review 8: pending index paints current byte scrollbar and drag works: ok
edit-cxho.1: pending one-line map lights only its own row: ok
review 5: equal-size tab switch exact byte=1: ok
review 7: cached fill preserves typing/blink damage: ok
minimap_test: density, hit round trip, stale, malloc guard: ok
exit 0
```

One intermediate red came from the test itself, not the code. `memcmp` on `minimap_window` compared padding bytes. The test now compares field by field (`window_equal`).

What the tests cover. H = 1410 and v = 45 come from the research repro: (M) about 705 rows of 2 px, and (M) a 45-line view.
- **Constant box.** box_height == 90 for N in {0, 1, 44, 45, 46, 100, 660, 661, 704, 705, 706, 1000, 2e4, 2e5, 1e6, 1e7}.
- **Monotone thumb.** box_top never decreases. It is 0 at f = 0, and at f = R it is H - 90 when scrolling, R*2 otherwise.
- **Small documents.** When N*2 <= H the window does not scroll, first_line is 0 and box_top = f*2.
- **Round trip f→top→f.** Within ceil(R/D) everywhere, and exact when R <= D.
- **Round trip top→f→top.** Exact for every t in 0..D when R >= D, so the drag is 1:1. Otherwise within ceil(D/R).
- **Placement and EOF.** The viewport line sits exactly at box_top, and the window never shows past EOF.
- **Grab and warp.** Inside, outside and edge presses. A warp click lands at the box centre.
- **Clamps.** N = 0; v > N; H < box; H = 0; s = 0 gives ERR_ARG with out unchanged; NULL arguments; f > R; N = UINT64_MAX with overflow-prone v and f.
- **Byte variant.** Bounds, round trip, and exact 1:1 over t in 0..1320 at 10^9 bytes.
- **Allocation.** The malloc guard counts 0 allocations.

## Verification

```
$ make -j4 all; echo $?            -> 0   (gcc 13, -Werror; 0 diagnostics in the log)
$ build/tests/minimap_test          -> exit 0 (release)
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/minimap_test -> exit 0 (above)
```

Bounded fuzz. Stamp: BAT0 Not charging; 12:07:25 up 1 day 14:10, load 1.59 1.58 1.52.

```
$ ASAN_OPTIONS=detect_leaks=1 build/fuzz/minimap_fuzz -runs=200000 -max_len=64 -print_final_stats=1 -artifact_prefix=<scratch>/ <scratch>/mmcorpus; echo $?
Done 200000 runs in 146 second(s)
stat::number_of_executed_units: 200000
stat::peak_rss_mb:              81
exit 0
```

Why `-max_len=64` (P): the window op reads 49 input bytes, so 64 covers it fully. The cap also keeps the pre-existing body off the sampled path, where it crashes (see "Outside scope"). The old body reads count = (64-4)/2 = 30 lines of at most 2049 bytes each, so at most 30*2049 = 61470 bytes < MINIMAP_SMALL_BYTES = 65536. The run count of 200000 is a smoke bound I chose, not a budget.

Full `make -j4 check` on :95:

Stamp: BAT0 Not charging; 12:10:39 up 1 day 14:13, load 2.30 1.72 1.57.

```
$ DISPLAY=:95 XDG_RUNTIME_DIR=$(mktemp -d) make -j4 check; echo $?
...
check: 112 test binaries passed
== tools/test_replay_cli.sh
test_replay_cli: all passed
== tools/test_display_lock.sh contract
waiting for display :99 (held by 512363)
sh: display lock timeout: display :99 (held by 512363)
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
make check exit 0
```

The `:99` lock-timeout lines come from the display-lock contract script's own deliberate timeout case. No suite fell back from :95: the log has 0 "refused, using :99" lines.

## What remains

- edit-cxho.9: render the miniature of `[first_line, first_line + rows)` at y = (L - first_line)*s - phase. For bytes, walk `above` lines back from the viewport's first byte. This replaces the 256-sample striped path.
- edit-cxho.10: editor glue.
  - Call `minimap_window_lines` when the index is ready, else `minimap_window_bytes`.
  - On press, call `grab`. On motion, call `seek(y, offset)`, then scroll to the line, or to the byte resolved to a line start.
  - Choose the scale units, logical versus device px.
  - Pass N + v - 1 if the thumb should cover scroll-past-end. The editor clamps first_line to N - 1, not N - v.
  - Both points are in the decision doc.

## Outside scope (reported, not fixed)

1. **Pre-existing `fuzz/minimap_fuzz.c` crash.** It is unrelated to this bead.
   - Where: inputs over 64 KiB take the sampled path. The harness then calls `lineidx_seek_line(idx, &fast_src, UINT64_MAX, n)` once and asserts `lineidx_complete(idx)`, at `fuzz/minimap_fuzz.c:119` in this tree.
   - Why: `lineidx_seek_line` scans at most min(budget, 64 KiB) unindexed bytes per call (lineidx.h:167), and the harness builds up to 512*2049 bytes. One call cannot complete the index.
   - Found by: an unbounded-length run that crashed after 26107 execs (crash input 72 bytes, 0x58 0x00 0x00 0x58 0x59 0xff…).
   - Confirmed pre-existing: I rebuilt the harness in scratch with the new op disabled and the crash reproduced at the same line.
   - Likely fix: loop the seek until complete.
