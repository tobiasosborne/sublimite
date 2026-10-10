# edit-4w1.47 — lineidx UI slices and cancellation

Continues P1.6c WIP commits `0a1abb4` and `c208ce7`, rebased onto main.
Scope is `docs/reviews/lineidx-1.md` findings §6–11. Historical red evidence
from those sessions remains in [P1.6c.md](P1.6c.md); the final per-finding
evidence is in [the s8 report](../worker-reports/edit-4w1.47-s8.md).

## Decisions retained and verified

- Seek persists partial chunk counts and real line anchors. An unindexed
  synchronous call consumes at most its byte budget, additionally capped at
  64 KiB, 256 callbacks and 0.5 ms thread CPU between callbacks (G). A target
  already indexed uses a separate, equally sliced chunk query. Changing the
  target, editing, or independently advancing the prefix cannot reuse invalid
  counts. Exact offsets can be returned before a complete chunk is adopted.
- Bulk jump scanning runs on `WORK_BULK` over an immutable source. The worker
  stops at the requested newline and publishes its offset through a validated
  mailbox message. The request retains one source lease; cancellation, edit
  and replacement suppress its result. Request allocation and geometry-copy
  setup remain on the allocating path; this is not a typing-path API.
- Relative chunk lengths in fixed-pool block treaps replace absolute starts
  and suffix shifts. Summaries carry byte/newline, edited, non-ASCII and unbuilt
  counts. Edits detach deleted subtrees without sweeping their entries; retired
  blocks supply entries on demand. Block coalescing prevents capacity growing
  with edit count. Replacement admission is capped at 512 chunks (G), with
  refusal before cancellation/model mutation; larger replacements require an
  index recreated on the allocating path.
- Mailbox receivers only stage ranges. Poll examines at most four messages
  and adopts/traverses at most 64 entries, checking a 0.5 ms CPU deadline (G).
  Refresh finds the first edited chunk via summaries and resumes up to one
  chunk, 256 callbacks and the same CPU deadline (G). The source callback is
  indivisible and must honor the caller's CPU contract.
- Logical cancellation requests `work_cancel`, invalidates the application
  generation, unbinds the receiver and retires the lease before any adoption
  or cleanup. Physical retirement and release hooks run during maintenance.
  Cancellation never invokes a release hook. Worker checks occur before and
  after each callback and between 16 KiB scanner blocks (G). Partial cancelled
  counts are never adopted. CPU diagnostics are read only after the work
  completion acknowledgement.
- Queued destruction uses the already-landed work cancellation/removal and
  completion acknowledgement. A running callback or blocking I/O still has
  to return before synchronous destruction can release its source; the header
  states this lifetime limit rather than promising preemption.

## Session s8 test integration

The release allocator test now types 10,000 keys (G) and demands exactly zero
allocations. Its byte oracle and index storage are reserved before the guard;
the oracle uses `memmove`/`memcpy` during typing. The preceding red run showed
the old oracle's allocation inside the guard. This strengthens acceptance
evidence without changing production allocation behavior or adding globals.

`tests/minimap_test.c` and `tests/scroll_test.c` had setup assertions requiring
one bulk synchronous seek or refresh to complete. Scroll also explicitly
expected a one-byte seek to build a whole chunk, the behavior rejected by §6.
Those test setups now resume slices with finite loop bounds, and the one-byte
assertions require zero built chunks. All their minimap/scroll behavioral and
allocator assertions remain. These are migrations of the lineidx contract,
not other-module production fixes.

## Verification limits

The benchmark's new worker-seek row uses a controlled partial prefix and
checks an independent target-byte and viewport-cell oracle through null
viewport submit. Logical acknowledgement and worker CPU are separate rows
with executable nonzero limits. Measurements are loaded-box TRACK evidence;
their numerical gate comparisons do not establish a gate verdict. No variant
speedup is inferred from differently prepared workloads.

Real display/renderer integration, cold performance verdicts, legacy
`bench/scroll_bench.c` bulk-seek migration, and slicing other modules' source
loops are outside this bead. The legacy ordinary line-to-byte/byte-to-line
queries retain their one-chunk content-scan contract; use the resumable seek
API for fragmented UI seeks. No new production globals or other-module
production changes were made. LeakSanitizer remains for the coordinator.
