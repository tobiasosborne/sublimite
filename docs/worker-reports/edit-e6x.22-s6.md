Fixed §1–3, §5–8, §15 and §17. **§4 remains unresolved:** its safe fix requires an atomic batch-enqueue primitive in `src/work`; the proposal is documented.

Changed files:

- `src/raster/raster.c`, `raster_kernel.c`, `raster.h`: authentication, backend/visual checks, reliable publication, bounded cancellation, immutable fence snapshots, destination documentation and T6 preservation.
- `src/render/render.c`: glyph-work ceiling and removal of duplicate atlas validation.
- `tests/raster_test.c`: appended regressions; `fuzz/raster_fuzz.c`: cancellable-kernel coverage.
- [P2.5b.md](/home/tobias/Projects/editor/.wt/edit-e6x.22/docs/decisions/P2.5b.md) and [STATUS.md](/home/tobias/Projects/editor/.wt/edit-e6x.22/src/raster/STATUS.md): decisions, evidence and remaining work.

Red lines, before fixes; §4 intentionally remains red:

```text
raster_test:507: FAIL render_backend_event(&b, &e) == RENDER_ERR_UNSUPPORTED
raster_test:536: FAIL !raster_last_present(&b, NULL, NULL)
raster_test:560: FAIL rc == RENDER_ERR_UNSUPPORTED
raster_test:552: FAIL elapsed < UINT64_C(20000000)
raster_test:570: FAIL review_message_count == 1 && review_publish_calls == 4
raster_test:606: FAIL rc == RENDER_ERR_CAPACITY
raster_test:622: FAIL tail_untouched && began && review_stop_calls >= 3
raster_test:633: FAIL review_presents==1
raster_test:651: FAIL strstr(doc,"first pixel row of the requested cell row") != NULL
raster_test:681: FAIL t5.ns==300 && t6.ns==200 && t6.ust==55 && t6.msc==66
```

Final release and ASan/UBSan regression runs:

```text
P2.5b section 1: GREEN
P2.5b section 2: GREEN
P2.5b section 3: GREEN
P2.5b section 5: GREEN
P2.5b section 6: GREEN
P2.5b section 7: GREEN
P2.5b section 8: GREEN
P2.5b section 15: GREEN
P2.5b section 17: GREEN
```

Verification, all on `DISPLAY=:99 EDIT_DISPLAY=:99`:

```text
make all (final): exit=0
check: 29 test binaries passed
test_replay_cli: all passed
make check: exit=0
fuzz: 16 fuzzers built
make fuzz: exit=0
release live: exit=0
```

Raster fuzz, one requested 120-second run, (M)[AC], start load 6.46:

```text
Done 59067 runs in 121 second(s)
raster fuzz: exit=0
```

No mismatch or sanitizer finding. Leak detection was disabled as authorized.

Descriptor-cap self-check, TRACK (M)[AC], load 2.67:

```text
TYPING_BUDGET_TRACK cells=43200 glyphs=4096 samples=256 p50=102183 ns p99=181090 ns allocations=0 guard=1 (M)
```

Module bench ran **once**, `--track --quick --idle 0`. All rows below are TRACK (M)[AC], start load 2.46; after-run load 10.40. Times are ns:

```text
full_frame_warm_15px: p50=7232431 p99=11425929
typing_row_15px:     p50=1960403 p99=3319593
full_frame_warm_30px: p50=6708355 p99=14526699
typing_row_30px:     p50=3126174 p99=6261102
ALLOC typing_path_15px frames=300 total=0 guard=1
ALLOC typing_path_30px frames=300 total=0 guard=1
raster bench TRACK: exit=0
```

Open work: §4’s work primitive; compact layout bindings within the glyph ceiling; quiet-hardware calibration/gates and leak verification. Larger incremental tables require a frozen-contract proposal. P2.5c-owned tests and bench findings remain untouched; these TRACK measurements exclude the minimap.