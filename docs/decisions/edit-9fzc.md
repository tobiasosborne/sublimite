# edit-9fzc: Save As peer publication and save-lease retention

The Save As target's clean peer uses the existing replacement path through
`editor_reload_saved_peer`. It replaces the tab, clamps its view, clears its
history, retires the old generation and refreshes the session graph, but marks
`publish_owed` instead of flushing and rotating the journal on the UI thread.
The peer and owner therefore join the rebind's existing `editor_publish_start`
job. Explicit Reload and automatic source refresh retain their synchronous
checkpoint and rollback policy. A deferred reload marks the checkpoint owed
as soon as its replacement is staged, so later setup errors cannot lose that
obligation. No new allocation or work submission enters the typing path.

Retention still polls a finished publish job first, to adopt its result and
release its lease. It then returns whenever `editor_save_leased` is true,
covering both publication and the save controller's prepare/finish leases.
No retention submission, completion transition or copy removal occurs during
those leases. An active save without a journal lease keeps the existing
retention behavior; later maintenance resumes deferred retention. This leaves
the save worker as the sole owner of journal-pool submission and draining
while its `journal_flush` runs, under the existing ownership protocol.

The exit path remains synchronous by the bead's explicit allowance:
`editor_flush` finishes admitted saves and publication, flushes staged records,
then writes an owed Save As checkpoint itself. This off-path operation may
wait for storage. It does not promise a responsive shutdown.

Regression tests in `dataloss_test` observe journal rename callbacks by thread
for a Save As with a clean target peer, and hold the main bulk lane before a
real Ctrl+S prepare to check that retention stays idle until its lease ends.
They verify written and peer bytes, clean/modified state and resumed retained
publication. Existing shared-target crash/recovery cases cover recovery of
the peer and owner together. (P) Fixture lengths come from their text literals,
tab indexes from insertion order and counters from callback events. (E) The
watchdog reuses `EDITOR_IDLE_NS`; no new time, size or allocation bound is
introduced. The selectors are `--9fzc-peer` and `--9fzc-lease`, also included in
the default suite.
