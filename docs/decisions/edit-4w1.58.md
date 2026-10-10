# edit-4w1.58 — bounded find counts and fixture completion

Find's frozen public header is unchanged. An internal `find/visit.h` API counts
the whole source and emits only a caller-sized prefix, a bounded visible window
and one ordinal. Dense byte masks use fixed-work compiler popcount; offset
iteration stops at the fixed output cap. Mask rank/select lets late pages avoid
walking the preceding dense matches. The panel adopts the exact visible count
at completion, preserving overflow reporting without flooding its mailbox.
Whole-word filtering keeps its existing rejection/overlap policy.

Keep the ordered tagged Thompson implementation for its settled captures and
leftmost-longest semantics. Until a streaming replacement is built, allow at
most (G) `64 * (states + 1) * (source_bytes + 1)` NFA state/candidate units per
request (saturating at the integer maximum). `FIND_ERR_LIMIT` replaces a
quadratic unbounded result; unsuccessful or limited exact searches clear their
counts. A filtered consumer carries the budget through successful-but-rejected
matches too. Literal prefix scans are separately bounded by the production
literal kernel, and cancellation polling remains in all paths. This is a
state-work bound, not a wall-time service guarantee or a new capture policy.

Benchmark G6c enforces (G) 1/5 ms logical acknowledgement and a (G) 5 ms maximum
for every observed production-kernel polling CPU interval. Use a caller-owned
instrumented copy of the same source, with only work_should_stop redirected to
CPU metering. Wait for a non-entry production polling interval before requesting cancellation.
Include the cancellation-observing interval; physical cleanup is
a separate wall-time observation. Deliberate delayed values self-test verdicts.

Isolate threaded fixtures before creating their workers. Parent supervision
uses a monotonic deadline and kills/reaps a stalled fixture, preserving argument
ownership until process termination. Successful fixtures use normal exit so
registered cleanup and LeakSanitizer hooks still run; an atexit-marker regression
proves this without needing LeakSanitizer in the sandbox. Inner deadlines also cover per-sample
start, completion and retirement. Apply supervision to the frozen find test
body and both panel bench modes. Its existing semantic assertions stay intact.

Add the actual panel count endpoint as a G6 benchmark, using a new file mapping
per sample. Its existing range budget permits 4095 cached offsets plus one
visible offset; the benchmark verifies their union is the first 4096 offsets.
Default fixtures are read-only `/tmp/edit-corpus` inputs. An explicit
`--fixture` permits a supplemental worktree-local all-a file without mutating
shared fixtures. TRACK mode preserves timing comparisons; gate mode can fail.

Red/green transcripts, loaded back-to-back measurements, exact commands and
verification limitations are in `docs/worker-reports/edit-4w1.58-s8.md`.
