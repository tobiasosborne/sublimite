# edit-ovu: scroll review fixes

Scope: P4.3b, `docs/reviews/P4-modules-2.md` findings §2–8. This session
implements the source-failure, foreground-slice, resident-source bridge and
seek-benchmark contract work. Renderer origin/overscan, wrapped visual-row
integration and displayed G3z remain open; this document grants no gate waiver.

Scroll state is committed only after the entire operation succeeds. Source
errors preserve it; completed index chunks may remain published, but partial
chunk counts are never treated as complete. Source callbacks returning zero
before EOF or a positive count with a NULL pointer are failures.

The adapter has an explicit caller-owned `scroll_resolver`, initialized outside
the typing path. It retains scan positions, newline counts, backward-block
phases and query seeds across slices. Each complete resolve/follow operation
shares a literal positive byte budget, at most 256 (G) source callbacks, and an
absolute monotonic wall deadline, defaulting to 0.5 ms (G). Zero selects the
64 KiB (G) default byte budget. An individual callback must itself be bounded;
a callback or OS descheduling can overrun the deadline. Each returned span is
capped at 4 KiB (G). Yielding preserves public scroll state and retains progress
only in the continuation and index. Input checks belong between slices.

Line seeking uses lineidx's bounded continuation. Byte relabelling obtains a
chunk seed through a metadata-only source callback, then counts that chunk's
suffix with the resumable adapter. The intentionally incomplete legacy result
is private and is never committed. This avoids changing lineidx's frozen API.
A proof-exhausted cursor follow waits for index publication and recomputes its
seed on retry. A changed navigation intent invalidates the old continuation;
a source edit requires explicit cancellation/zeroing before replacement.

A `scroll_resident` bridge provides two 64 KiB (G) caller-owned windows. Only
the bulk worker dereferences the immutable, possibly cold snapshot. A bounded
worker continuation copies and touches a whole window before sealing it in a
work mailbox. A failed partial window is discarded. The UI uses only adopted
window bytes; a missing window returns MORE with the viewport unchanged.
Backpressure yields the bulk lane instead of sleeping inside a job. Window
reuse requires physical handle completion even when its terminal message
arrives before worker return. Closing cancels and unbinds, then returns MORE
until physical completion. The caller retains the snapshot, pool and bridge
through that acknowledgement; the bridge does not own the source release hook.

The resident wrapper is the integration entry point for mapped sources. The
raw-source slice API requires resident/nonblocking bytes; a byte limit alone
cannot prevent a major storage fault. Existing editor code does not yet use
this scroll module; this bead does not claim installed editor wiring or full
cold-storage latency qualification. The mapped unit fixture drops its mapping
pages before navigation and verifies that raw spans execute only on the worker,
with zero (M)[AC] foreground major faults. This is not a disk-cache eviction
experiment. No global cache or allocation on the typing path was introduced.

The benchmark uses `lineidx_seek_start_owned`/`lineidx_seek_result` with its
immutable warm mapping. Pool/index setup is outside the timer; enqueue, wait,
mailbox adoption, bounded resolution, layout and correct null-backend submission
are inside. Timeout cancels; an independent cancellation check requires result
suppression. Sample counts and cadence-proxy semantics are unchanged for the
other worker's independent sampling/fuzz bead. TRACK measurements remain
observations on the loaded AC box, not a displayed G3z verdict. Exact commands,
red/green output and the remaining scope are in the worker report.
