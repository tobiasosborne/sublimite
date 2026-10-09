Both beads are complete. Generation-filtered receive now carries lineidx results; atomic batch enqueue removes raster’s waiting rollback. Existing callers compile, and the review probes run by default.

Changed: `src/work/work.{h,c}`, `src/lineidx/lineidx.{h,c}`, `src/raster/raster.c`, all three named test files, `src/work/STATUS.md`, and [P1.8d.md](/home/tobias/Projects/editor/.wt/edit-4w1.51/docs/decisions/P1.8d.md).

RED — (M)[AC], `Not charging`, load1=15.91:
```text
tests/lineidx_test.c:346: FAIL lineidx_poll(x) == 0
tests/lineidx_test.c:347: FAIL !lineidx_complete(x)
lineidx_test: 2 failure(s)
work_test: receive selected=5 foreign=0
work_test: selective_receive FAIL
work_test: batch fail_at=3 queue_delta=2 handles_unchanged=0
work_test: atomic_batch FAIL
raster_test:1079: FAIL review_rollback_waits == 0
raster batch: fail_at=3 strips=4 UI_rollback_waits=925 retained_jobs=0
```

GREEN, final ASan/UBSan — (M)[AC], `Not charging`, load1=4.54:
```text
lineidx_test: ok
P2.5b section 4: GREEN
raster batch: fail_at=3 strips=4 UI_rollback_waits=0 retained_jobs=0
work_test: receive selected=2 foreign=3
work_test: batch fail_at=3 queue_delta=0 handles_unchanged=1
work_test: dormant_receive_cursor_wrap_order ok
work_test: ok
check: 43 test binaries passed
test_replay_cli: all passed
make check complete: exit 0
```

Build and concurrency results — all (M)[AC], `Not charging`:
```text
load1=4.23 make all / make fuzz final: exit 0
fuzz: 22 fuzzers built
load1=5.80 final release work: exit 0
work_test: new_api ui_allocations=0 guard_active=1
load1=5.80 final work TSan: exit 0
load1=6.04 lineidx TSan --review=13: exit 0
```

Fuzz campaigns — (M)[AC], load1=20.63, both exit zero:
```text
lineidx_fuzz: Done 10216 runs in 121 second(s)
raster_fuzz: Done 28190 runs in 121 second(s)
```

Representative TRACK bench rows; timings are ns (M), all [AC]:
```text
load1=6.09 work_cancel_logical p50=766 p99=1693
load1=6.09 work_submit_start_TRACK p50=9229 p99=51262
load1=6.84 TRACK_G7_index_1g_fixture_warm p50=334498652 p99=336145465
load1=6.84 TRACK_G7j_unindexed_viewport_90pct_fixture_warm p50=221098265 p99=298142174
load1=5.19 A_idle_full_frame_warm_15px_subset_TRACK p50=10201035 p99=19071256
load1=5.79 A_synthetic_all_queued_full_frame_warm_15px_subset_TRACK p50=13143864 p99=45860736
```

Each affected bench ran once and exited zero. Bench and fuzz campaigns preceded the final dormant-cursor wrap guard; final builds, full checks and work TSan include it. Complete rows and additional ordering red/green evidence are in P1.8d.

No scoped blocker remains. Leak checking and final performance verdicts remain with the coordinator. Existing raster acceptance limitations are documented and unchanged.