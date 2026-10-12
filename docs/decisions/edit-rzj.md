# edit-rzj: recover orphan sessions behind the newest checkpoint

Discovery walks every unlocked, nonempty generated session journal in descending
mtime order, with pathname as a deterministic tie breaker. The newest session
keeps its active tab and chosen window size. Older sessions append their live
tabs and retain their closed tabs. Replay remains source-local: installation
assigns fresh editor buffer IDs, so independent sessions' equal wire IDs and
separate edits to the same target cannot overwrite each other.

The existing retirement pass still runs before discovery. The existing strict
replay, changed-base fallback, set-aside notices, and explicit-journal contract
are preserved. Each source lease is held through replay, installation, durable
merged checkpoint publication, and source retirement. Only buffers installed
from that source acquire new BASE identities; earlier recovered buffers keep
their validated generation.

The discovery cursor advances even when a source has no recoverable buffers or
cannot fit. Repeated directory scans need only stack state and the existing
lease descriptors; no new allocation or work enters the typing path. The
cursor deliberately leaves journals that become eligible after their position
has passed for the next launch.

Before installing a discovered session, check the configured live-tab,
closed-tab, and editor-buffer capacities. If the entire source does not fit,
leave its journal discoverable and show a status notice that recovery was
deferred and its journal retained. This prevents partial installation, dirty
closed-tab eviction, and the existing startup retry from hiding earlier
recovered sessions. There is no additional recovery budget. A closed buffer
needs a temporary live slot under the existing installation order (P: install
opens the buffer, then closes it into the retained-tab pool).

The dataloss regression synchronizes each child after its edit has been
published, starts the children serially while keeping both instances alive,
and sets the crashed journal's mtime to the POSIX epoch (P: zero seconds and
zero nanoseconds). This proves relative age without a delay. The original
campaign's successive launches cover initial restoration, its checkpoint,
and that checkpoint's replacement (P: three generations from these roles).
The single-live-slot variant forces deferral (P: one slot cannot hold the
crashed and surviving instances' tabs). A shared-target variant verifies that
both independent edits survive even when their paths are equal.

Integration note for edit-fi2: retirement is untouched; the candidate leasing
walk now lives in `discover_source`, and replay/retirement lives in
`restore_source`. Its candidate filtering and lease admission remain the
previous walk's policy, followed by the descending cursor check.
