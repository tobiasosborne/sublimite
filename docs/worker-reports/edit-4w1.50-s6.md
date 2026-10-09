Fixed §§10–13 and §16 in:

- `bench/work_bench.c`, `bench/scan_bench.c`
- `tests/scan_test.c`, new `fuzz/scan_fuzz.c`
- [P1.2c decision doc](/home/tobias/Projects/editor/.wt/edit-4w1.50/docs/decisions/P1.2c.md), `src/scan/STATUS.md`

No implementations or public headers changed.

Red → green evidence:

```text
§10 RED: FAIL delayed return rejected=0 CPU slice rejected=0
    GREEN: PASS delayed return rejected=1 CPU slice rejected=1

§11 RED: FAIL missing waits/refusal bounded=0 cleanup bounded=1
    GREEN: PASS missing waits/refusal bounded=1 cleanup bounded=1

§12 RED: FAIL slow p99 rejected
    GREEN: PASS slow p99 rejected; G6 basis/invalid/boundary checked

§13 RED: FAIL prefix result rejected
    GREEN: PASS prefix result rejected=1 wrong pointer=1 wrong count=1 correct accepted=1

§16 RED: exact-bound FAIL: p[n] is addressable n=1 off=0
    GREEN: scan_test: all checks passed
           (§13 large ranges; §16 exact bounds, 16 alignments, guarded pages)
```

Guarded tests caught injected one-byte overreads in all three APIs. No actual scan overread found.

Verification:

```text
make all final exit=0
check: 34 test binaries passed
test_replay_cli: all passed
make check (:99 outside sandbox) exit=0
fuzz: 21 fuzzers built
make fuzz exit=0
```

`make check` required outside-sandbox access to Xvfb `:99`; ASan/UBSan used `detect_leaks=0`.

Scan fuzz: **33,993 runs in 121 seconds**, requested 120-second campaign, no findings — (M)[AC], load1=5.91. No work fuzzer exists.

Exactly one TRACK run per bench:

| Row | p50 / p99 | Stamp |
|---|---:|---|
| Logical cancel | 261 / 706 ns | (M)[AC], load 7.79 |
| Worker CPU slice | 15,970 / 37,590 ns | (M)[AC], load 7.79 |
| Physical retirement | 159,404 / 206,131 ns | (M)[AC], load 7.79 |
| Submit/start | 9,132 / 22,822 ns | (M)[AC], load 7.79 |
| scan_count | 4.36 / 3.29 GB/s | (M)[AC], load 8.35 |
| nth newline | 5.25 / 4.93 GB/s | (M)[AC], load 8.35 |

Work CPU maximum: 249,882 ns; publication suppression passed. Work exit=0. Scan primitive and 16 GB/s basis thresholds missed; exit=1. These are loaded-box TRACK results; no reruns.

Outstanding: work-test deadlines are proposed in P1.2c.md because `tests/work_test.c` is outside the edit allowance. Quiet-box and end-to-end G6/G7 verification remain with the coordinator/module owners.