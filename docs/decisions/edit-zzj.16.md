# edit-zzj.16 — capacity admission and index repair

Scope: P1-1 sections 1 and 6 only. The Enter/brace indentation algorithm and
IPC drain remain with their separate workers.

Capacity admission uses the same lineidx geometry planner for check and edit.
Check may reclaim detached metadata but never cancels a build or changes live
geometry. Plain replacements check their complete interval before undo grouping
or deletion; individual deletes/inserts and whole undo/redo groups also check
before mutating the piece tree. Refusal is EDITOR_ERR_CAPACITY, preserving the
editor's existing negative-error/stopped-loop contract. The index planner repacks
one adjacent short chunk on each side with the touched interval, avoiding a new
per-character tail while retaining the existing bounded replacement limit.

Each editor buffer reserves one reusable lineidx job and compact geometry/result
arrays during open. Ordinary initial builds reuse that storage. Post-edit restart
owns an immutable piece snapshot obtained from the buffer's prepaid recycling
allocator, then copies geometry through bounded poll continuations before bulk
submission. Preparation is capped at 64 entries and checks a 0.5 ms CPU deadline
(G); no new libc allocation enters mutation or repair. Cancellation still unbinds
before adoption, and source release/storage reuse wait for physical retirement.
Failed work admission retains dirty state and is retried by maintenance.

Maintenance selects one eligible buffer per turn with a per-editor round-robin
cursor, including inactive buffers and staged initial-build results. It refreshes
one edited chunk, then restarts incomplete indexes. Legacy unreserved indexes or
snapshot exhaustion use the existing bounded seek with UINT64_MAX as its target:
an estimated line count must never truncate repair. The active replay checkpoint
is excluded until commit because its piece tree exposes provisional bytes.
A UI readiness query prevents preparation, staged adoption and completed
retirement from waiting for another external mailbox wakeup.

Exact line totals come from the completed index. large.c retains ownership of
estimated viewport numbering and its existing exact-publication transition;
there is no parallel editor estimate policy. Only the active buffer can request
a minimap frame. No new globals or worker service are introduced.

Prepaid storage remains optional for other lineidx consumers, and is included
once in memory accounting regardless of active/retiring state. Source ownership
with a release hook remains conservatively unknown under the ordinary start
contract. Giant replacements still receive capacity refusal; this bead does
not recreate their index on the typing path. Enter/brace prefix acceptance and
other editor review findings remain outside scope.

Validation and the pasted red/green evidence are in
[the session report](../worker-reports/edit-zzj.16-s9.md).
