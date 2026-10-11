# edit-lez — findui review fixes

Scope: P4-modules-2 §§21–30 (review section identifiers). The historical session
below left find core and editor internals untouched. The continuation described
at the end changes the internal find visitor; its frozen public header, editor
internals, work implementation and Makefile remain unchanged.

## Literal and whole-word counting

ASCII folding belongs to the panel literal visitor, not the regex transformer.
KMP carries state across immutable piece spans, preserving leftmost non-overlap
and retrying overlapping candidates after word-boundary rejection. One-byte
literals use SSE2 candidate/word masks and rank/select/popcount through the
existing bounded visitor helpers. Scratch and pattern storage are reserved at
initialization; no new globals or typing allocation. Case-sensitive ordinary
literals retain the existing core kernel. The historical implementation left
whole-word regex on the shared-budget next API; the continuation below replaces
that path with a persistent visitor.

## Publication and independent caches

Prefix and visible arrays each have their own bounded initialization capacity,
including the required (G)4096 first offsets independently of viewport position.
This increases reserved panel memory rather than compressing variable regex
ranges. Each argument slot reserves an outbox sized from those capacities.
Publish immediately when mailbox credit exists; after backpressure, collect only
bounded wanted ranges and the completion. Delivery retries with work_continue,
returning the sole bulk lane to its FIFO. No sleeping job retains the lane.
The retry continuation can consume CPU while the UI remains stalled; it does
not provide preemption of the counting stage itself or blocking bulk I/O.

## Viewport identity

Window refreshes retain completed count, prefix cache and selected ordinal.
Changes during an unfinished global count coalesce into a pending window refresh
without cancelling/restarting that count. One-byte literal window requests seek
directly to the requested span. Multi-byte literals and regex still scan from
source start to preserve exact non-overlap semantics; checkpointed bounded
window scans and immediate window priority remain outstanding (§25).

## Replacement pages and byte slices

Beyond-cache replace-all fetches ascending bounded pages from the original
immutable snapshot, applies each page right-to-left, then requests the preceding
page. A separate page array preserves the original prefix cache for empty/no-op
replacement. One explicit undo group spans every page. The implementation
rescans the immutable source per page; memory is bounded, but page-count times
source-length work remains a performance limitation.

Individual deletion slices are capped at (G)8192 bytes; replacement input is
already capped at (G)4096 bytes. An unfinished match yields MORE with zero
completed matches, and checks the deadline before insertion. Cancellation or
error closes the successful byte prefix as one undoable group. Admission budgets
worst-case span records for all slices before mutation. Regex admission uses
source size conservatively; precise matched-byte summaries could reduce false
LIMIT refusals. A mutation can still overrun wall time through OS scheduling or
underlying fixed piece work; no hard real-time claim is made.

## Host mutations

An optional public mutation adapter preflights host index/history/journal capacity
before the undo group opens, then owns every delete/insert plus its bookkeeping.
A result separates successful mutation from error, so a host failure after an edit
still invalidates the source and preserves its undoable prefix. Adapter tests
cover newline counts/revision, failed insertion after deletion, accepted journal
records, live-prefix recovery and truncated-record recovery. The standalone undo
fallback remains compatible. The opaque editor API has no external mutation
entry point; editor binding, lineidx/layout/history/tab/journal coordination and
integrated editor replay remain outstanding (§29). No editor internals changed.

## Full storage leases

Leased sources retain the complete snapshot storage (arena, allocator context,
nodes, original mapping), both for the panel owner and each physical worker
argument. Logical cancellation does not release worker storage. Public
source_retired checks all current, worker and deferred source owners.
findui_maintain or off-path disposal releases snapshots before their storage lease.
Typing service never reaps a leased final owner. Deferred source slots are bounded;
source replacement returns BUSY before exhausting them. Legacy source binding
requires full storage to outlive panel disposal. Hosts must bind leases and defer
eviction until acknowledgement; save snapshots require their own leases. Editor
buffer eviction/reload and save integration remain outstanding (§30).

## Validation interpretation

Bench variants were run consecutively on the loaded AC box, always TRACK.
The count bench checks initial, late and empty windows. --word compares ordinary
and whole-word endpoints over an anonymous (G)1 GiB a-space source. All measurements
and red/green output are in the worker report. No loaded single timing is a gate
pass/fail. LeakSanitizer is disabled in this sandbox; coordinator reruns it enabled.

## Trace-driven regex whole-word continuation

The worker regression traces repeated matcher entry and boundary reads after
its independent caches fill. The counted production copy also traces repeated
prefix-plan creation. Their runtime red transcripts justify pulling the regex
portion of review §22 under the triage rule; see
[continuation report](../worker-reports/edit-lez.md) for evidence and derivations.

`find_regex_word_visit` is additive in the internal `find/visit.h` integration
header. The frozen `find/find.h` and all previous entry points remain. Matching,
ASCII word-boundary filtering, accepted-result ordinals and publication now
share a request-wide traversal. A separate cached boundary reader avoids
interfering with anchored regex matching. Prefix preprocessing is retained
across candidates, together with its verifier/fallback accounting. Boundary
reads and rejected candidates consume the existing request-wide meter; no
budget is enlarged, no allocation or global state is added.

Accepted matches keep the leftmost non-overlap policy. Rejected matches retry
from their start advanced by a byte, preserving valid overlaps. Empty matches
advance in the same way, including source-end termination. Only accepted
matches reach bounded prefix/visible/selected publication, while exact visible
cardinality remains independent of cache capacity. Cancellation, source faults,
callback refusal and request exhaustion keep the existing cleared-result policy.
The panel's scan hook now marks matcher traversals; internal boundary reads are
metered by find, rather than invoking the panel hook separately.

The existing restarted anchored NFA remains; this change makes no general regex
linearity or displayed-latency claim. Prefix snapshot seeks still use the core
literal kernel. Late-window checkpointing and immediate window priority remain
incomplete under review §25. Editor mutation binding (§29) and buffer/save lease
integration (§30) remain with the dedicated editor worker. Its existing findui
host and lease interfaces are preserved for edit-73j.
