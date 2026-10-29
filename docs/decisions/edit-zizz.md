# edit-zizz: restore what matters from an over-capacity session

Policy by Tobias, 2026-10-29. Code: `src/editor/recovery.c` (`unsaved_buffer`,
`plan_partial`, `install_partial`, `restore_source`).

## When it applies

Only when `session_fits` is false for a discovered source (`!recover_existing`).
A session that fits restores exactly as before. An explicitly named journal
keeps its strict contract.

## Order

1. Every unsaved buffer is installed: as a live tab while live room remains,
   then as a closed (retained) tab. Unsaved buffers in the source tab list come
   first, in tab order. Unsaved buffers that were closed in the source follow.
   They are live too while room remains, appended after the tab-order ones.
2. Unmodified tabs fill the remaining live room in recorded tab order (buffer
   order when the journal has no TABS record). Unmodified buffers that were
   closed in the source are never reinstalled.
   - An unmodified file tab whose target no longer exists is never reinstalled
     (coordinator follow-up, 2026-10-29). "No longer exists" means the stat of
     the target returns `OS_E_NOENT` or `OS_E_NOTDIR`; any other stat result
     keeps the tab.
   - Such a tab carries no bytes, so dropping it loses nothing. It counts among
     the "unmodified tabs ... not reopened".
   - Unsaved buffers are never dropped, whether or not their file exists.
   - Sessions that fit are unchanged: they still restore such tabs as before.
3. Everything else is dropped. Its bytes are on disk.

Capacity follows `session_fits`. A closed buffer needs one temporary live slot
before `editor_close_tab` retains it, so with any closed pick the live picks
stop at `room - 1`. Closed picks never exceed the free closed pool, so no
existing closed tab is evicted, dirty or clean. Installed buffers never exceed
the free buffer slots. The edit-dzk1 unpainted buffer is never live. If it is
unsaved it is installed last and closed, with its notice. If it is unmodified
it is dropped.

If some unsaved buffer does not fit, nothing is installed. The source is
deferred whole, exactly as before, and the notice counts the unsaved tabs:
`additional session recovery deferred; journal retained (tab capacity: N unsaved tabs)`.
This applies when there are more unsaved buffers than `(live room - 1) +
closed room`, the temporary slot again. The existing install_failure case
(two unsaved buffers, 1 live + 1 closed) therefore still defers.

## How "unmodified" is decided (`unsaved_buffer`)

- Untitled (no target): unsaved iff the replayed text is nonempty.
- File with no INSERT/DELETE after its BASE, where that BASE is the file itself
  (not an edit-fi2 retained copy): unmodified. Replay already checked the BASE
  identity and read exactly those bytes (`materialize_base`).
- Any other file buffer: compare the replayed bytes with the target file on
  disk (a regular file, same size, then chunked `pread` + `memcmp`). Equal
  means unmodified. This is the comparison `install_buffer` makes for its clean
  flag (`same_bytes` against the opened target), so a tab ranked unmodified is
  exactly a tab that would restore unmodified. It is exact, and it is safe to
  drop such a buffer because the same bytes are on disk. If the file changed
  after replay, the text the user had is still what replay compared against.
- Ops are not enough to decide. The real 1013-buffer journal has an INSERT and
  a DELETE after the BASE of nearly every file buffer (M, read-only shape scan
  of a copy), yet their replay equals the BASE.
- The cost is one more read of file bytes that the startup cost scan already
  admitted under `JOURNAL_STARTUP_SCAN_BYTES`. It is paid only on the
  over-capacity path, never on the typing path.
- An interrupted save (`save_pending`) always counts as unsaved.
- A mapped (large) buffer with ops counts as unsaved: there is no flat text to
  compare.
- A changed BASE (edit-btj conflict) ranks as unmodified. Install opens only
  its disk text, and its edits are not replayed in any case. They stay in the
  source journal, which a session with conflicts sets aside whole (renamed,
  never deleted). Counting conflicts as unsaved deferred the real journal on
  every launch: 998 of its `/tmp/*.dot` targets no longer exist, and one `.md`
  changed.
- A changed BASE whose target is gone (deleted file) therefore also falls
  under the vanished-file rule and is not reopened. Its edits were never
  installable from that BASE. They remain in the set-aside source, which is
  byte for byte the original journal.

## Retirement and notices

Round 2 (Codex review BLOCKER; the fix was chosen by the coordinator): a
partial restore that dropped any buffer never deletes its source. It sets the
source aside with `editor_set_aside`: a dotted `.set-aside-*` name that
discovery skips, renamed and never copied.
- The retained BASE copies the source references are kept: restore_source
  returns before its retained-copy unlink loop, as the conflict branch already
  did.
- `journal_retire_directory` scans set-aside sessions for BASE references and
  pins them, so orphan cleanup keeps what the set-aside source needs.
- Why: dropping rests on comparing replayed bytes with a file that another
  process can rewrite at any time (between chunks, after the comparison,
  before retirement). Keeping the source makes a wrong "unmodified" verdict
  lose nothing. Every dropped buffer still replays from the set-aside file
  (`journal_replay`).
- `disk_equals` also requires the descriptor's generation (identity, kind,
  size, mtime, ctime) to be unchanged across the read, and requires the name
  to still select that generation. A file that changed during the comparison
  therefore ranks as unsaved and is restored. This is cheap: two stats per
  compared file, on the over-capacity path only.
- The no-ops shortcut stays: its exposure is covered by the set-aside.
- A partial restore that dropped nothing, and every session that fits, is
  retired as before.

When every unsaved buffer was installed and nothing was dropped, the source is
retired as before: the durable replacement checkpoint holds the accepted state. A session with
conflicts is set aside as before. The partial notice is
`restored N unsaved tabs[ (K closed: Ctrl+Shift+T)]; M unmodified tabs from the last session were not reopened`.
When there are conflicts, the existing changed-on-disk text is appended after
"; ". Otherwise a partial restore that dropped buffers appends "the old
session journal was set aside". The edit-fi2 rewritten-target notice ("file
changed on disk since this session; its unsaved edits were recovered") is
appended too, instead of replacing the partial notice (round 2). The dzk1 first-paint notice, if install sets it, takes precedence.
The editor-owned `session_notice` buffer holds the text. Its size is derived
in `private.h`.

## Active tab

The active source tab stays active if it is restored live. Otherwise its right
live neighbour (else its left) becomes active, the rule edit-dzk1 uses for its
skipped tab.
