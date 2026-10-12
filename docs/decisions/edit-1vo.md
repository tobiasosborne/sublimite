# edit-1vo: retry retained save failures

The next save request reconciles the failed controller's retained token before
starting the requested save. Simply clearing the session's blocked flag cannot
work: savectl correctly refuses fresh saves while it owns recovery retention.
Keep that ownership on recovery failure, and allow another request to retry.

Cancellation captures a complete CURRENT graph with empty BASEs and full
snapshot deltas for buffers sharing the failed target. This checkpoint does not
depend on the retained old original, including ambiguous replacement cases.
Verified replacement recovery instead uses savectl's captured replacement BASE.
After durable reconciliation, publish those BASEs back into the session graph
and start a fresh save of the requested buffer's current text. Reconciliation
alone does not declare later edits clean.

The requested buffer is identified by its existing logical identity rather than
an additional raw pointer. Another tab may request recovery; if that requested
buffer has since retired, skip its fresh save. Existing owner-leaf lifetime
behavior remains outside this bead.

Add a request-form recovery entry point to savectl so CURRENT encoding,
filesystem work, snapshot retirement, and graph release stay on the bulk
worker. The UI only pins published state and submits work. Drain accepted
journal batches before fresh finish rotation, accepting damaged/full-log status
as recoverable through a new complete generation. Do not overwrite a checkpoint
preparation error with a rotation result.

Treat Save as a control action at the source-change input gate. A verified
replacement followed by a directory-barrier failure may already have raised
the source banner; Ctrl+S must reach session recovery. Ordinary fresh saves
still check source staleness in the session request handler.

Regression faults use the existing durability-step seam: remove the temporary
name before rename, or close the writer's directory descriptor after rename.
The latter uses a separate target directory so it cannot close the journal's
directory descriptor. Exercise same-tab retry, another-tab request, subsequent
original-tab saving, target contents, token retirement, and dirty markers.
Park the bulk lane while counting allocations for the retry request so worker
checkpoint allocations cannot be attributed to the UI. Run the same regression
in release, where the allocator interposer is active.
Fixture lengths derive from marker strings; waits reuse the existing editor
idle policy. No new budgets or durations are introduced.
