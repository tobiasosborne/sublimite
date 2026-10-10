# edit-6pc — clipboard review fixes

Scope: docs/reviews/P2-1.md §1, §26, §27 and §28, implemented in that order.

The paste pointer keeps its existing public lifetime: its blob is retained until a later
paste completion replaces it or clipboard shutdown destroys it. Empty input queues do
not revoke a borrow. Admission can refuse an ownership replacement while the retained
paste prevents it from fitting; no copy or new lease API is needed.

Request admission sums the pending local, confirming, remote and deferred-failure
counts for each selection. Its existing MAX_WAITERS limit applies to the sum, rather
than independently to each state. Completion emission checks input-ring capacity.
A partially emitted local batch retains a blob reference; a remote batch retains its
receive state and blob. New local requests wait behind a partially emitted batch.
Failures/ownership notifications that cannot fit retain per-selection counts. No
later selection replaces the shared paste data while completion events are queued.

MULTIPLE saturation uses the review's bounded retirement option. If all job storage
is occupied and a new refusal shares a tuple with older requests, unfinished older
requests are failed in arrival order before the newest refusal. Already checked,
held completions retain their result. Requests with other tuples are retained.
The otherwise-valid full-capacity serve path uses the same refusal routine. This
avoids an extra refusal queue that could itself saturate; under saturation callers
can receive failure for older conversions instead of the newest failure overtaking
them. Attached INCR transfers are retired and outstanding replies discarded.

Budget admission checks current bookkeeping, performs at most one bounded mailbox
drain, then immediately returns PLAT_ERR_FAIL if it still cannot fit. Callers retry
after worker completion. The private clipboard work pool is aligned with
_Alignof(work_pool) and initialized in x11_clip_init, before runtime dispatch.
Initialization failure fails clipboard setup with cleanup. plat_clip_set retains its
documented synchronous-copy contract; the zero-copy setter still consumes its buffer
on failure. No new process globals or typing-path allocation were added.

Tests include clip.c in a dedicated translation unit to construct deterministic
ownership, ring and worker states without adding production test hooks. They use
real X transport exclusively on :99. Existing raw-peer clipboard integration tests
continue to exercise production dispatch. Full red/green output, final verification,
measurement stamps and limitations are in ../worker-reports/edit-6pc-s9.md.
