# Benchmark defaults

`make bench` uses each bench's `.args` file. Timing misses are printed against
the rows in [the goal sheet](../perf/01-perf-target.md) and do not fail the run.
Missing required inputs and correctness failures remain errors.

The editor default is a reduced serial typing probe across the existing
backends. Its key count comes from the local per-key calibration and arithmetic
in [the decision](../docs/decisions/edit-ev0.md). Its percentiles are descriptive;
the reduced population does not qualify the full interaction tail.

Run the full editor workload explicitly, including contention, idle and tab
rows, under the shared display lock:

```sh
ev0_runtime=$(mktemp -d)
trap 'rm -rf "$ev0_runtime"' EXIT
XDG_RUNTIME_DIR="$ev0_runtime" DISPLAY=:99 sh tools/with_display_lock.sh build/bench/editor_bench --track
```

The display identifier above is the existing shared Xvfb fixture. The full
population is unchanged; `--track` records timing misses without failing them.

Find's quick mode prints `SKIP` for the dense, adversarial and cancellation rows
when the optional all-`a` fixture is absent. It still measures the available
log, periodic and regex rows. Full mode requires the all-`a` fixture. The corpus
generator remains unchanged, so the default never writes that dense fixture.
