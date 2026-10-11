# edit-btj: a recovery journal never blocks launch

## Defect

`editor_session_restore` (src/editor/recovery.c) discovered the newest lockable
`session-*` journal and replayed it with `journal_replay_file`. That replay
checks every BASE record's identity (size, mtime, ctime, inode, device, CRC32C
of the first 4096 bytes) against the file on disk and stops with
`JOURNAL_BASE_CHANGED` at the first mismatch. Restore mapped every nonzero
result to `EDITOR_ERR_IO` (-103), `editor_open` failed, and the source was kept
by design, so the newest journal failed again on every launch.

Astra's drive 2 hit this twice with one cause: task 12 replaced the file
externally; the ordinary-profile failure came from the drive harness rewriting
`build/astra2/files/typing.txt` with the same 22 bytes before each task, which
changes mtime/ctime and therefore the BASE identity. Tobias's real profile's
newest journal is that drive's `session-tXZIzU` (BASE id 3 = typing.txt,
check result 5 = BASE_CHANGED), so his profile launched -103 as well.

## Choice

The journal stores BASE identity, not base bytes. When the file differs from
the journal's BASE, its INSERT/DELETE records cannot be replayed faithfully on
the bytes now on disk. Of the options in the brief, the existing tab code
supports this one with the smallest change:

* **Open the file as it is on disk** in its recovered tab, clean, view at the
  start. Every other buffer of the session (unchanged bases, untitled buffers,
  tab order, window size) restores as before.
* **Keep the unsaved edits** by renaming the whole source journal to
  `<journal>.set-aside-<CLOCK_REALTIME ns>` (renameat2 RENAME_NOREPLACE, never
  delete, never replace). Discovery skips dotted names, so it is not replayed
  again; the new CURRENT checkpoint holds the restored session without the
  conflicted buffer's edits. Retained `.sublimite-base-*` generations are not
  retired when the source is set aside, because the set-aside journal still
  names them.
* **Notice** on the bottom status row (save-status overlay, lower priority than
  save status): "file changed on disk since this session; its unsaved edits
  were set aside". It disappears at the first mutation (`stats.mutations`
  snapshot; one forced full compose restores the row).

A second dirty tab with the journal's text was rejected: without the base
bytes there is no journal text to show, only fragments of inserted bytes.
Strict identity is kept (no "same size and prefix CRC means same bytes"
shortcut): a CRC match is not proof, and applying edits to the wrong bytes would
show wrong text silently. The same-bytes rewrite therefore also sets the edits
aside; the test pins that.

## Mechanism

1. First pass unchanged (`journal_replay_file`, streaming, truncates a torn
   tail on success).
2. If it fails and the journal was discovered (not `recover_existing`), the
   partial session is discarded and a tolerant pass replays the same bytes
   (`journal_replay_bytes` over a startup-only heap copy of the file). In that
   pass `restore_record` checks each named BASE itself; `BASE_CHANGED` marks the
   buffer `conflict`, drops its edits and opens its target from disk.
3. Any other replay error: discard, set the source aside, open with no
   restored session, notice "session recovery failed; its journal was set
   aside".
4. An `install_session` failure leaves partly installed tabs that cannot be
   unwound in place. Restore sets the source aside and flags `recovery_retry`;
   `editor_open` closes the failed instance, sets aside a nonempty destination
   (a partial checkpoint; `journal_open` requires it empty) and opens once
   more with `recovery_skip` (own lock taken, no discovery).

A journal named explicitly as the editor's own path (`recover_existing`, not
used by main.c) keeps the strict contract: it is also the live destination, so
setting it aside would remove the CURRENT journal.

All of this is startup-only; nothing runs on the typing path except a
`stats.mutations` compare in the frame overlay and compose.
