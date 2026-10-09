Completed the fold-in and fixed repeated tree seeks in the long-needle snapshot verifier. Frozen `find.h` and `find_test.c` hashes are unchanged.

Changed files:

- [literal.c](/home/tobias/Projects/editor/.wt/edit-4w1.52/src/find/literal.c) and [find_int.h](/home/tobias/Projects/editor/.wt/edit-4w1.52/src/find/find_int.h): independent forward cursors and metered span traversal.
- [find_bestof_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.52/tests/find_bestof_test.c): uncovered losing-variant tests and design edges.
- [find_fuzz.c](/home/tobias/Projects/editor/.wt/edit-4w1.52/fuzz/find_fuzz.c): additive long-snapshot operation with independent KMP oracle.
- [P1.10b.md](/home/tobias/Projects/editor/.wt/edit-4w1.52/docs/decisions/P1.10b.md) and [STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.52/src/find/STATUS.md): comparison, results and verification instructions.

Red run: counts (M)[AC], `Not charging`, launch load 11.96; bounds (G). Exit 1:

```text
snapshot traversal seeks=4608 span_next=4608 bound_seeks=3 bound_spans=3072
FAIL tests/find_bestof_test.c:322: probe.seeks<=3 && probe.spans>0 && probe.spans<=3*spans
FAIL tests/find_bestof_test.c:384: snapshot_traversal(&t)==0
```

Green ASan/UBSan run: (M)[AC], `Not charging`, launch load 27.26:

```text
snapshot traversal: seeks=2 span_next=768 bound_seeks=3 bound_spans=3072
find_bestof_test: ok
find_simd_filter_verify_test: ok
find_test: ok (frozen P1.10a)
check: 42 test binaries passed
test_replay_cli: all passed
make_check_exit=0
```

GCC release also passed all find suites, including the frozen allocation checks.

```text
make_all_exit=0
fuzz: 21 fuzzers built
make_fuzz_exit=0
```

Fuzz: (M)[AC], `Not charging`, launch load 27.00; no mismatch, crash or sanitizer finding:

```text
Done 21541 runs in 301 second(s)
find_fuzz_exit=0
```

Benchmark ran once with existing corpus fixtures. All rows passed correctness. Times below are ns, (M)[AC], `Not charging`, launch load 27.71, **TRACK only**:

```text
BENCH name=G6_ERROR n=3 p50=5319790 p99=7572087 ci95=[5112983,7572087] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6_newline n=3 p50=5632193 p99=7190547 ci95=[3706871,7190547] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6v_a31b n=3 p50=3707324 p99=5966973 ci95=[2354322,5966973] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6v_first_last_middle n=3 p50=6804643 p99=39872760 ci95=[2162001,39872760] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6v_first_last_early n=3 p50=2354763 p99=2831665 ci95=[2155357,2831665] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=periodic_dense n=3 p50=44548096 p99=49597881 ci95=[18461956,49597881] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=regex_prefix n=3 p50=152088628 p99=160218307 ci95=[135580555,160218307] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=regex_no_prefix n=3 p50=3866305359 p99=6637684029 ci95=[3507249448,6637684029] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6c_logical_TRACK n=3 p50=503 p99=612 ci95=[285,612] gate_p50=0 gate_p99=0 pass=1 power=[AC]
BENCH name=G6c_worker_return_TRACK n=3 p50=1872 p99=27297 ci95=[1389,27297] gate_p50=0 gate_p99=0 pass=1 power=[AC]
find_bench: PASS (quick TRACK only; full gates not tested)
find_bench_exit=0
```

Open problems: no new functional findings. The initial sandbox XCB failure was resolved using the existing Xvfb `:99` socket. LeakSanitizer was disabled as permitted; its rerun and full gate verdicts remain coordinator-owned. The existing no-prefix regex path remains slow.