# edit-9jo — editor benchmark sample schedules

The P4 ingress-to-T5 and same-frame minimap rows now repeat the existing
hundred-tab switch workload until `BENCH_INTERACTION_MIN_N` is reached
(10000 samples (G)). Both sample arrays have that capacity; each append is
checked. Fixture contents, viewport, allocation guard, frame readiness endpoint,
and same-frame minimap freshness checks are unchanged. Reusing the prepared
editor makes this practical without repeating setup for each sample.

The null G11 CPU row is named
`editor_null_G11_process_cpu_synthetic_due_now`. It samples one real editor
poll/blink/caret/submit sequence per observation using process CPU time, including
worker CPU as before. Before each measured turn, the benchmark advances that
instance's private `next_blink` deadline to the current monotonic time and resets
`last_input` to prevent the idle timeout. It asserts exactly one additional blink
per sample. This is a synthetic zero-wait cadence, not default-period wakeup
latency or power evidence. No production clock, blink policy, or configuration
changes are introduced.

The original real-cadence idle window remains in the same invocation. Its wakeup
counts and elapsed time are captured before synthetic sampling; subsequent
quiet and unfocused observations retain their existing behavior. After the
synthetic population the benchmark disarms its private blink deadline. Existing
raster and GL CPU rows retain their real-cadence schedules and verdicts: their
MISS findings belong to edit-zzj.12 and are not addressed here.

`tests/harness_test.c` runs the actual null tab and idle rows in a child, parses
their emitted populations, and fails if any named finding has fewer than the
shared minimum. It permits timing misses on the loaded box but rejects negative
row errors or missing output. The test was observed failing before schedule
changes. The shared interaction threshold and parser are unchanged.

This worktree predates the split into `bench/editor_p4_bench.c`; P4 tab rows
are still in `bench/editor_bench.c`. Edits stay within its `tab_row` and
`idle_row` functions. The IPC launch helper and typing section are untouched.
The M0 record was read from `dd84a49:docs/bench/m0.md`, because that document
is absent in this checkout. Full red/green evidence and validation are in
[the worker report](../worker-reports/edit-9jo-s9.md).
