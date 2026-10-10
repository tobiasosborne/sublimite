# edit-zzj.14 — benchmark timestamp, contention and verdict decisions

G1 starts immediately before `editor_inject`, not in `on_ingress`: the public
API explicitly timestamps that callback when the editor drains input. Reserve
one timestamp record per measured sequence and keep dequeue, submit return,
T4 and containing frame ID separately. Coalesced keys all inherit their own
containing T4; hooks detect duplicate/missing/out-of-range attribution.
Submit return and dequeue latency are descriptive diagnostics; only injection
to T4 is the G1 gate.

The A fixtures select the atlas's advertised pixel size explicitly and verify
backend configured dimensions through the public render API. Runtime output
comes from those dimensions. G1 and G11 use the same fixture builder.

Serial measurements remain a comparison row. Queue scenarios inject their
first key, advance it until a frame is active, then inject the remaining keys
before draining frame completion. Native runs must observe arrivals during
an active frame; the immediate null backend is a headless reference. A delayed
headless backend regression proves sequence attribution across active frames.

Contention jobs borrow `render_backend.config.workers`, an existing public
render field: no editor internals or additional worker pool is needed. One
bounded index scan runs alone, or index scan, literal find and save write are
atomically queued on the editor bulk lane. Each continuation processes at most
64 KiB and rejoins the FIFO. Per-job progress and maximum thread CPU chunk time
are reported; no progress, failed work or exceeding 5 ms (G) is structural.
An immutable corpus mapping and unlinked bounded save spool are setup-only
resources and survive physical worker cancellation acknowledgement. These are
kernel contention workloads; durable save commit/rename/fsync is not asserted.

Use the existing shared gate judge/report unchanged. Default misses exit 1;
qualified insufficient-sample verdicts exit 3 when under the timing bounds.
The shared judge prioritizes timing misses over refusal, even for short runs;
its explicit TRACK opt-out also applies to refusal. Keep that settled behavior.
Separate structural checks from timing contributions and preserve exit 3 when
aggregating rows. A structural failure always exits 1, including under TRACK.
The args file is empty so ordinary `make bench` does not silently opt out.

`--file` supports review/reproduction fixtures without relabeling them as the
large-corpus acceptance fixture. `--serial-only` isolates the comparison row;
`--ingress-delay-ms` explicitly exercises ingress wait inclusion. The default
still uses `/tmp/edit-corpus/log_1g.txt`, and no inherited editor warmup,
allocation, or wake-policy defect is fixed by this bead.

All timestamp/sample buffers and bulk resources are reserved before measured
keys. No globals or typing-path allocations are added. Public red/green and
validation evidence: `docs/worker-reports/edit-zzj.14-s9.md`.

Typing-row assertions use one cleanup path. Cleanup disables measurement and
the allocation guard, cancels and physically joins each submitted bulk lease,
then closes the editor and releases mappings, journal scratch and sample
storage. Bulk teardown is idempotent. Structural failure must not create a
benchmark-owned worker lifetime error.

The harness test includes the benchmark translation unit with its entry point
renamed, so headless integration probes exercise the actual static fixture,
hooks and row verdict code. No production public API or test-hook globals are
introduced. Actual native failure evidence remains in the worker report.
