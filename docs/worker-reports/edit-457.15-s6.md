Completed [P4.14 decision](/home/tobias/Projects/editor/.wt/edit-457.15/docs/decisions/P4.14.md). Recommendation: **needs a real-display run; do not adopt B yet**. No clear production winner, so no `winner.patch`.

Changed: `variants/P4.14/**`, `bench/zygote_bench.c`, `tools/zygote_bench.sh`, and the decision doc. Production sources and Makefile unchanged.

Each variant completed 200 interleaved launches. Times below are p50/p99 milliseconds, **(M)[AC], Not charging, load1=26.24–28.10; summary stamp=26.24**, TRACK only.

| Variant | Exec→map requested | Exec→first-present request | Idle RSS |
|---|---:|---:|---:|
| A normal CPU exec | 34.631 / 96.776 | 214.555 / 431.763 | Process exits |
| B GL-preferred, CPU fallback | 6.350 / 20.517 | 8.860 / 29.194 | 155,952 KiB |
| C warm CPU | 6.526 / 22.462 | 8.243 / 26.865 | 55,536 KiB |

Both warm servers observed **zero CPU ticks and context switches over 12 seconds (M)[AC], load1=26.24**. B’s native GL initialization was unsupported on Xvfb; its measured rows are CPU fallback.

Red runs:

```text
zygote contract: FAIL missing executable variants/P4.14/a/launch
First-frame audit RED: FAIL 307/472 complete rows have ready after first present
zygote contract: FAIL cleanup private runtime variant=b
zygote contract: FAIL cleanup private runtime variant=c
```

Green runs:

```text
zygote contract: PASS A/B/C exec, IPC wait, real map/present completion, unmap, repeated warm launch
First-frame audit GREEN: PASS 600/600 ordered rows; 200 per variant; strict A/B/C order
```

Verification:

```text
make all: exit 0
check: 41 test binaries passed
test_replay_cli: all passed
fuzz: 21 fuzzers built
Done 860988 runs in 31 second(s)
```

IPC fuzz exited zero without sanitizer findings, **(M)[AC], Not charging, pre-run load1=26.33**. Release and ASan/UBSan harness checks passed; LSan was disabled as instructed.

Remaining: native GL measurements and readiness validation, real GPU power/vblank effects, exact G10 ownership accounting, and focused G11 blink measurements. Cold-cache rows were skipped because they require root. No real-display run was performed.