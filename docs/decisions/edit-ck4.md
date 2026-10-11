# edit-ck4 — journal/recovery integration decisions

Scope: P4-3 §§3,4,5,6,8,7,10,32,33, in that order. Current result is partial;
[the worker report](../worker-reports/edit-ck4-s9.md) is the acceptance record.
The initial session-root/startup gaps are addressed by the continuation below.

## BASE generation schema

Keep prefix checksum semantics and path offset stable. BASE v2 uses the high
bit of encoded prefix_len as a schema discriminator and appends little-endian
ctime after the NUL-terminated path. Empty BASE retains its legacy encoding.
All layout constants are wire contracts (G), not timing measurements. Decode
removes the discriminator from the public prefix length. File recovery compares
ctime before delivering BASE or edit callbacks; old named generations without
ctime conflict on current Linux filesystems instead of applying their deltas to
an unproven original. Replay preserves the conflict artifact.

Expose journal_encode_base so editor checkpoints use the same schema as save
prepare/finish. Savectl's test/fuzz/bench producers also carry ctime; no benchmark
workload, gate or timing policy changes. Retained originals have their own ctime
identity and preserve full metadata barriers.

## File API gap and normal BASE acquisition

File already acquires a canonical path, complete file_id and owned prefix from
one validated opened descriptor on its nonblocking worker path, but did not
expose that immutable prefix identity. Add file_prefix_identity as a CPU-only
borrowed getter available after successful prefix publication, before close.
This is the only file implementation change. journal_base_from_prefix consumes
that identity and the acquisition prefix before mapped attachment retires the
prefix allocation. No separate foreground journal path discovery/open is needed.
Standalone journal capture uses O_NONBLOCK so it rejects FIFO descriptors before
reading. Resumable editor/IPC request preparation is still missing; this change
does not turn the existing blocking setup state machine into an asynchronous API.

## Source resolution

Keep rebases the retained tree against the accepted disk BASE through a complete
session checkpoint and marks its history conservatively dirty. Resume only after
checkpoint publication; keep the source suspended on failure.

Reload substitutes the editor-owned buffer/undo/view leases within the existing
logical tab. Tabs currently provide no replacement-lease API, so the editor
updates only these borrowed-resource fields in its owned tab storage; it does
not change tab identity/order/MRU bookkeeping. Old resources retire only after
durable checkpoint publication. This avoids retaining an obsolete BASE as a
closed conflicting generation and preserves wait membership. The explicit view
clamp is to EOF; wider navigation-boundary findings are outside this bead.
Pending-remainder COPY replacements currently remain suspended and need a future
asynchronous replacement state. Multiple stale named BASEs likewise require a
staged session-wide source-choice plan before complete checkpoint publication.

## Durability execution and admission

Reserve a per-editor private journal work_pool/eventfd instead of sharing the
bulk execution lane. Route its completions through the existing owner mailbox
router. Pump independently of frame dirtiness, including credit-refused work.

Use journal_insert_step and protected append/delete admission for complete
records, including eventual PAD, relative to received durable fences. Setup
registration streams initial unsaved contents using the same credit with
explicit off-path flushes. Normal metadata shares that credit. Hold frame
publication while content/metadata is incomplete.

Large undo/redo journal ranges borrow the committed UI tree, park subsequent
input, and defer buffer retirement until the range drains. Taking a new snapshot
after undo commit introduced a failure point outside undo's rollback checkpoint;
the existing allocator sweep caught it. A borrowed tree needs no post-commit
allocation and remains stable under the parked-input/retained-arena contract.
No new typing-path malloc is introduced. The dedicated pool and byte credit do
not prove an unconditional wall-clock device/syscall bound.

## Healthy metadata and target association

Coalesce accepted VIEW/TABS/WINDOW values in reserved storage at ordinary frame
submission and explicit flush. Cache only successful appends; credit refusal
keeps the metadata pending. Each tab's embedded editor-owned undo lease gives
constant-work access to its buffer owner, avoiding repeated buffer-table scans.
The bounded tab list is encoded into prepaid storage. TARGET is an independent
record carrying the intended pathname, including missing named files whose
content BASE is empty. Complete checkpoints retain associations. Startup replay
of this metadata still belongs to the missing session-discovery implementation.

## Failed save recovery

Use file_save_args.replaced to preserve replacement knowledge independently of
return status. A known replacement plus validated saved BASE can finish after a
directory barrier failure. Sync the target parent before finish, even when it is
different from the journal parent. Do not infer a successful generation from a
post-rename identity failure; retain ambiguity and permit explicit cancellation.

savectl_recover_cancel exclusively leases the journal to publish a complete
CURRENT checkpoint through journal_save_cancel, then retires retention and its
namespace barrier. Cancel never installs Saved and leaves the current contents
dirty. Failed PREPARE with an ambiguous journal rename can have an unreceived
namespace barrier: retry that barrier off path before rotating. Writeback loss
still requires fresh CURRENT publication. All failures preserve the token for
retry; every other live token must remain represented in the caller's checkpoint.
The editor has not yet adopted these recovery UI operations because §3 is open.

## Reload private backing

Retain a configurable writable cache directory descriptor during controller
setup; default to an absolute TMPDIR or the system temporary directory. Try
source-parent temporary backing/reflink first. If that fails, create private
backing through the retained cache directory. Unsupported O_TMPFILE falls back
to a cryptographically random exclusive/no-follow temporary entry, immediately
unlinked through that same directory descriptor. Copy uses bounded scratch and
checks source identity before and after. Cache/source directory access is off
path; no write access to the readable source parent is required for fallback.

## Acceptance

No new globals. No change to Enter/brace indentation or large-file routing.
Release and sanitizer validation, red/green transcripts, partial requirements and
LeakSanitizer limitations are recorded in the worker report. No whole-editor
performance gate or unqualified power-loss time bound is claimed.


## Ctrl+S and startup continuation

Maintain a prepaid persistent binary session graph, publishing changed buffer
snapshots and view/topology metadata during ordinary bookkeeping. A save pins
the root and selected published snapshot; it performs no buffer enumeration,
content copy, heap allocation or filesystem work on the command path. The node
reservation is `3 * (2 * width - 1)` (G, structural): `width` is the smallest
power of two covering buffer capacity; a complete binary graph has
`2 * width - 1` nodes, and the live graph, pinned graph and transient update path
fit within three complete graphs. This is a ownership bound, not an invented
memory or latency budget. Snapshot creation uses the buffer's existing arena
allocator. Failed publication preserves committed edits and refuses a stale
save cutoff rather than saving obsolete bytes.

One editor save owns the session journal at a time. Save preparation and CURRENT
finish serialization run on the bulk worker. Neither UI append/pump nor the UI
private-pool consumer may touch the journal during the controller lease. After
PREPARE handoff, edits and ordinary journal pumping resume during file writing.
Finish reacquires a quiescent journal and rebases every association sharing the
canonical target, retaining each buffer's own current content. The selected
cutoff supplies the saved history boundary; branching/eviction invalidates an
unretained cutoff conservatively. Other target buffers remain conservatively
dirty after another buffer replaces their shared disk file.

Compose Saving after the existing paint composition, keeping paint.c untouched.
Backend BUSY does not acknowledge it. If journal admission is pending, the
submitted Saving frame timestamp belongs to that queued request and transfers
to its controller when admission completes. Controller completion cannot erase
an unsubmitted Saving frame. The busy-backend regression verifies a single
admitted frame (G, derived from render_frame_begin's successor identity).
Explicit close is quiescent and may settle saves without claiming a new frame
submission after the window is closing.

Failures remain per buffer. A retained ambiguous token prevents another save
until recovery resolves it; unrelated buffers remain editable. No new error is
promoted to the global stopped-loop state. Existing controller recovery APIs
remain available, but a dedicated retry/cancel UI is outside this continuation.

Use stable sidecar flock leases for session discovery, because atomic journal
rotation changes the journal inode. Select the newest nonempty unlocked session
artifact. Restore named associations, untitled content, live tab order/active
selection, retained buffers and views before any text submission. Ordinary
startup requests reuse recovered matching files instead of opening duplicates.
The existing WINDOW record enters the resize queue. Recovery uses private COPY
acquisition so a mapped prefix frame cannot expose unreplayed bytes.

An explicitly supplied existing journal enters journal_open_checkpoint: no
append or pump can overwrite the recovery inode before complete rotation.
Validation/replay failure preserves it, including failed-startup teardown.
Recovered CURRENT state is durably checkpointed before an orphan journal is
retired. Interrupted PREPARED saves remain dirty even when replacement bytes
already match disk; their private originals retire only after the recovered
checkpoint no longer depends on them. There is no new on-disk record schema.

The small-file editor open path now waits for acquisition readiness for COPY
as well as MMAP before attachment. PREFIX_READY alone does not satisfy
file_attach, and the new process/repeated-open tests exposed that race. Large
COPY prefix preview behavior and large.c remain unchanged.

Native display comparison, recovery conflict UI, merged recovery across multiple
orphan sessions, precise closed-stack history and recovery memory/first-paint
performance for giant sessions are not certified here. The journal schema has
live tab order but no explicit closed-stack ordering record. Existing module
benchmark memory census does not yet include the new prepaid session graph.
