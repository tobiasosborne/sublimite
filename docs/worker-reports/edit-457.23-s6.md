Implemented P4.14b in `tools/zygote_bench.sh`, `bench/zygote_bench.c`, and `variants/P4.14/**`. Added guarded real-display opt-in, launch counts, strict native GL, separate normal-GL exec, driver identity, distinct readiness/fence/Present endpoints, idle RSS, and after-idle diagnostics. Updated variant STATUS and appended the exact coordinator command to [P4.14.md](/home/tobias/Projects/editor/.wt/edit-457.23/docs/decisions/P4.14.md:295).

Never ran on `:0` or set `EDIT_ALLOW_REAL_DISPLAY`.

Red run:
```text
AssertionError: zygote CLI contract: FAIL ['--build', '--real-display']: exit=1; missing build/libedit.a; run make all / make check first
```

Green runs:
```text
zygote CLI contract: PASS opt-in refusal, launch bounds, missing/unknown/conflicting arguments
zygote executable guard contract: PASS driver and A/B/C/D independently refuse missing permission
zygote endpoint contract: PASS CPU readiness, native fence/Present identity, dual display guard
zygote contract: PASS A/B/C interleaved, D normal GL exec, IPC wait, matching readiness/fence/Present, map/unmap, cleanup
First-frame audit GREEN: PASS 8/8 ordered rows; 2 per variant; strict A/B/C order then D
Summary audit: PASS all 20 percentile rows match raw nearest-rank samples
```

Build/check/fuzz:
```text
make all: PASS — GCC, exit 0
check: 47 test binaries passed
test_replay_cli: all passed
fuzz: 24 fuzzers built
Done 234894 runs in 11 second(s)
```
IPC fuzz smoke: no findings, `(M)[AC]`, `Not charging`, pre-run load1 `8.67`. ASan/UBSan harness self-check passed; LSan was disabled.

Final `:99` smoke, two observations per variant: all values below `(M)[AC]`, `Not charging`, summary load1 `7.49`, **TRACK**. Times are milliseconds, p50/p99.

| Variant | Exec→map | Exec→first-present request |
|---|---:|---:|
| A normal CPU | 21.028 / 21.332 | 132.533 / 135.546 |
| B warm GL probe, CPU fallback | 4.097 / 5.098 | 4.871 / 6.086 |
| C warm CPU | 4.448 / 4.458 | 5.015 / 5.573 |
| D normal GL probe, CPU fallback | 1865.527 / 1904.972 | 1983.185 / 2009.832 |

Idle RSS: B `155908 KiB`, C `55472 KiB`, `(M)[AC]`, load1 `7.49`. Raw stamped rows and verification are in [p4.14b-verification.txt](/home/tobias/Projects/editor/.wt/edit-457.23/variants/P4.14/p4.14b-verification.txt).

Open limitations: native GL remains unsupported on Xvfb and awaits the coordinator’s announced run. Full CLI file/journal startup, GPU power/owned graphics, on-glass timing, focused blink, and leak-on verification remain pending. One overlapping-build run was excluded; final release and sanitizer runs were sequential.