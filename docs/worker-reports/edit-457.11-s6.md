Implemented the Unicode cluster atlas and headless rendering tests. Covered corpus glyphs render without placeholders; the 10% layout regression verdict remains unresolved on this shared box.

Changed: `src/font/font.h` (additions), new `src/font/unicode.c`, new `tests/unicode_render_test.c`, `bench/font_bench.c`, `bench/layout_bench.c`, `fuzz/font_fuzz.c`, [P4.11.md](/home/tobias/Projects/editor/.wt/edit-457.11/docs/decisions/P4.11.md), and `src/font/STATUS.md`. Layout source, frozen headers, parser paths, and Makefile are unchanged.

The cache composes combining marks, packs two-cell CJK/monochrome emoji images, preserves immutable atlas rectangles, and caches missing glyphs. Discovery/loading runs on a work-pool worker. Invalid bytes retain one inverse `?` cell per byte.

Red run:

```text
unicode_render_test: FAIL covered e+U+0301 rendered '?' (no composed fallback atlas)
```

Green run (M)[AC], preceding load 5.98:

```text
unicode screenshot: computed expectation == scalar == SSE2, px=15; combining/wide/invalid/clipping passed
unicode corpus: covered=529389 uncovered=19307 clusters; no tofu for covered set; all viewport grids valid
unicode corpus layout/cold-cache mallocs: 0 (guard active)
unicode no-malloc: 0 allocations over 10000 cached lookups; no arena/page growth (guard active)
unicode screenshot: computed expectation == scalar == SSE2, px=30; combining/wide/invalid/clipping passed
unicode_render_test: all passed (headless)
```

The test enumerates the actual covered and uncovered scalar sets; the decision doc records this machine’s sets.

Build/check results:

```text
make all: exit 0 — gcc, strict warnings
make: Nothing to be done for 'all'.
check: 26 test binaries passed
test_replay_cli: all passed
fuzz: 14 fuzzers built
```

ASan/UBSan check passed on authorized `:99` outside the socket-restricted sandbox, with `detect_leaks=0`. Unicode-only font fuzzing (M)[AC], load 4.14:

```text
Done 35689 runs in 61 second(s)
```

No crashes, sanitizer findings, or invariant failures.

Before/after benches: ns p50/p99, (M)[AC]; baseline load 5.41, final load 4.19. All shared-box results are TRACK.

| Row | Before | After |
|---|---:|---:|
| font raster | 4725 / 6900 | 4912 / 5322 |
| CJK raster | 4516 / 7930 | 3761 / 6780 |
| ASCII 300 rows | 170741 / 300004 | 138755 / 251241 |
| Log 300 rows | 317682 / 606938 | 441989 / 479097 |
| Unicode 300 rows | 373159 / 668270 | 291378 / 517385 |
| Malformed 300 rows | 200011 / 236814 | 154841 / 274882 |

Final font TRACK lines, (M)[AC], load 4.19:

```text
BENCH name=font_fallback_discover n=1 ns=25145290 gate=none fontconfig=1
BENCH name=font_raster_cjk_4E2D n=20000 p50=3761 p99=6780 gate=none dropped=0
BENCH name=font_cluster_cold_TRACK n=3 p50=24215 p99=24599 gate=none dropped=0
BENCH name=font_cluster_cached_TRACK n=20000 p50=45 p99=48 gate=none dropped=0
```

Final layout TRACK lines, (M)[AC], load 4.19:

```text
BENCH name=layout_unicode.txt_360x300_TRACK n=2000 p50=291378 p99=517385
BENCH name=layout_malformed.txt_360x300_TRACK n=2000 p50=154841 p99=274882
BENCH name=layout_ascii_code.c_360x120_TRACK n=2000 p50=53339 p99=64079
BENCH name=layout_log_1g.txt_360x120_TRACK n=2000 p50=108048 p99=191403
BENCH name=layout_ascii_code.c_360x120_typing_row_TRACK n=2000 p50=1382 p99=1801
BENCH name=layout_oneline_before_checkpoint_TRACK n=1 p50=232239 p99=232239
checkpoint_build_TRACK ns=1077610444 entries=262145 reserved_bytes=4194336
BENCH name=layout_unicode.txt_360x300_font_cache_TRACK n=2000 p50=345021 p99=403526
BENCH name=layout_unicode.txt_360x120_typing_row_font_cache_TRACK n=2000 p50=2154 p99=4080
```

Font’s unchanged 50000 ns p50 and zero-allocation gates (G) pass. Layout bench retains ASCII/log gate misses against 150000 ns p50/p99 (G).

Open problems: log p50 increased 39.13% and malformed p99 increased 16.08% (M, derived), exceeding the 10% comparison limit despite unchanged layout source. Quiet coordinator measurement is required; benches were not rerun seeking better numbers. End-to-end G1 and large cold-cluster composition latency remain uncertified. LSan needs the coordinator’s enabled rerun.