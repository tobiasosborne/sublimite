# edit-qct: deferred session journals become recoverable closed tabs

Bead: edit-qct (edit-fm4 part 2). Worker session s7, 2026-10-16.
Code: `src/editor/restore.c` (new), hooks in buffers.c, editor.c, checkpoint.c,
session.c, input.c, recovery.c. Test: `tests/editor_session_test.c`
`large_restore` (new) and `large_orphans` (extended).

## Coordinator decisions as implemented

1. **Placeholder closed tab per deferred source.** `editor_restore_defer`
   (called from fm4's deferral branch in `editor_session_restore`) prepares an
   empty untitled buffer, opens it with `tabs_open`, renames the tab after the
   journal's first BASE path, and closes it straight into the closed stack
   with `tabs_close`. The live tabs and view state are unchanged (open saves
   the live state, close restores the MRU head). It is bounded by the existing
   pools: it needs a free buffer slot, one transient live tab slot and a
   closed-tab record that evicts nothing. If they are not available, the
   source stays counted only (`recovery_deferred`), and the status line says
   "... not replayed at startup (closed-tab pool full)". The placeholder
   carries the journal path, its directory and the lease that discovery took
   (`previous_fd`; fm4 used to close it). Excluded from the journal:
   `build_checkpoint` skips placeholder buffers. `editor_journal_metadata`
   skips their VIEW records. TABS (checkpoint and metadata, through
   `recorded_tabs`) lists live tabs without placeholders and remaps the active
   index, because recovery rejects a TABS id that has no BASE. The session
   tree's `publish` skips them, so the save worker's checkpoint never sees
   them either. The "nothing installed yet" metadata guard counts placeholder
   ids (`recovery_placeholder_ids`), so a launch that only listed placeholders
   writes nothing. Clean exit: the placeholder is destroyed, the lease closes
   and the journal is untouched. The test checks inode, size and mtime.
2. **Restore on reopen.** `editor_reopen_tab` starts the restore when the
   reopened tab is a placeholder. The **idle trigger is not implemented**
   (my choice): restoring a GB buffer that nobody asked for maps files and
   reads the journal and BASE in the background. Reopen is explicit and the
   closed tab already shows the user what can be restored.
   - **WORK_BULK scan** (`restore_job`): `journal_scan_file` reads the
     journal read-only. The accepted prefix of a torn tail is used, as
     `journal_replay_file` would truncate to, but the source keeps every byte
     until it is retired. The scan builds a BASE-relative program: per buffer,
     BASE identity, target, view, a PREPARED SAVE flag, and INSERT/DELETE ops
     whose insert bytes are copied into one worker arena. The arena is sized
     by the journal file size: (P) every journalled insert byte is a byte of
     the journal. Every named BASE is checked with `journal_check_base` on the
     worker. If it changed, the buffer is marked conflict and its edits are
     skipped (edit-btj).
   - **Whole-text shape** (pre-28u checkpoints: DELETE(0, all) then the full
     text). A DELETE that covers a buffer's whole current text drops its
     earlier ops. The worker materialises the new text in a gap buffer, then
     trims its common prefix and suffix against BASE, reading BASE with
     `pread` in `JOURNAL_INSERT_SLICE_BYTES` chunks. The program becomes one
     DELETE plus one INSERT of the differing middle, so the UI copies only
     that middle.
   - **Completion** goes through the src/work mailbox (`EDITOR_RESTORE_MSG`,
     a message-kind tag like `EDITOR_LARGE_MSG_*`), which wakes the loop.
     `editor_restore_service` (called from `editor_step`) also polls the
     handle.
   - **Open with the configured acquisition.** `editor_buffer_prepare(target)`
     keeps `cfg.copy_threshold`, so a 1 GiB BASE takes the mapped large-file
     path. The program is applied to the tree right after `file_attach`,
     before any lineidx, layout, preview adoption or snapshot sees it. For a
     pending (mapped) open that is in `editor_adopt_open`. For a small or
     untitled file it is in `editor_buffer_prepare`, through
     `e->restore_prepare`. Before applying, `origin` (the opened generation)
     must equal the journalled BASE. If it does not, the buffer is treated as
     a conflict.
   - **Install** (reload_source's swap). The first restored buffer replaces
     the placeholder in its own tab (`tabs_replace`, same tab id and source
     generation, so parked keys carry over), and the tab is renamed. The
     other buffers of the session are installed after it; those that were
     closed in the session go back to the closed stack. Capacity is checked
     first, as edit-rzj does; when it is short the restore is deferred and the
     placeholder stays. View state comes from the full VIEW record (no
     newline count over a mapped original on the UI thread). Modified =
     edits or PREPARED SAVE; a conflict buffer is clean. Then `journal_flush`
     + `editor_checkpoint`. 28u writes it as differences from BASE (origin ==
     BASE): **4096 B (M) for the 1 GiB fixture**, so the restored path sets
     origin through ordinary prepare and needs no edit-f3i work. Only after
     that is durable is the source unlinked, or renamed aside if any buffer
     conflicted, together with its lock name; then the directory is fsynced
     and the lease closed.
   - **Why install_session forced COPY.** The eager startup path
     (`install_buffer`) replaces the whole tree with replayed private bytes
     (`piece_delete` all + `piece_insert` all). With a mapped acquisition,
     `add_buffer`'s mmap branch would run `editor_step` and submit BASE bytes
     before replay, and `piece_delete` of a mapped original copies every
     deleted byte into the add buffer. That path now runs only for sources
     within `JOURNAL_STARTUP_SCAN_BYTES`, where the copy costs at most the
     bound. The lazy path never deletes the whole tree, so it keeps the
     configured threshold. install_session is otherwise unchanged.
3. **Input and inert placeholder.** A placeholder accepts no mutation:
   non-control events are parked as for `pending_open`. `editor_inject`
   marks them and `queue_pick` holds them until the placeholder is replaced,
   then they apply to the restored text. Ctrl+S on a placeholder is ignored.
   No malloc was added on the typing path. The checks added there are a bool
   read in inject and pick, and an early return in `editor_restore_service`
   while `restore_active == 0`. Release editor_test, before and after:
   mallocs=0 on every guard print (worker report).
4. **Failure keeps the source.** If the scan fails with JOURNAL_INVALID, the
   journal is set aside (edit-btj naming). Any other failure leaves it
   discoverable. Either way the lease is released and the placeholder becomes
   an ordinary empty untitled buffer registered in the journal.

## Numbers

No new bound or count. Growth factors are (P) structural (doubling op and
buffer arrays; a quarter-text gap growth copied from recovery.c's edit-28u gap
buffer). The worker arena size is (P) the journal size. Chunk size reuses
`JOURNAL_INSERT_SLICE_BYTES`. `JOURNAL_STARTUP_SCAN_BYTES` is unchanged.

## Known limits (not done)

- **Whole-text journals that differ at both ends.** The trimmed middle can
  still be the whole text, and the UI then copies it: `piece_insert`, plus
  `piece_delete` copying the deleted original bytes for undo. A
  worker-materialised text could become a private mapped original instead
  (`piece_init_mapped` over the worker buffer), but then the buffer is not
  bound to the target's generation, which needs the edit-f3i origin work.
- Large **untitled** buffers in a deferred source are inserted on the UI
  thread. Their text exists only in the journal.
- `install_all` after the swap runs `journal_flush` + `editor_checkpoint`
  on the UI thread, as reload_source does. This is the worst tracked step
  (11-30 ms (M) under load 4-12).
- If the restore job cannot be submitted (pool full), the placeholder stays
  live and inert. Closing and reopening it retries.
- Retained `.sublimite-base-*` originals named by a restored source are not
  retired (restore_source does that for eager installs). They stay as litter.
