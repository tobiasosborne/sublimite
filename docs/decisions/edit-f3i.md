# edit-f3i: checkpoints after a save diff against the saved revision

Bead: edit-f3i (the "Not addressed (remaining)" list of edit-28u). Worker
session s7, 2026-10-16. Code: src/editor/checkpoint.c, session.c, buffers.c,
commands.c, recovery.c, private.h. Test: tests/editor_checkpoint_test.c
(f3i (a), (b), (c)).

Bug: after Ctrl+S, BASE is the saved file while the piece tree's ORIGINAL is
still the pre-save generation, so `editor_checkpoint_references(origin, BASE)`
failed and every later checkpoint wrote DELETE(whole BASE) + the whole text.
Red run, 1 GiB sparse file, one byte typed, Ctrl+S, one more byte, checkpoint:
journal 1,077,936,128 B, checkpoint 4558.395 ms (M) [AC: BAT0 "Not charging",
load1 4.52]. The save worker's finish checkpoint did the same when the user
typed during the save (2 MiB fixture: 2,105,344 B instead of one page), and a
buffer installed by startup recovery was all add bytes with no valid origin.

## Decisions

1. **Mechanism (b): encode against the saved revision, not a re-based tree.**
   The saved file holds exactly the bytes of the snapshot the save wrote. That
   snapshot is retained (`editor_buffer.saved_snapshot`, paired with
   `saved_text`) and indexed by the *address* of its span bytes
   (`editor_ref_map`: sorted `{at, len, off}`; adjacent spans contiguous in
   memory and in the text merge, so a mapped GB original is one entry). Piece
   spans point into immutable original/mapping or add-buffer memory; the
   retained snapshot keeps that memory alive and unchanged (piece.h: the add
   buffer is never moved or freed while the tree lives; snapshot-owned add
   storage survives checkpoint aborts), so equal addresses are equal bytes.
   `editor_text_ops` gains a `ref` argument: a span found in the map is kept at
   its saved offset (same `out[0,dst) + base[src,size)` replay model as
   28u: DELETE for a gap, nothing for kept bytes), a span not found is written
   as INSERT slices, partially covered spans are split. Growth per checkpoint
   = metadata + 48 B per saved gap + (40 B + bytes) per insert slice: O(pieces
   + bytes typed since the save), never O(file size).

   Why not (a), re-basing the tree onto the saved generation: it needs a new
   piece.h operation that swaps the ORIGINAL under live pieces, undo refs,
   lineidx and the large-file layers, plus a new mapping of the saved file on
   the UI thread; and when the user typed during the save the tree is no
   longer the saved text, so it cannot simply become one ORIGINAL run. (b)
   copies nothing (the snapshot already exists: it is the save's request
   snapshot), allocates only in checkpoint encoding (never on the typing
   path), and needs no piece.h amendment (`piece_iter_next` pointers suffice).
   Memory: holding one snapshot pins at most the nodes of the tree as it was
   at save time (copy-on-write by refcount, piece.c `cow`), which publication
   snapshots already pin transiently; add bytes are never freed anyway.

2. **Snapshot lifetime.** `request_buffer` retains the request snapshot as
   `save_snapshot` (atomic refcount, no malloc). A completed save of a
   tracked revision (`SAVED`, `finishing && save_tracking`, where 28u sets
   `saved_text`) moves it to `saved_snapshot` (`editor_saved_reference`,
   which releases the previous one and frees its map) and republishes the
   buffer. Every other end of the save (`s->saving = NULL`: start failure,
   failed/unblocked, reconcile end, plain end) drops `save_snapshot`. Save As
   keeps its written snapshot as the reference exactly when 28u sets
   `saved_text` (the captured BASE is the generation the write reported).
   `editor_buffer_destroy` releases both before the buffer arena goes.

3. **Save in flight: ordering against the journal's BASE.**
   - Prepare (worker, `journal_save_prepare`): BASE = previous generation
     (substituted by the retained `.sublimite-base-*` copy, same bytes); text
     encoded against the tree's original or, after an earlier save, against
     that save's reference (`session_node.reference`, see 5).
   - A UI checkpoint while the write is still in flight (before rename) uses
     `b->base`, which is still the pre-save BASE (it is replaced only in
     `editor_save_tick`'s SAVED handling), so it encodes against the
     pre-save bytes: never against the half-written target. Test (b) step 1:
     4096 B, inserts "AB" + "CD". (Unchanged behaviour: `editor_flush` waits
     for the save; a UI checkpoint between rename and SAVED fails
     `journal_check_base` with JOURNAL_BASE_CHANGED as before.)
   - Finish (worker, `journal_save_finish`): the first checkpoint against the
     new BASE. For the selected buffer the replacement BASE holds the bytes of
     the snapshot that save request wrote; the finish capture retains
     `save_snapshot` as `session_capture.reference` and encodes the current
     text against it (identical revision: BASE only, as 28u). Test (b) step 2:
     4096 B, inserts only "CD" (typed during the save). Other buffers of the
     same target have unrelated trees: no reference (legacy shape, as before).
   - After SAVED: `b->base` = saved file, `saved_snapshot` = its bytes; UI and
     later worker checkpoints reference it. Test (b) step 3 and (a).

4. **Startup recovery install** (`install_buffer`, sources within
   JOURNAL_STARTUP_SCAN_BYTES). `editor_buffer_prepare` now records `origin`
   also while `e->recovering` (the journal is detached during install, so the
   28u branch did not run): the identity of the opened generation, from the
   same validated prefix. The replay keeps, next to the flat gap-buffer text,
   a log of the INSERT/DELETE records since each buffer's last BASE (header
   17 B (P): type, offset, length; plus the inserted bytes; doubling growth).
   When the opened generation equals the journalled BASE identity (all of
   size, mtime, inode, device, ctime, prefix CRC/length), install applies the
   logged ops to the tree with `piece_insert`/`piece_delete`, so the tree
   keeps ORIGINAL spans and the install checkpoint (and later ones) are 28u
   differences. Otherwise (retained base copy, untitled, changed file, log
   allocation failure, an op failing or a length mismatch) the whole
   recovered text replaces the tree as before. Test (c): install checkpoint
   = 1 DELETE of 3 B + 2 INSERTs of 2 B, 4292 B (4096 B checkpoint page + one
   VIEW/TABS/WINDOW metadata append, (P) 96 + 56 + 44 B) vs 2,109,440 B red.
   Note (28u decision 5 unchanged): modified texts of BASE <= FILE_PREFIX_MAX
   stay self-contained by design, so "small" here means within the startup
   scan bound but above FILE_PREFIX_MAX; the fixture is 2 * FILE_PREFIX_MAX + 7.
   The log costs at most the journalled insert bytes again (bounded by the
   journal size, itself within the scan bound).

5. **Save worker (session.c).** `session_node` carries the buffer's reference
   snapshot and its generation identity, retained at publication (so the
   worker never reads UI fields); `encode_leaf` uses it when that identity is
   the leaf's BASE and the tree's original is not, and BASE >
   FILE_PREFIX_MAX. Maps are built lazily per session slot in the capture and
   freed in `release`.

6. **No per-record allocation in session.c.** The 28u two-pass builder moved
   out of checkpoint.c into `editor_records_*` (private.h): sizing pass into
   one scratch record, then one record array + one `edit_arena` payload for
   the fill pass, with a count check. Both `editor_checkpoint` and the save
   worker's `prepare` use it. The worker allocates per capture: the ids array,
   scratch, records array, payload arena, and one map per referencing slot,
   independent of the record count (was one calloc per record).

## Numbers (all (M) on AC unless (P))

| case | red | green |
|---|---|---|
| (a) 1 GiB, save, +1 B checkpoint | 1,077,936,128 B, 4558.395 ms | 4096 B, 6.649 ms, 3 allocations (scratch, records, map) |
| (b) step 2 finish checkpoint (2 MiB) | 2,105,344 B | 4096 B |
| (b) step 3 later checkpoint (2 MiB) | 2,105,344 B | 4096 B |
| (c) recovered 2 MiB install checkpoint | 2,109,440 B | 4292 B |

Bounds (P): (a) `metadata_bound_with(path, 1, 32 + 8 + 1)` = one page: BASE +
TARGET + VIEW + TABS + WINDOW + one 1-byte INSERT + PAD header, page rounded.
(b) same with 2-byte INSERTs ("AB"+"CD" against the pre-save BASE, "CD" after
the save, "CD"+"EF" later). (c) one DELETE (48 B) + two 2-byte INSERTs plus
the metadata append. The 1 GiB fixture is 2^30 bytes (P), sparse.
Power/load stamps are in the worker report.

## Not addressed / limits

- External modification of the *old* generation's inode through another hard
  link after a mapped save changes the tree's (MAP_PRIVATE) bytes and the
  reference together, while the saved file keeps the saved bytes; a
  checkpoint would then claim kept bytes that differ from what is on screen.
  The editor already shows changed text in that case (file module's changed
  source handling); trial-irrelevant hardening, not done.
- Shared-target buffers (another buffer on the same path as the saved one)
  still checkpoint whole text after the save (unrelated trees).
- Evicted buffers with a save in flight (edit-5u2 "kept") keep the empty BASE
  + whole text shape by design (unchanged).
- Recovery still materialises each BASE in memory for the flat text (needed
  for the fallback and the clean check); the op log adds the journalled
  insert bytes once more.
