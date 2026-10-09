Implemented the assigned fixes. **`make check` remains blocked by an out-of-scope journal-test capacity assertion**; its required update is documented in [P1.8c.md](/home/tobias/Projects/editor/.wt/edit-4w1.49/docs/decisions/P1.8c.md).

Changed `src/work/{work.c,work.h,STATUS.md}`, `tests/work_test.c`, and the decision doc. Alignment-only caller edits cover nine allocation sites in:

- `bench/font_bench.c`, `bench/layout_bench.c`, `bench/raster_bench.c`
- `tests/layout_test.c`, `tests/unicode_render_test.c`
- `fuzz/layout_fuzz.c`, `src/editor/open.c`

Red/green evidence follows. §§15/17 use temporary mutants to prove the new tests enforce the existing contracts.

```text
RED   work_test: section1 callbacks=3 nested=1 delivered=2 pending=4294967295
GREEN work_test: section1 callbacks=2 nested=0 delivered=2 pending=0

RED   work_test: section2 unaligned_callers=5
RED   work_test: section2 unaligned_callers=1
RED   work_test: section2 unaligned_callers=2
GREEN work_test: section2 unaligned_callers=0

RED   work_test: section3 old=2 replacement=2 alias=1
GREEN work_test: section3 old=2 replacement=3 alias=0

RED   work_test: section4 queued_busy=1 queue_count=1
GREEN work_test: section4 queued_busy=0 queue_count=0

RED   work_test: section5 destroyed_mutex_locks=1
GREEN work_test: section5 destroyed_mutex_locks=0

RED   work_test: section6 stage=3 rc=-1 thread_attempts=1 bad_destroy=1
GREEN work_test: section6 stage=3 rc=-1 thread_attempts=0 bad_destroy=0

RED   work_test: section7 cancelled_queue=63 raster_epoch=0
GREEN work_test: section7 cancelled_queue=0 raster_epoch=2
RED   work_test: section7 saturated_bulk=63 raster_epoch=0
GREEN work_test: section7 saturated_bulk=54 raster_epoch=3

RED   work_test: section8 slice_delivered=256 continuation_ready=0
GREEN work_test: section8 slice_delivered=1 continuation_ready=1

RED   WARNING: ThreadSanitizer: data race
      (both tail-relaxed and head-relaxed mutants)
GREEN work_test: section15 live_ordering ok
GREEN work_test: section15 full_wrap ok
GREEN work_test: section15 state_machine ok

RED   work_test: section17 guard_active=1 calibration=1 ui_allocations=384 closed_allocations=1
GREEN work_test: section17 guard_active=1 calibration=1 ui_allocations=0 closed_allocations=0

RED   work_test: section18 timestamp_nonzero=0
GREEN work_test: section18 timestamp_nonzero=1
```

Verification used `DISPLAY=:99 EDIT_DISPLAY=:99`:

```text
make all: exit 0
make check: exit 2
journal_test:935 FAIL submitted==WORK_MAX_JOBS-2
CHECK_REST: 24/24 sanitizer binaries passed; replay_cli exit=0
make fuzz: exit 0 — fuzz: 20 fuzzers built
work_test: ok — release, ASan/UBSan, and TSan
```

No work-module fuzzer exists, so the conditional 120-second campaign was inapplicable. Corpus unchanged.

One bench run, **TRACK (M)[AC], battery “Not charging”, load1=9.61**; times are ns:

```text
BENCH name=work_cancel n=1000 p50=5482 p99=14948 ci95=[5142,5774] gate_p50=1000000 gate_p99=5000000 pass=1 power=[AC]
BENCH name=work_submit_start n=1000 p50=7407 p99=47258 ci95=[7371,7445] gate_p50=0 gate_p99=0 pass=1 power=[AC]
```

Open work: the journal owner must update its bulk-capacity assertion for the raster reservation. Leak checking remains for the coordinator; ASan used `detect_leaks=0`. Frozen headers and `bench/work_bench.c` were untouched.