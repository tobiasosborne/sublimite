# edit-4w1.59 — worker snapshot retirement without typing locks

P1-1 §13. The parked-worker red regression proves the inherited synchronous
snapshot release holds a mutex required by typing. Use deferred physical
retirement, keeping the pool's compact representation and existing ownership
model. No other review finding is included.

## Ownership and synchronization

A snapshot's final worker owner atomically drops its reference, publishes the
header to a per-core Treiber stack, then drops its core reference. Publication
uses release ordering and collection uses acquire ordering. No producer reads
its header after publication, so owner cleanup may immediately reuse it. A
zero-reference header's former length word stores its queue link; its root,
original and ADD view remain retained. No header growth or queue allocation.

The tree or another active snapshot keeps the core alive after publication.
The live-owner flag is cleared before the tree drops its core owner; releases
after destruction select deferred retirement, including when the original
thread has exited and its identifier is reused. Store Linux pthread identity
as an integer token and compare only the current thread identity; no thread
API receives an expired pthread handle, even across a destruction race. The flag fits existing
core padding. If the publishing worker drops the last core owner, no UI
operation or other producer remains; that worker exclusively drains the queue before destroying
storage. A publisher parked before publication still holds its core owner,
including when the tree is destroyed. This removes the worker-held UI mutex
rather than replacing it with a spinning or retrying foreground path.

Only the owner (or exclusive final-core thread) touches pool occupancy, slab
lists and the ADD snapshot list. Existing recursive pool locks may remain for
owner operations; worker release with a live tree acquires neither piece lock.
Pointer/reference atomics must be always lock-free at compile time.

Owner maintenance detaches pending headers and decrements roots incrementally.
Zero-reference nodes use their dead ADD-high word as an intrusive work stack.
A step handles one header or one node, with at most the fixed fanout of child
references. Automatic owner endpoints consume 64 steps (G); explicit
`piece_reclaim(t, budget)` consumes the requested graph budget and at most
64 completed slabs (G). Backing-view pruning and sized allocator frees retain
their existing costs; this graph-work bound is not an end-to-end CPU deadline.
Node retirement reads tree structure only, so backing stores may be returned
once no physical snapshot header owns them. The tree's current cached header
and direct owner releases preserve synchronous contracts. Destruction drains
all deferred work and trims unused reserves before transferring the remaining
snapshot-only core ownership.

A logical worker release transfers physical ownership, rather than destroying
it. Pending storage remains charged until maintenance finishes. The worker
memory regression explicitly waits for that completion before comparing its
unchanged G10f bound, without using a content query. An idle-service caller can
use `piece_reclaim` until it returns zero; automatic edit/query/take batches
also provide progress. General editor idle-service wiring is outside this
piece-only change. The new declaration is an additive maintenance extension
in the module's one public header; existing API signatures and layouts stay.

## Measurement choice

Keep the original frozen competition bench unchanged. Add
`piece_reclaim_bench --quick` as a separate memory-only TRACK row. Its fixture
retires one or three snapshots while sampling insert/delete; it records the
first edit separately so a single blocked edit cannot vanish in percentiles.
The deterministic parked-worker test establishes lock independence; timing
observations on this loaded box cannot establish G1 input-to-present passage.

Back-to-back before/after measurements, 2026-10-10T04:50:12Z (M)[AC], power
Not charging, load averages 7.72 / 10.66 / 10.40 (M)[AC]. The before library is
the inherited WIP before the fix; both variants use the same bench implementation.
This final pair supersedes preliminary pairs collected before the owning-thread
lifetime guard and integer identity representation were settled. Additional
pairs followed those implementation changes; no unchanged variant was rerun
to pursue a timing gate.
Quick fixture: 20,000 pieces, 16 repetitions (G fixture).

| TRACK cell | Before p50 / p99, us (M)[AC] | After p50 / p99, us (M)[AC] |
|---|---:|---:|
| One worker, first edit | 1.170 / 435.013 | 1.076 / 17.092 |
| One worker, insert + delete | 0.256 / 228.138 | 0.256 / 15.568 |
| Three workers, first edit | 70.068 / 506.208 | 7.296 / 20.937 |
| Three workers, insert + delete | 0.271 / 298.426 | 0.267 / 17.431 |

Ordinary typing row, also paired in the same minute,
2026-10-10T04:50:12Z (M)[AC], power Not charging, load averages
7.72 / 10.66 / 10.40 (M)[AC]:

| Quick typing cell | Before (M)[AC] | After (M)[AC] |
|---|---:|---:|
| Insert p50 / p99, us | 0.045 / 0.444 | 0.083 / 0.477 |
| Delete p50 / p99, us | 0.108 / 0.357 | 0.149 / 0.732 |
| Batch p50 / p99, ms | 0.014 / 0.027 | 0.024 / 0.037 |
| Peak owned bytes | 1,118,280 | 1,118,312 |

Interpretation: the structural test proves the contention fix; the paired
TRACK samples are consistent with removal of worker-retirement tail stalls.
Ordinary insert/delete samples differ by fractions of a microsecond; batch
samples also vary. Scheduling and CPU state were uncontrolled, so these rows
do not separate added maintenance overhead from loaded-box variability.
Core ownership/queue fields add 32 bytes (M)[AC] in the release fixture;
leaf, branch, snapshot and slab geometry remain unchanged. These timing
comparisons are observations, not quiet-box or loaded-box gate verdicts.

Red/green output and final validation: `docs/worker-reports/edit-4w1.59-s8.md`.
