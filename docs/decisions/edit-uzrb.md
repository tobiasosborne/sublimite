# edit-uzrb: save admission while another tab is loading

The save session graph omits buffers with `pending_open`, `placeholder`, or
`derived` set. Its admission count and live ranks now use that same population:
visible loading tabs contribute neither a leaf nor a rank. The existing
revision check still refuses unpublished text. Initial publication refreshes
metadata so adoption inserts an inactive loading tab at its actual position
and shifts the other ranks together. This uses the existing prepaid graph;
the request path adds no allocation.

Explicit flush/exit checkpoints retain their existing pending-open BASE and
tab records, as required by `editor_checkpoint_test`'s pending recovery cases.
Only the save graph's membership and ranks change.

The regression opens the brief's sparse GiB file, switches back to a dirty
small file, and requests Ctrl+S while COPY acquisition is queued behind a held
bulk worker. A raster worker supplies the prefix. Releasing the worker proves
the accepted request writes the right bytes; another edit/save checks adoption.
Reordering the loading tab before the small tab also exercises rank compaction.
All cleanup paths remove the sparse fixture.

Numeric derivations: (P) the GiB length reuses `FM4_GIB`, the brief's binary
GiB; COPY threshold is that length plus the next byte. Tab indices are the
ordinals of the small/large pair; geometry reuses the existing fixture's status
width and tab/text/status rows. (E) watchdogs reuse `EDITOR_IDLE_NS`; no new
budget or timing target is introduced.

Landing onto main (after edit-3f2k): the worker wrote a local
`recorded()`/`recorded_count()` pair because `editor_recorded_rank` was not yet
on its base. At landing there is one mapping: `editor_recorded_rank`
(checkpoint.c) now skips `pending_open` tabs too, through the one predicate
`editor_tab_recorded` (private.h). Session publication, metadata ranks and
save admission all use it; the local helpers are gone. The UI checkpoint's
`recorded_tabs` is unchanged and keeps pending-open tabs (their BASE is in
that checkpoint). Closed leaves keep edit-3f2k's closed-stack index.
