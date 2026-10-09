Completed. Recommendation: **reject adoption for now**. B reduced median code RSS, but its paired latency gain was inconclusive. Release-size Pareto front: **A/B**; C is dominated by B. No `winner.patch`.

Changed:

- Added [benchmark](/home/tobias/Projects/editor/.wt/edit-zzj.7/bench/linkorder_bench.c), [build/run script](/home/tobias/Projects/editor/.wt/edit-zzj.7/tools/linkorder_bench.sh), and `variants/P3.6/**`.
- Wrote [decision and evidence](/home/tobias/Projects/editor/.wt/edit-zzj.7/docs/decisions/P3.6.md).
- Updated [editor STATUS](/home/tobias/Projects/editor/.wt/edit-zzj.7/src/editor/STATUS.md). Production code and Makefile unchanged.

Red run:

```text
sh: 0: cannot open tools/linkorder_bench.sh: No such file
```

Green run and required checks:

```text
linkorder_contract: endpoint, percentile, ELF order, preload and variant isolation passed
make all: exit 0
check: 41 test binaries passed
test_replay_cli: all passed
fuzz: 21 fuzzers built
```

Editor fuzz smoke, **(M)[AC], load1=18.41**, no findings:

```text
Done 1273 runs in 11 second(s)
```

Bench: one interleaved series, **200 launches per variant (M)[AC]**, launch load1 **20.54–25.28**; all on Xvfb `:99`:

```text
BENCH A p50=166.220 p99=359.684 ms status=TRACK (M)[AC] load1=20.70
BENCH B p50=155.090 p99=355.510 ms status=TRACK (M)[AC] load1=20.70
BENCH C p50=163.530 p99=362.668 ms status=TRACK (M)[AC] load1=20.70
```

Open limitations: cold-cache rows skipped because they require root; real GPU/vblank effects unmeasured; page counts are RSS/residency proxies. ASan/UBSan passed with leak detection disabled; coordinator must rerun leaks enabled.