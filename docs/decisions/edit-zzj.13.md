# edit-zzj.13 editor review integration decisions

## Session 9 — editor-local continuation

Mutation cancels index work without first polling/adopting its completed
backlog. Adoption and source retirement remain in the existing maintenance
turn. The current line-index dependency already uses relative offsets and
bounded adoption; no line-index implementation or repair policy was changed.

Each buffer supplies a recycling piece allocator backed by its existing base
arena. An exact-size class table has at most 64 classes (G); allocation examines
at most that many classes, and free pushes directly into its recorded class.
A 16-byte header preserves payload alignment. Exact sizes avoid rounding
original bytes and add chunks up to powers of two. A per-buffer mutex protects
both arena allocation and returned blocks, including snapshot-thread frees.
Destroy workers/snapshots before destroying this mutex and arena. Class
exhaustion returns NOMEM; storage returned by slab trimming, checkpoints and
temporary arrays stays reachable for reuse. The per-buffer request counter
and failure-request seam support deterministic replay fault tests; they are
not process globals.

The editor records that a journal job was scheduled when its ordinary pump
succeeds at the default batch/deadline admission boundary. It then waits for
the journal completion mailbox instead of scheduling another expired sync
timer. Receive and explicit flush clear this state. Work-pool admission BUSY
retains the retry timer because that result does not establish that a journal
job exists. This preserves ordinary worker sync/batching policy and does not
add a synchronous durability barrier to typing.

Slice diagnostics measure complete action plus journal-staging segments,
blink/resize, and composition/submit. They no longer count just nested view
and layout calls. This reports indivisible stalls honestly; it does not make
those operations resumable or satisfy finding 11's hard preemption contract.

Current undo atomic checkpoints are adopted for finding 22. The editor test
sweeps allocator failures through replacement undo and checks unchanged tree,
selection, history position and recovered journal bytes. Linking the test
against the undo implementation preceding 92ca926 reproduces the intermediate
tree failure. No undo source or frozen undo header is changed in this session.

Whole-file open, bounded undo-group accounting, automatic burst grouping,
mapped-source reload/keep and native translation allocation attribution remain
unimplemented. A prefix-only tree without a pending-open ownership/adoption
state would risk accepting edits into an incomplete document. The undo group
count accessor still traverses history; accessing its private record encoding
from the editor was rejected. Session evidence and exact completion status are
in [the session 9 report](../worker-reports/edit-zzj.13-s9.md).

Successful editor actions append recovery records before returning to layout continuations or publishing submit/present callbacks. Successful journal writes establish process-crash protection in the page cache; this change does not add a synchronous durability barrier to typing. Power-loss durability remains the transport's worker sync contract.

On admission/IO failure, stop the UI loop before publishing another frame. Compare accepted sequence before/after each logical call: journal-owned IO records must not be retried by the editor, while an unaccepted suffix retains its staging bytes and offsets. Explicit flush/exit may allocate and block. It first drains accepted records and, if suspended, creates a fresh complete checkpoint containing each retained buffer's BASE, whole current content, view, tab order/active index and window dimensions. Named buffers preserve and validate their original BASE; a conflict returns an error rather than silently changing identity. Persistent filesystem errors remain errors, preserving the earlier recovery generation where transport rotation guarantees it. Successful failure recovery does not resume the stopped UI loop.

Unwrapped editor layout seeds each logical row through exact piece-tree line queries and restricts a layout invocation to that row, preserving resumable layout phase across turns. Piece leaves cover a bounded byte span and newline counts are already warm in this editor. This avoids adding a new checkpoint worker lifecycle to the current loop. It does not solve whole-file warmup (review 8), giant Unicode cluster timing, or distant horizontal column approximation. Wrapped layout continues using its existing visual-row machinery.

Finding 14 remains open. A bounded-input-batch draft made sustained-input progress, but overlapped the next mutation's process-wide allocation guard with preceding-frame libxcb fence IO. Waiting for that frame before mutation broke P4.I's delayed-backend ingress/IPC invariant. The batching draft was withdrawn; original input scheduling is retained. Platform ingestion now stops at full downstream editor capacity, routing work completions while queued native events remain upstream. This capacity protection does not claim to solve sustained input starvation.

Undo-restored insertion uses successive chunks of the existing staging arena. This removes the editor's staging-size rejection while keeping typing/replay free of libc allocation. Actual journal admission and replay slice timing remain separate concerns.

Backend initialization is an exclusive src/work bulk job. Publish an immutable integer result in a mailbox, selectively receive it on UI, and keep the configuration/state storage alive until the handle has physically finished. The UI only observes backend state after the mailbox handoff. Open remains synchronous; no new async cancellation API was introduced.

First-run directory creation retains parent descriptors, creates/opens a child and synchronizes the parent before descending. Existing components are also synchronized, covering retries after earlier failed barriers. Journal setup proceeds only after the whole chain succeeds. The filesystem barrier seam is setup-only and tested for namespace reachability and error propagation.

Main's existing ordered/bounded X11 drain and P4.I's final decoration repaint already address findings 1, 2 and 6. Tests preserve those behaviors; controlled fault runs establish that the inherited protections matter. No X11 module source delta remains.
