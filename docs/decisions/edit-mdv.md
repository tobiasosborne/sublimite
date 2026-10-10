# edit-mdv session 9: savectl review fixes

The earlier session implemented P4-modules-2 review §§9–17. The session 8
identity comparison remains in force. Slice 3 implements §§19, 20, then the
standalone controller contract for §18. Actual editor frame wiring and its G8s
measurement belong to the editor integration bead; no controller-only gate
claim replaces that test.

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

## Slice 3: prepare handoff (§19)

Prepare and file-only writing use successive invocations of the same work
lease. A generation-validated prepare mailbox message publishes the durable
retained token. The receiver copies it, clears journal_leased, and releases an
atomic acknowledgement; the file invocation cannot proceed before receipt.
Mailbox saturation and waiting for receipt yield the bulk lane. File work
never accesses the journal instance: independent capture_base validates the
replacement file only. This permits append/pump/receive throughout a long
write, across the entire session. The token getter exposes a borrowed UI copy
after this handoff, even while file work is busy. Other saves/checkpoints must
preserve all in-flight retained BASE paths and SAVE metadata.

Finish is an explicit new exclusive lease. The host makes the private journal
quiescent and supplies a complete CURRENT checkpoint containing every accepted
post-cutoff edit, including other buffers, before calling savectl_finish.
Accepted edits while prepare owns the journal require bounded host staging;
returning ownership permits their journal append in order. File duration no
longer extends that staging interval. A regression kills the process while its
file worker is held at FILE_STEP_FSYNCED; an independently appended other-buffer
edit and view survive replay without orderly flush, while the target remains
its old generation. This tests process-crash protection, not power-loss timing.

## Slice 3: finish writeback recovery (§20)

Append/partial-write and checkpoint-directory failures may be cleared by
journal_retry off the typing path, followed by another savectl_finish. Failed
fdatasync/writeback can have lost previously returned unsynced batches;
journal_retry deliberately continues returning JOURNAL_IO. Never reappend the
already accepted edit or discard the retained token to escape this state.

savectl_recover_finish transfers an exclusive lease to a worker, rotates a
COMPLETE CURRENT-session checkpoint into a fresh inode, then calls
journal_save_finish with the retained token and saved replacement BASE. Every
other in-flight token's BASE/SAVE metadata must be retained in that checkpoint;
all buffers sharing the replaced target must have been rebased. Rotation and
finish can each fail: needs_finish and the token survive, and saved status is
not installed until reconciliation succeeds. This deliberately reuses the
existing journal APIs; no file/journal implementation or API change is needed.
The regression injects old-generation writeback loss, demonstrates ineffective
retry/ordinary finish, fails the first fresh rotation, then verifies successful
reconciliation, retained-file retirement, and both buffers' exact replay.

## Slice 3: bounded request and saving-frame contract (§18)

The editor wiring bead must maintain a published immutable session root using
bounded mutation bookkeeping and reserved storage. It must already contain
stable buffer/source/allocator leases and selected-buffer snapshot identity.
Save input cannot enumerate dirty buffers, copy their bytes, allocate a
checkpoint, or construct a fresh piece snapshot before claiming acknowledgement.
Pin that published root and one existing selected snapshot through bounded
reference operations; both name the same content cutoff. Publication and
retirement of that host root remain editor work; this controller does not
construct it secretly on input.

The exact host sequence is:

1. Capture the monotonic input receipt timestamp before request capture.
   Ensure the session journal is quiescent; enqueue admission must not block
   waiting for worker I/O. Populate savectl_request with the pinned snapshot,
   root lease/context, worker prepare/release callbacks and request_ns, then
   call savectl_save_request. A refusal leaves both references with the caller;
   an accepted request transfers them to the worker.
2. The prepare callback runs only on the bulk worker. Construct the COMPLETE
   checkpoint for the immutable root: every BASE, dirty content/delta, view,
   tabs/window and every retained token's BASE/SAVE metadata. Return the selected
   buffer's matching previous BASE. Retain callback output storage and all arena
   contexts until release; release runs on the worker after selected snapshot
   retirement, including validation/preparation/file failures.
3. Mark status damage immediately. Render savectl_get_model().status and tag
   that frame with model.request_id while saving_frame_pending. The model keeps
   status "saving" pending even if the worker completes or fails first. A busy
   backend preserves the pending request and retries frame submission without
   blocking input. Call savectl_status_frame_submitted ONLY after that exact
   saving frame is actually submitted successfully, with its monotonic submit
   timestamp. Old IDs, duplicate acknowledgements and times before input are
   rejected. No new save request may replace an unsubmitted acknowledgement.
4. Continue routing/ticking messages. Once journal_leased becomes false, append
   staged/new session edits in order and maintain normal journal pumping.
   Later acquire a complete CURRENT immutable checkpoint for savectl_finish;
   select savectl_recover_finish if writeback recovery requires a fresh inode.
5. Instrument G8s from input receipt BEFORE capture through successful backend
   saving-frame submission, using request_ns and saving_submitted_ns. The editor
   acknowledgement regression must include a large dirty session with substantial
   checkpoint payload and a busy backend, and must observe actual frame submission.
   The existing controller enqueue-only benchmark cannot establish G8s.

The controller regression uses a large synthetic session, blocks its bulk lane
before admission, and enforces no UI I/O or allocation for the new request call.
It proves preparation is off-path and that mailbox/tick/completion cannot supply
an acknowledgement before the explicit submitted-frame hook. Its synthetic
clock samples test protocol ordering, not latency. No src/editor files are
changed and no end-to-end displayed-frame percentile is claimed.
