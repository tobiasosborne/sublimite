Implemented the scoped harness fixes. Full gate acceptance remains unmeasured where integrations or fixtures are missing.

Changed [bench/raster_bench.c](/home/tobias/Projects/editor/.wt/edit-e6x.23/bench/raster_bench.c), [tests/raster_test.c](/home/tobias/Projects/editor/.wt/edit-e6x.23/tests/raster_test.c), [P2.5c.md](/home/tobias/Projects/editor/.wt/edit-e6x.23/docs/decisions/P2.5c.md), and [raster/STATUS.md](/home/tobias/Projects/editor/.wt/edit-e6x.23/src/raster/STATUS.md). Backend implementations and frozen headers were untouched; the raster test’s file-end region was preserved.

Red runs:

```text
SELF-CHECK section=9 FAIL
SELF-CHECK section=10 FAIL
SELF-CHECK section=11 FAIL
SELF-CHECK section=12 FAIL
raster_test:287: FAIL cells[8].fg != initial_fg
raster_test:314: FAIL !edit_malloc_guard_active() || external_guard_enabled()
RED section=16 expected=2 actual=0
SELF-CHECK section=18 FAIL
```

Green runs:

```text
SELF-CHECK section=9 PASS
SELF-CHECK section=10 PASS
SELF-CHECK section=11 PASS
SELF-CHECK section=12 PASS
raster pixels: PASS full + 10 individual partial comparisons (disconnected/boundaries/unchanged rows)
raster law2: windows=10000 allocations=0 guard=1 scope=input->submit
gate missing-display exit=2 (expected 2)
SELF-CHECK section=18 PASS
```

Negative controls correctly failed:

```text
raster_test: 2952 window pixels differ
raster law2: FAIL frame=1000 allocations=1 scope=input->submit
```

Verification:

```text
make all exit=0
check: 29 test binaries passed
test_replay_cli: all passed
make check exit=0
fuzz: 16 fuzzers built
make fuzz exit=0
Done 374360 runs in 121 second(s)
raster_fuzz exit=0
```

Fuzz budget: 120 seconds; start stamp Full (M)[AC], load1=3.97. Sanitizer checks used `detect_leaks=0`; coordinator leak verification remains.

Ran the benchmark once as TRACK across both target resolutions, both atlas sizes, and all contention cases; exit 0. Selected lines, in nanoseconds:

```text
BENCH name=A_idle_full_frame_warm_15px_subset_TRACK n=100 p50=8987622 p99=13018111 ci95=[8748262,9350365] gate_p50=0 gate_p99=0 pass=1 evidence=(M)[AC] load1=7.21 verdict=TRACK
BENCH name=A_idle_full_frame_warm_30px_subset_TRACK n=100 p50=6771768 p99=9071991 ci95=[5344890,7291638] gate_p50=0 gate_p99=0 pass=1 evidence=(M)[AC] load1=5.51 verdict=TRACK
BENCH name=B_idle_full_frame_warm_15px_subset_TRACK n=100 p50=1979009 p99=3032682 ci95=[1925854,2046509] gate_p50=0 gate_p99=0 pass=1 evidence=(M)[AC] load1=5.20 verdict=TRACK
BENCH name=B_idle_full_frame_warm_30px_subset_TRACK n=100 p50=3716833 p99=4367652 ci95=[3614815,3783702] gate_p50=0 gate_p99=0 pass=1 evidence=(M)[AC] load1=11.57 verdict=TRACK
```

Open proposals are recorded per finding: actual minimap and index/find/save integration, the real B fixture, verified vblank diagnostics, and deterministic CPU completion transport. The allocation assertion currently covers 10,000 real X-backed submissions. G3/G3i subsets and Xvfb G3z explicitly remain SKIP/not measured; gate mode cannot report acceptance success.