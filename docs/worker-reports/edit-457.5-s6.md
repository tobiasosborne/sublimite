Implemented the standalone minimap: exact small-file density, bounded large-file sampling, viewport band, click/drag mapping, staleness reporting, and allocation-free fills.

Changed files:

- [minimap.h](/home/tobias/Projects/editor/.wt/edit-457.5/src/minimap/minimap.h), [minimap.c](/home/tobias/Projects/editor/.wt/edit-457.5/src/minimap/minimap.c), [STATUS.md](/home/tobias/Projects/editor/.wt/edit-457.5/src/minimap/STATUS.md)
- [tests/minimap_test.c](/home/tobias/Projects/editor/.wt/edit-457.5/tests/minimap_test.c)
- [bench/minimap_bench.c](/home/tobias/Projects/editor/.wt/edit-457.5/bench/minimap_bench.c)
- [fuzz/minimap_fuzz.c](/home/tobias/Projects/editor/.wt/edit-457.5/fuzz/minimap_fuzz.c)
- [P4.5.md](/home/tobias/Projects/editor/.wt/edit-457.5/docs/decisions/P4.5.md), including dependency change, integration contract, and accessor proposal.

Red run:
```text
minimap_test:43: minimap_init(&m, rows, 64) == 0
```

Green runs:
```text
minimap_test: density, hit round trip, stale, malloc guard: ok
make all exit=0
check: 29 test binaries passed
test_replay_cli: all passed
make check exit=0
fuzz: 16 fuzzers built
make fuzz exit=0
```

Final fuzz: **19,823 runs in 301 seconds (M)[AC], load1=9.04**, exit=0; no ASan/UBSan findings. Release guards verified zero fill allocations.

Benchmark lines, shared-box **TRACK**:
```text
(M)[AC] load1=12.67 small warm      p50/p99=0.063893/0.085377 ms PASS
(M)[AC] load1=12.67 small after edit p50/p99=0.125443/0.131373 ms PASS
(M)[AC] load1=12.67 log_1g warm      p50/p99=0.076031/0.088010 ms PASS
(M)[AC] load1=12.67 log_1g after edit p50/p99=0.176629/0.189800 ms PASS
(G) p99 <=0.5 ms; minimap_bench exit=0
```

Both stale-after-edit/refill-reference checks passed.

Open verification: coordinator quiet-box verdict, deferred editor integration/combined G3 measurement, and LeakSanitizer rerun. Existing live raster/X11 cases skipped because sandbox Xvfb sockets were unavailable.