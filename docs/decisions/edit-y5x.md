# edit-y5x: preserve the launch lease across recovery retry

When `open_editor` schedules the existing edit-btj recovery retry, transfer
artifact cleanup to the retry by clearing the failed attempt's
`session_lock_owned` before `editor_close`. The close still drains and closes
the journal and closes the editor's lease descriptor. It keeps the empty
launch journal and the named lock inode, so the retry can duplicate and
validate the caller's still-held lease. The successful retry owns cleanup
again through `editor_session_restore`.

Keep this handoff in `open_editor`'s retry branch. Ordinary failed launches
and final closes continue to discard empty launch journals and release their
lock names. A nonempty partial checkpoint continues through edit-btj's
existing set-aside handling.

The existing `install_failure` case now exercises edit-rzj capacity deferral,
which never reaches install failure. The new regression creates an unsaved
missing TARGET and then makes that TARGET a directory. Its empty BASE replays
successfully; TARGET acquisition fails during install using the normal file
error return. Test both leased and non-leased launches with an empty tab, plus
the leased `start_empty` shape from `main.c`. Check the notice, preserved
recovery artifact, unchanged pre-existing profile entries, lock identity and
final-close consistency. The `start_empty` case also checks that the empty
launch journal retains its inode across the retry.

All fixture sizes and counts derive from the marker text, the fixture's
named files and the editor's structural states (P). This change is startup
teardown only and adds no allocation to the typing path.
