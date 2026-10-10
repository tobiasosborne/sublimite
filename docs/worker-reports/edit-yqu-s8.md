# edit-yqu slice 1 (review P4-modules-2 s31, s32, s38, s39)

Power at edit time: Not charging [AC]. No bench was run (no timings claimed).

## Red / green
Red (tests/harness_test.c gained test_gate_verdict first; bench/harness.h lacked the API):
    tests/harness_test.c:251:44: error: 'BENCH_REFUSED' undeclared (first use in this function)
    tests/harness_test.c:254:43: error: 'BENCH_PASS' undeclared (first use in this function)
Green (after bench_judge / bench_exit_code / bench_gate_line / bench_gate_report added):
    harness_test: 10135 checks, 0 failures     (gcc release and clang ASan/UBSan)

## s31 DONE
Default exit is non-zero on MISS (1) and REFUSED (3); `--track` is the explicit opt-out. savectl_bench, findui_bench
(cancel and --count) now gate by default; `--gate` is still accepted as a no-op alias. scroll_bench already defaulted
to gating; its jump verdict now goes through the shared judge. Tested through bench_exit_code(verdict, track) with
crafted samples (miss -> nonzero, track -> 0).

## s32 DONE (verdict logic), samples NOT raised
bench_judge refuses a PASS when n < required_n (BENCH_INTERACTION_MIN_N = 10000, perf/01-perf-target.md section 4).
The line prints verdict=REFUSED, required_n, dropped and ci95 for p50 and p99. A miss stays a miss with few samples;
dropped samples remain a miss. Consequence: every row in scroll (5 jumps), savectl (128/7), findui cancel (64) and
count (<=64) now exits 3 by default, and `make bench` fails on those three until the sample counts are raised or the
.args files pass --track. I did not add .args files (decision for the coordinator). Rows with 10 000 steps
(G3z work proxy in scroll) keep their separate over-budget count.

## s38 DONE
findui_bench uses bench__power_from_status and bench__tag_from_power: Unknown -> "[unknown]", never "[bat]"
(asserted in test_gate_verdict via bench_gate_line with power "Unknown").

## s39 NOT DONE (not trivial)
mode_valid cannot be forwarded unconditionally: savectl_options carries only `mode` through baseline.mode, and
exists==0 with mode 0 means "unset" today, so always setting mode_valid would make default new targets 0000.
Needs a new savectl_options field (e.g. create_mode + create_mode_valid) plus a test; left for its own slice.

## Pre-existing failures (not mine, files untouched)
make check aborts at tests/editor_close_test (intentional RED lines 42, 72) so later binaries do not run under make;
I ran every other build/san/tests/*_test manually: all pass except editor_test (lines 614, 639: durable_sequence,
review_suite). Neither includes bench/harness.h. `make all` exits 0.

## Files
bench/harness.h, bench/savectl_bench.c, bench/findui_bench.c, bench/findui_count.h, bench/scroll_bench.c,
tests/harness_test.c, docs/worker-reports/edit-yqu-s8.md
