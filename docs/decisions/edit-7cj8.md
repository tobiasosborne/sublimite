# edit-7cj8: checkpoint a detached original

The retained-original size limit remains the existing
`JOURNAL_STARTUP_SCAN_BYTES`; opening or typing into a larger file must not
start an unbounded background copy. The immutable session snapshots already
keep the file module's original descriptor and mapping alive across a
rename-over. Use that lifetime to preserve modified text after source
maintenance observes a changed BASE whose retained copy is unavailable.
The existing zero-limit test opt-out (P, disabled retention) also disables
this fallback, preserving the set-aside regressions' explicit policy.

The existing plain worker checkpoint owns the journal lease and writes an
empty BASE, the real TARGET, and the complete snapshot as INSERT records.
This uses the existing journal schema and makes recovery independent of the
replaced original. Acquire source validation leases only when starting this
maintenance job; session publication on the typing path stays allocation
free. The worker accepts the detached inode's ctime change through the file
module's existing retired-source validation, and validates the mapped source
before and after encoding. A changed size, mtime, owner, mode or fault epoch
refuses publication. COPY originals remain private even after an in-place
rewrite. An already overwritten or truncated mapped original cannot be
reconstructed by this mechanism.

Adopt the empty BASE in live state only after durable rotation succeeds.
Subsequent checkpoints then retain the self-contained shape. Failed
publication leaves the previous journal and live BASE intact and reports a
recoverable result through the existing checkpoint retry path. Publication
uses the existing quiescent-journal and input/frame gates; a crash before
change detection or durable publication still has the original dependency.
An already queued Reload/Keep has priority over automatic publication. A
held-save regression makes the peer copy unavailable through the existing
zero-limit seam (P, disabled retention), queues the explicit choice, then
restores retention. Taking a publication lease before that choice delayed
its reconciliation while save-status painting resumed on an unbound stale
layout. The retain tick now leaves that turn to the explicit choice.

Large complete journals use the existing deferred closed-tab recovery path.
That path must replay an empty BASE independently of the current TARGET and
reserve its actual INSERT bytes in the buffer arena before applying them.
The reservation follows journal contents (P), with no new budget.

The regression's sparse original is the existing startup-scan bound plus
`sizeof "x" - 1` bytes (P): the smallest excluded generation. Boundary
sentinels and a full comparison of the sparse holes verify the old original,
the inserted literal, the modified flag, and the untouched external TARGET.
Watchdogs reuse `EDITOR_IDLE_NS` (E, existing test escape policy); no latency
claim is made.

The copy-window regression reuses `FI2_BYTES` (P, predecessor fixture) in
private COPY mode. It makes the edit durable, holds the first retention
write, rewrites TARGET in place, and releases the write. Retention then
fails its source validation, while the worker checkpoint recovers the
private original. It also evicts the sole leaf in a sibling session-graph
branch (P, minimal binary-graph shape). Source validation identifies leaves
by their buffer pointer, so an empty internal branch is skipped safely.
A permanently stalled journal worker still prevents the
existing quiescent-journal publication gate from opening; this change does
not remove that journal-ownership constraint.

The failed-save cancellation concern is already superseded in this tree:
`session.c` encodes cancellation against an empty replacement BASE and
`savectl` publishes that complete CURRENT checkpoint before retiring its
save token. Add a replay assertion after another external rewrite to pin
that behavior. The old fi2 copy reference need not survive when the durable
journal already contains all text. Failed saves retain their prepared token
until reconciliation, as the existing save-controller suite checks.

Asynchronous retention now creates `.sublimite-copy-XXXXXX` while holding a
shared lease on its actual parent directory. Those names are never
journalled. After the existing source/copy validation and barriers, promote
with `RENAME_NOREPLACE` to `.sublimite-base-*`, recapture the rename's ctime,
and persist the final inode and directory identity before reporting READY.
Both prefixes have equal lengths (P, literal sizes), so the existing path
capacity and nearest-ancestor fallback remain sufficient. A housekeeping
lease refusal returns BUSY and retries through the existing retention tick.

Housekeeping's exclusive directory lease proves that no live asynchronous
writer owns a staging name. It removes regular staging files with the exact
`mkostemp` suffix schema, without reading content or applying the artifact
cap, then syncs the directory. The crash regression kills the real writer
after its first successful write, checks its actual partial length against
the existing cap, and verifies live-owner exclusion and dead-owner cleanup.
Completed retained generations keep the existing reference-inventory and
artifact-cap policy. Legacy `.sublimite-base-*` partials cannot be identified
as incomplete from their names; synchronous save-prepare retention continues
to use that older namespace.

Crash-fixture teardown in the editor suites removes the new staging prefix
as well as completed retained copies: the last deliberately abrupt launch
has no subsequent startup sweep. The independent mid-copy regression still
requires housekeeping itself to remove the dead stage below the cap.

The cited `docs/decisions/edit-jy6.md` is absent from this worktree. The
available predecessor evidence is `edit-fi2.md`, its worker report, and the
worker-checkpoint decision `edit-ze59.md`.
