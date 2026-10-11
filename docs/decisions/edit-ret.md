# edit-ret — clean source refresh

Use the existing saved-history marker to decide whether a disk change can
replace a buffer: `!saved_lost && history_cursor == saved_cursor`. The editor's
`dirty` field means a frame needs rendering, so it cannot decide whether text
has unsaved edits. Undoing back to the saved marker also makes a buffer clean.

Extract the existing Reload replacement into `reload_source`. Explicit R and
automatic refresh share buffer preparation, cursor/selection/scroll clamping,
fresh undo history, journal checkpointing, save setup, watcher registration,
logical tab identity, and launcher wait ownership. Inactive open tabs use their
retained tab view; refreshing them does not select them. Closed retained tabs
refresh when reopened.

Automatic refresh happens during source maintenance, after the existing save
completion/rebase pump. Keep the existing self-save suppression: do not detect
source conflicts while a save is active, and let save completion rebase the
file identity and saved-history marker before classifying a change. The shared
inotify dispatcher, generation validation, and file module are unchanged.

Refresh preparation can allocate and perform I/O. Defer it while input is
queued, an input frame still needs submission, a backend frame is active,
view/clipboard/save leases are busy, or journal edits are staged. A deferred
clean change stays pending in the file module without drawing a prompt. An
intervening unsaved edit requires the existing dirty-buffer prompt.

Stage the replacement tab and its clamped state before checkpointing, including
inactive tabs. Roll back the borrowed tab resources, modified marker, and live
view if reconciliation fails. Successful replacement retains outgoing worker
leases under the existing retirement policy. Failure to acquire/reconcile the
replacement and incomplete/failed original acquisition retain the existing
source recovery prompt; they must not discard the old text.

Tests cover the open/saved, copied/mapped, active/inactive combinations, then
shrink the source to exercise clamping. The saved case explicitly drains
self-save notifications and checks the buffer and undo history were retained.
Submitted-frame inspection catches a transient prompt as well as a persistent
stale-source state. Existing prompt/Keep tests now make actual unsaved edits
before changing the source, preserving their intended coverage.

`editor_p4_test` and `editor_close_test` gain a `--headless` selector so their
existing null/injected cases can run without an X socket. Their ordinary runs
still include their display cases.
