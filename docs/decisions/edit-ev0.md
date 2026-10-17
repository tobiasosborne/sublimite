# edit-ev0 — bounded default bench work

Use the existing editor flags to select serial typing only in the default
args file. Keep the full workload available as `build/bench/editor_bench
--track`, documented in [the bench README](../../bench/README.md). Keep timing
misses descriptive under the existing harness policy.

## Key-count derivation

Calibrate with `--track --no-idle --no-p4 --serial-only --keys=2`: **two keys
(P)** are the minimum closed alternating insert/backspace pair. This is a
work-cost probe, not qualified tail evidence. The printed injection-to-submit
G1 p99 values were **6,357,968 ns**, **77,295,022 ns** and **62,865,608 ns**
**(M)[AC]**, for null, raster and GL's raster fallback respectively. Take their
maximum as the provisional per-key cost. Power/load stamps:

```text
BEFORE
Not charging
 08:47:19 up 6 days, 13:35,  1 user,  load average: 55.96, 56.63, 47.33
AFTER
Not charging
 08:47:29 up 6 days, 13:35,  1 user,  load average: 56.85, 56.79, 47.48
```

Reuse the bench's existing settle watchdog interval as the estimated work
envelope for each serial row: **5,000,000,000 ns (E)**, the unchanged constant
in `settle`, equivalent to **5 s** by division by **1,000,000,000 ns/s (P)**.
The derived default is **64 keys (E)**:
`2 × floor(floor(5,000,000,000 / 77,295,022) / 2) = 64`.
The outer factor/divisor **two (P)** preserves complete insert/backspace pairs.
This is finite observation work, with a load-dependent duration; no new timer
or performance gate is added. The reduced percentiles remain descriptive.

The calibration's total wall was **10,197,160,018 ns (M)[AC]**, monotonic end
minus start, with the stamps above. For red evidence the existing default was
allowed **26 s (E)** before `timeout` stopped it:
`ceil((10,197,160,018 + 3 × 5,000,000,000) / 1,000,000,000) = 26`.
Here **three (P)** is the cardinality of the bench's existing backend list;
the calibration conservatively includes setup and its measured pair. This
cutoff is a reproduction bound only. It is not installed in `make bench`.

## Absent all-a fixture

Choose quick-mode skips for absent all-`a` input rather than generating it.
The generator does not produce this fixture. Generating dense input in the
default would add unmeasured disk work; skipping adds none. Print the affected
row name and file, including the cancellation row. Only `ENOENT` is optional
in quick mode: full mode, required inputs, other open errors, wrong sizes and
correctness failures retain their existing errors. Present all-`a` input still
runs normally. The regression uses a private empty file and then its absent
path, so it does not generate a large fixture.

Raw red/green commands, exit statuses, the attempted default run and its
provisional load/power-stamped total are in
[the worker report](../worker-reports/edit-ev0-s8.md).
The full default acceptance run and remaining display suites require a free
shared display window after repeated lock timeouts; the report names the
checks still required. No display-lane changes are made in this bead.
