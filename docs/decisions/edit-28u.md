# edit-28u: checkpoints reference BASE instead of copying it

Bug: `editor_checkpoint` wrote, for every named buffer, DELETE of the whole
BASE plus the whole text as 4 KiB INSERT records (one calloc each), even when
the text was byte-identical to BASE. A 1 GiB open file gave a 1 GiB journal
per checkpoint ((M)[AC, BAT0 Full], red run: 1,086,427,136 B, 4208 ms,
262,151 allocations for one checkpoint of an unchanged 1 GiB sparse file).
The save path's checkpoint builder (`session.c encode_leaf`) did the same.

## Decisions

1. **Text equal to BASE writes BASE only.** Two exact cases:
   - untouched original: the tree is one run of ORIGINAL spans covering
     [0, base.size) in order, and the original is BASE's generation (rule 3).
     The encoder (rule 2) then emits no text record at all; no separate test.
   - saved and unchanged since: `editor_buffer.saved_text` (identity) +
     `saved_text_revision`. Set when a save completes with a replacement BASE
     (`session.c`, SAVED with `finishing && save_tracking`: the replacement
     BASE is the file written from the snapshot of `save_revision`) and on
     Save As when the captured BASE is the very generation `file_save_write`
     reported (dev, inode, size, mtime, ctime). A checkpoint writes BASE only
     iff `revision == saved_text_revision`, the identity equals the current
     BASE, and `piece_len == base.size`. The save worker's finish checkpoint
     does the same for the selected buffer when the pinned leaf's revision is
     `save_revision`.
2. **A modified text is written as its differences from BASE**, using only
   the existing INSERT and DELETE records. Pieces are walked in order with
   `piece_iter_next_source` (new, additive piece.h amendment: span's ORIGINAL
   offset or PIECE_ADD). With dst = output length so far and src = next
   unconsumed BASE byte, the text during replay is always out[0,dst) +
   base[src,size): an ORIGINAL span at offset o >= src emits DELETE(dst, o-src)
   if o > src, then is kept in place (no bytes); an add span emits INSERT(dst,
   bytes) in JOURNAL_INSERT_SLICE_BYTES slices; the end emits DELETE(dst,
   size-src) if src < size. Growth per checkpoint = metadata + 48 B per base
   gap (32 header + 16) + (8 + 32) B per insert slice + added bytes: O(pieces +
   added bytes), never O(file size). (P) ORIGINAL spans are strictly increasing:
   piece_delete copies removed ORIGINAL bytes into the add buffer and undo
   reinserts them from there, so an original range never reappears out of
   order; any span violating that is written out as bytes (always correct).

   **Deviation from the brief's BASE_RANGE record (and no version bump).**
   The kept ranges are expressed implicitly (keep = not deleted) instead of a
   new BASE_RANGE type, for crash safety: `journal.c payload_valid` rejects
   unknown types, so an older build replaying a journal containing a new type
   treats it as corruption and *truncates the journal there*
   (`replay_file_mode`: ftruncate + fdatasync) after having applied the
   leading DELETE, i.e. loses the text on a rollback to an older build. A
   magic/version bump is worse (truncation at offset 0). With only existing
   types the wire format is unchanged: old journals replay as before and new
   journals replay on old builds. Growth is the same order as BASE_RANGE (one
   record per gap instead of one per kept range).

   **Replay stays linear.** Flat-buffer recovery (`recovery.c`) memmoved the
   whole tail per op, O(ops x size). It now keeps a gap buffer: ops at
   nondecreasing offsets move each byte a bounded number of times; growth adds
   a quarter of the text beyond the request (geometric, (P) bounded total
   suffix moves). Live-journal ops at random offsets cost O(distance) instead
   of O(tail). `journal_apply_piece` (manual replay tool) needs no change.
3. **BASE identity rule.** Referencing is allowed only when
   `editor_buffer.origin` (identity of the generation `file_attach` installed
   as the tree's ORIGINAL; set once in `editor_buffer_prepare` from the same
   validated prefix identity as BASE, before publication) equals the BASE being
   written in size, mtime_ns, inode, device, ctime_ns, prefix_crc and
   prefix_len, and BASE is named. Any BASE replacement (save, Save As, keep
   after external change, recovery capture) changes the identity and falls
   back to the legacy whole-text shape. Replay validity is unchanged: every
   BASE identity is checked before delivery (`journal_replay_file`); a changed
   BASE stops the strict pass and the edit-btj tolerant pass opens the disk
   text clean and sets the journal aside by rename. Test (c) covers it.
4. **No per-record allocation.** `editor_checkpoint` runs two passes over the
   same unmutated state: the sizing pass writes each record into one scratch
   record of JOURNAL_MAX_RECORD and counts; the fill pass writes into one
   calloc'd record array and one `edit_arena` (mmap, MAP_NORESERVE) of exactly
   the counted payload. (M)[AC, BAT0 Full, load1 4.41]: 2 malloc-family calls
   per checkpoint (scratch + record array), independent of size, vs 262,151.
   `session.c` keeps its per-record calloc (worker, off the typing path); its
   record count is now O(pieces + added/16 KiB).
5. **Safer over smaller: small modified texts stay self-contained.** A
   modified text references BASE only when base.size > FILE_PREFIX_MAX (the
   file module's synchronous prefix, 1 MiB; larger files open asynchronously
   as pending_open). Smaller modified texts keep the legacy shape, so a
   journal set aside after its BASE changed externally still holds the whole
   edited text for manual replay (`tools/journal_replay`). For referencing
   journals that guarantee is gone: after an external change of BASE, the set
   aside journal holds only the inserted bytes and offsets, and the kept
   ranges referred to bytes that no longer exist. This is the price of not
   copying a GB file per checkpoint; automatic recovery never replayed edits
   onto a changed BASE in either shape.
6. Atomic groups: a checkpoint is a complete new file published by
   `journal_rotate`'s rename; groups are not needed inside it (unchanged).

## Behaviour changes to note

- **Retirement keeps clean tab sets (coordinator decision, 2026-10-16).** A
  session whose buffers all equal their BASE has no INSERT or DELETE after a
  checkpoint. Before 28u a BASE-only journal could only mean "never
  checkpointed"; after 28u it is a checkpointed clean tab set, which is
  hot-exit state (PRD §48: the tab set comes back). `retire.c` therefore
  treats a discoverable session journal (generated `session-*` name without a
  dot) that holds at least one BASE as not clean, like INSERT/DELETE/PREPARED
  SAVE; only BASE-free, edit-free session journals are retired as empty.
  Dotted names (`.set-aside-*`, `.retired`) are never discovered and keep the
  edit-rfq rule (their clean ones still retire), so set-aside housekeeping and
  the artifact cap are unchanged. Discovery adopts the kept session and
  `restore_source` unlinks it once the recovered checkpoint is durable, so
  the profile does not accumulate: after each of three launches (open, edit,
  Ctrl+S, checkpoint, quit; then two bare relaunches) it holds exactly one
  `session-*` journal (the last launch's own, nonempty because its install
  checkpoint wrote BASE) plus that journal's stable lease file (edit-rfq:
  stable lease files stay; the adopted session's lease is removed).
  The rollback-safety reason for no new record type (decision 2) and the
  FILE_PREFIX_MAX self-contained rule (decision 5) were confirmed.
- The fm4 large-orphan fixture (`editor_session_test`) now produces its 1 GiB
  orphan by unbinding the buffer's original before the checkpoint, which
  reproduces the pre-28u whole-text shape older builds leave behind.

## Not addressed (remaining)

(edit-f3i, 2026-10-16, addresses the first two items: post-save and
save-in-flight checkpoints diff against the saved revision, and the startup
recovery install keeps ORIGINAL spans; see docs/decisions/edit-f3i.md.)

- After a save, further edits of a large file checkpoint the whole text again
  (the tree's ORIGINAL is the pre-save generation, BASE is the saved file).
  Fix options: rebase the tree onto the saved generation after a save, or
  journal against the retained previous generation. Also a 1 GiB file edited
  while its save is in flight gets a whole-text finish checkpoint.
- Recovery still materialises each BASE in memory and installs with
  copy_threshold = UINT64_MAX (edit-qct); a recovered buffer's tree is all add
  bytes, so its later checkpoints are whole-text (bounded by fm4's startup
  scan bound, JOURNAL_STARTUP_SCAN_BYTES).
- Evicted buffers with a save in flight (edit-5u2 "kept") keep the empty BASE
  + whole text shape by design.
