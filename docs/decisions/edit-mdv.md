# edit-mdv session 9: savectl review fixes

This session implements P4-modules-2 review §§9–17 in order. The session 8
identity comparison remains in force. §§18–20 remain open; this document does
not supersede their existing contracts or claim their gates.

Reload acquisition opens nonblocking, validates the descriptor type before
reading, and compares the descriptor with the canonical entry before and after
acquisition. FIFO replacement never requires a writer to release the job.
JOURNAL_BASE_CHANGED is classified as an external conflict while retaining its
journal diagnostic and any recovery token. File-only checks/recovery actions
do not clear a preceding journal error; a fresh save or journal finish updates
that diagnostic channel.

Content identity and revision serve different purposes. The host may report
current and saved undo-history identities through savectl_content_identity.
Identities must never be reused across branches; an evicted/unknown saved history
state is explicitly invalid and remains dirty. Every report still advances the
monotonic revision, including undo to clean, so an in-flight reload cannot erase
an edit. Save captures the content identity at acknowledgement and keeps that
cutoff across journal finish. Legacy savectl_modified remains conservative.

Reload uses an immutable private file-backed snapshot instead of file-size
anonymous allocation. A worker opens an unnamed O_TMPFILE in the target's parent,
tries reflink for the large-file policy, and otherwise streams through a bounded
scratch buffer. Descriptor and canonical generation validation brackets that
work. The read-only mapping refers to the private inode, never an externally
writable source inode. All trees and snapshots retain it through reference hooks.
The overlap is old tree/snapshot ownership plus a new mapping, bounded scratch,
and new metadata, rather than another anonymous copy of the original. The
reflink threshold defaults to the file module's threshold and is injectable.
This choice requires temporary-file support, write access and sufficient space
in the target parent; failure preserves the old tree and surfaces FILE_ERR_IO.
It does not pretend that disk snapshot creation is free or instantaneous.

Piece exposes a minimal owner-thread staged mapped constructor. It maintains
one unfinished node per level in a temporary allocator-owned construction
object, preflights only the next slice's pools, and
processes a capped chunk batch. There is no file-size pointer array to allocate
or initialize. Savectl keeps the partial tree hidden and installs only the
complete tree; BUSY between slices leaves output arguments unchanged. Allocation
failure sets FAILED/FILE_ERR_NOMEM, retires the backing on the worker, and enables
explicit recovery after retirement. Allocator free rolls back reclaiming pools;
paired mark/reset callbacks restore an exclusive non-reclaiming reservation.
The temporary object is freed on commit/abort; ordinary trees keep only its
pointer. Keeping the entire state in each tree inflated checkpoint copies and
failed the existing G10f memory test, so that layout was corrected before final
validation. That reservation must receive no unrelated allocations between mark and install
or rollback. Host input checks belong between construction calls. Final graph
cleanup is maintenance work and is not an input-to-submit operation.

File exports a retained source lease with the actual opened identity, original
mapping/descriptor and sticky fault service. Acquisition is CPU-only after full
attachment; validation runs on a worker and compares complete identity plus
fault epoch. Savectl retains the lease through destruction, so file_close does
not invalidate a running save. A changed detached original is conservatively
refused, including ctime changes from unlink/rename: arbitrary restored-mtime
writes cannot safely be distinguished from harmless detached-inode metadata
changes. Consequently repeated mapped saves after detachment may require reload.
Reload's private backing qualifies as immutable COPY for later saves. This is a
safe controller integration, not a weakening of the original identity guard.

Completion has a separately allocated result lease, reserved during controller
setup. Workers seal result/token storage before publishing a terminal mailbox
message. UI adoption occurs only after validated mailbox receipt; there is no
polling side channel. A full mailbox retains the sealed lease and uses work's
cooperative continuation to retry at the back of the bulk FIFO. The receiver is
bound to its physical handle and application generation, protecting it from
shared-pool drains. Tick selectively receives this controller's messages; the
host must also drain foreign traffic to free ring capacity. A worker touches no
result storage after successful publication. Destruction checks
work_handle_finished for the exact physical lease, rather than a reused slot's
busy flag, and unbinds before freeing the controller.

Actual editor G8s acknowledgement remains unresolved: checkpoint construction
and the submitted saving frame are outside the existing standalone controller
measurement. The session-wide journal lease still covers file writing, and
fresh-inode writeback recovery guidance still needs its dedicated fix. These
are the next findings in order, not accepted exceptions.
