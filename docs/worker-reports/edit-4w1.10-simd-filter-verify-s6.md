Continuation complete: builds, checks and fuzzing passed. The full TRACK benchmark missed the ordinary timing limits.

Added deterministic periodic/nonperiodic Two-Way cancellation regressions; retained the inherited production fix. Changed files:

- [Variant test](/home/tobias/Projects/editor/.wt/edit-4w1.10-simd-filter-verify/tests/find_simd_filter_verify_test.c:260)
- [Decision](/home/tobias/Projects/editor/.wt/edit-4w1.10-simd-filter-verify/docs/decisions/P1.10-simd-filter-verify.md)
- [STATUS](/home/tobias/Projects/editor/.wt/edit-4w1.10-simd-filter-verify/src/find/STATUS.md)

Production find sources and frozen files match the starting copies. Budget remains `V > 4S + 8n`, then permanent deterministic Two-Way fallback.

RED, temporarily reverting each inherited match-return correction; each exited 1:

```text
Two-Way cancel boundary case=periodic_match room=5 result=1 stopped=0
FAIL tests/find_simd_filter_verify_test.c:301: result==-1 && mt.stopped
FAIL tests/find_simd_filter_verify_test.c:331: two_way_cancel_boundaries()==0
Two-Way cancel boundary case=nonperiodic_match room=3 result=1 stopped=0
FAIL tests/find_simd_filter_verify_test.c:301: result==-1 && mt.stopped
FAIL tests/find_simd_filter_verify_test.c:331: two_way_cancel_boundaries()==0
```

GREEN, correction restored, Clang ASan/UBSan and GCC respectively:

```text
find_simd_filter_verify_test: ok
green_sanitizer_exit=0
find_simd_filter_verify_test: ok
green_release_exit=0
find_test: ok (frozen P1.10a)
frozen_release_exit=0
make_all_exit=0
check: 24 test binaries passed
test_replay_cli: all passed
make_check_x99_exit=0
fuzz: 12 fuzzers built
make_fuzz_exit=0
```

Check used existing Xvfb `:99` with socket access and `ASAN_OPTIONS=detect_leaks=0`; ASan/UBSan remained enabled.

Fuzz: (M)[AC], Charging, load 3.45, launch 12:42:59 +08:00. No crash or sanitizer finding:

```text
#1380831 DONE cov: 2346 ft: 12245 corp: 688/18Kb lim: 4096 exec/s: 4587 rss: 179Mb
Done 1380831 runs in 301 second(s)
find_fuzz_exit=0
```

Benchmarks each ran once; every correctness check passed. All timings **TRACK**, in ns; measured values are (M), gates (G).

Quick: (M)[AC], Charging, load 3.83, launch 12:48:26 +08:00:

```text
BENCH name=G6_ERROR n=3 p50=1894166 p99=2275644 ci95=[1755145,2275644] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6_newline n=3 p50=591630 p99=654286 ci95=[535871,654286] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6v_a31b n=3 p50=1454403 p99=1786108 ci95=[1083469,1786108] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6v_first_last_middle n=3 p50=2478359 p99=2877750 ci95=[1930524,2877750] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6v_first_last_early n=3 p50=1511099 p99=1950221 ci95=[1275409,1950221] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=periodic_dense n=3 p50=7071607 p99=8486805 ci95=[6893166,8486805] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=regex_prefix n=3 p50=6256469 p99=12644898 ci95=[6052813,12644898] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=regex_no_prefix n=3 p50=419191429 p99=428405968 ci95=[418625116,428405968] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6c_logical_TRACK n=3 p50=77 p99=155 ci95=[68,155] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6c_worker_return_TRACK n=3 p50=331 p99=3995 ci95=[270,3995] gate_p50=0 gate_p99=0 pass=1 power=[AC]
find_bench: PASS (quick TRACK only; full gates not tested)
bench_quick_exit=0
```

Full: (M)[AC], Charging, launch load 4.09 at 12:49:04 +08:00; observed load 7.86 at 13:27:31, Full [AC]:

```text
BENCH name=G6_ERROR n=31 p50=193871830 p99=393045078 ci95=[190263088,219034357] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
BENCH name=G6_newline n=31 p50=110269708 p99=159944659 ci95=[105229931,118410547] gate_p50=80000000 gate_p99=125000000 pass=0 power=[AC]
BENCH name=G6v_a31b n=31 p50=143954184 p99=207331407 ci95=[142619241,145725021] gate_p50=160000000 gate_p99=250000000 pass=1 power=[AC]
BENCH name=G6v_first_last_middle n=31 p50=130128522 p99=143445114 ci95=[128460908,133756056] gate_p50=160000000 gate_p99=250000000 pass=1 power=[AC]
BENCH name=G6v_first_last_early n=31 p50=130719536 p99=213778502 ci95=[128945666,206108793] gate_p50=160000000 gate_p99=250000000 pass=1 power=[AC]
BENCH name=periodic_dense n=31 p50=276974771 p99=321105408 ci95=[272900006,294000687] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=regex_prefix n=31 p50=1119887448 p99=2433384007 ci95=[1049766506,1258590641] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=regex_no_prefix n=31 p50=95373569303 p99=292110532783 ci95=[79285484874,126149603781] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6c_logical_TRACK n=31 p50=189 p99=225 ci95=[187,192] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6c_worker_return_TRACK n=31 p50=1240 p99=6569 ci95=[1114,1489] gate_p50=0 gate_p99=0 pass=1 power=[AC]
FIND cancel TRACK correctness=PASS target=(G)1/5ms logical; next_CPU_slice<=5ms; worker return is wall time (M)[AC]
find_bench: MISS
bench_full_exit=1
```

Current find `.text`: 24,414 B (M)[AC], Full, load 5.31, 13:49:56 +08:00.

Open: ordinary G6 reference limits missed on this shared box. Quiet-box gate verdict, variant selection, and leak-enabled coordinator check remain. No functional failure or fuzz crash remains.