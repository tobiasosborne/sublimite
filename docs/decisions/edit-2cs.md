# edit-2cs — default bench dispatch

The default driver excludes the zygote runtime; see [P4.14.md](P4.14.md).
The driver remains buildable under `make all`, and `make bench-zygote` retains
an explicit stale experiment invocation. Structural failures remain fatal.

Default args select existing reduced workloads. No new sample-count parser,
allocation path, duration budget, or C constant is introduced. Full workloads
remain reachable by invoking the bench binaries directly without these args:

```
build/bench/editor_large_bench --track
build/bench/raster_bench --track
build/bench/savectl_bench --track
build/bench/scroll_bench --track
```

The piece matrix remains available through `tools/bench_variant.sh` unchanged.

## Counts and measured costs

All local measurements below checked BAT0 immediately before execution:
`Not charging`, hence (M)[AC]. Raw output lives under `build/edit-2cs/` and is
pasted into the worker report. The comparison threshold of **900 s** is
(M), supplied by the bead from session 10; it is not a newly chosen budget.
Extrapolations assume repeated sample cost and are explicitly (E), not deadlines.

* `savectl_bench.args`: `--track --probe`. The existing probe performs **one
  sample per row** (P: the minimum nonempty observation, implemented by the
  existing `probe ? 1u : BATCH_SAMPLES` branch). Measured population wall costs
  (M)[AC] were **62,326,457 ns** for the small row, **14,107,825,981 ns** for the
  warm large row, and **15,789,506,428 ns** for the cold large row. Derivation:
  each is the bench's `wall_ns / n`, with `n=1` from the printed population.
  Full populations use **10,000 samples** (existing harness interaction
  population; source `BENCH_INTERACTION_MIN_N`). Repeating the large-row
  observed population costs gives (E) **141,078.259810 s** warm and
  **157,895.064280 s** cold: `wall_ns × 10,000 / 1,000,000,000 ns/s` (P: unit
  conversion). Both exceed the supplied threshold. The only existing reduced
  population interface is the probe; use it rather than inventing another
  count or budget. The complete reduced invocation took **29,973,320,726 ns**
  (M)[AC], monotonic end minus start, including setup and correctness checks.
* `scroll_bench.args`: `--track --probe`. Existing probe population is **one
  unindexed jump** (P: minimum nonempty observation; existing `probe ? 1u :
  JUMPS`). Its measured cost was **563,320,501 ns** (M)[AC], printed by the
  bench's end-to-end jump timer. Full **10,000-jump** population (existing
  harness count) extrapolates to **5,633.205010 s** (E): `563,320,501 × 10,000 /
  1,000,000,000`, exceeding the supplied threshold. Retain the existing
  **10,000-step** work and displayed-identity probes (counts from `STEPS`);
  the whole reduced run measured **16,337,686,286 ns** (M)[AC], monotonic end
  minus start. Physical display cadence remains unsupported in this null run.
* `editor_large_bench.args`: `--attribute-find-floor`. The existing attribution
  mode runs **five samples for each of the flat and snapshot sources** (E:
  provisional reuse of `row_find_floor`'s existing `N`, pending the required
  local per-sample measurement). It checks equal result counts and reports
  both populations. The single-sample `--probe-three` alternative is unsuitable:
  `report_fg` treats a population shorter than the full interaction population
  as a structural error even in TRACK mode. The selected mode cannot be
  measured here because `required_fixtures` demands the absent sparse corpus
  before entering any attribution mode. This is an outstanding acceptance
  requirement, not measured count evidence.
* `raster_bench.args`: `--quick --track`. Existing quick mode reduces the frame
  population from **10,000 to 100**, with **10 warmups instead of 200** (E:
  provisional reuse of `run_size`'s existing quick branches, pending the
  required per-sample measurement). Other fixed diagnostic populations remain
  unchanged. The sandbox cannot open X, so no per-frame cost or completion
  bound is claimed. The coordinator must measure this selected workload on
  the supplied Xvfb display and confirm or revise its args.

The editor-large and raster defaults are therefore provisional reductions,
not fully measured selections. Do not close the bead's measurement acceptance
until the missing corpus and display evidence have been supplied. Bench source
edits and corpus repair were outside this worker's authorized files.
