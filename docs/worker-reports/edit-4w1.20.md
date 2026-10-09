1. Summary — completed the scoped amendment.

- [piece_bench.c](/home/tobias/Projects/editor/.wt/edit-4w1.20/bench/piece_bench.c): runtime target counting; separate cold TRACK and indexed G7j cells.
- [piece.h](/home/tobias/Projects/editor/.wt/edit-4w1.20/src/piece/piece.h:15): added the thread-creation prohibition, comment only.
- [bench_variant.sh](/home/tobias/Projects/editor/.wt/edit-4w1.20/tools/bench_variant.sh): cleans only its own outputs.
- [test_bench_variant.sh](/home/tobias/Projects/editor/.wt/edit-4w1.20/tools/test_bench_variant.sh): row-contract, trailing-empty-line, and preservation regressions.
- [P1.4a.md](/home/tobias/Projects/editor/.wt/edit-4w1.20/docs/decisions/P1.4a.md:151): recorded the amendment and verification evidence.

2. RED — before implementation:

```text
FAIL: full line-jump target/cell contract
FAIL: quick line-jump target/cell contract
FAIL: bench_variant deleted foreign worker output
test_bench_variant: 3 failure(s)
```

3. GREEN:

```text
PASS: full line-jump target exists, cold TRACK, indexed G7j, byte_to_line TRACK
PASS: quick line-jump target exists, cold TRACK, indexed G7j, byte_to_line TRACK
PASS: empty-last line-jump target exists, cold TRACK, indexed G7j, byte_to_line TRACK
PASS: bench_variant preserves foreign worker output
test_bench_variant: 0 failure(s)
```

Run with `sh WT/tools/test_bench_variant.sh` after building the bench.

4. Verification:

```text
selftest: ok
check: 14 test binaries passed
fuzz: 4 fuzzers built
Done 182031 runs in 31 second(s)
```

The passing suite used `env -u DISPLAY ASAN_OPTIONS=detect_leaks=0 make -C WT check`. ASan/UBSan remained enabled; live X11 tests skipped. The piece fuzzer reported no finding.

5. Benchmark output — power before every measurement: **Not charging, (M)[AC]**. Values below are p50/p99 in milliseconds.

| Cell | Quick | Full | Variant script | Gate/status |
|---|---:|---:|---:|---|
| Cold | 11.762/12.299 | 452.520/468.479 | 706.891/767.171 | TRACK |
| Indexed | 12.281/14.072 | 438.106/444.135 | 579.544/599.654 | ≤30/50 (G)[AC, prov]; PASS/MISS/MISS |
| Repeated line-to-byte | 13.502/14.353 | 468.449/1292.296 | 638.961/1082.077 | TRACK |
| Byte-to-line | 190.430/475.304 | 121.529/1156.547 | 165.455/854.486 | TRACK |

Full repeated-query phases reached their soft caps after 84 and 69 samples respectively. Full runs exited 1 for the indexed MISS. Complete outputs: [quick](/tmp/edit-4w1.20-quick-final.log), [full](/tmp/edit-4w1.20-full.log), [variant](/tmp/edit-4w1.20-variant.log).

6. Decisions — retained `line_jump_1e7` for compatibility. Runtime count is **8,947,842 lines (M)[AC]**, giving full target **8,053,057** and quick target **125,829**. No target cap. Cold latency is ungated; indexed latency uses G7j.

7. Out of scope — the stub’s full indexed MISS remains for synthesis. LeakSanitizer cannot run under sandbox ptrace, and the live display is inaccessible. The shared harness prints `power=unknown [AC]` for “Not charging”; explicit sysfs stamps supply the evidence. Timings are indicative under concurrent work; the coordinator should rerun gates quietly.

8. Makefile changes needed: none. `src/piece/piece.c` was untouched.