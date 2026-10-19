# edit-dzk1: a render-rule violation drops or repairs the frame; a tab that dies in its first paint is restored closed

Follow-up to edit-2ghs (its worker report, out-of-scope items 1 and 2). The edit-2ghs font fix is not in this tree; the tests inject its zero-sized descriptor directly.

## 1. Render-rule violations in submit

`render_backend_submit` front-end checks are now a separate pure function, `render_backend_check` (render.c). It does exactly the checks submit ran before `ops.submit`, so behaviour is unchanged. When submit fails with anything other than BUSY, `submit` (editor.c) calls the check:

- **The check passes.** The backend's own submit failed. The error is returned as before, so the loop ends. A broken device is not a rule violation, and hiding it would freeze the window.
- **The check fails.** A render.h rule is broken. `render_rejected` handles it in four steps:
  1. It records `TRACE_RENDER_REJECTED` (frame id) in the trace ring, counts the rejection in `editor_stats.render_rejects` / `render_reject_cause`, and draws the notice on the status row (the existing notice mechanism, which clears at the next mutation).
  2. It calls `font_async_repair`, which runs `render_grid_repair` on the font cache's own descriptor table. Every descriptor that is not a real in-page rectangle becomes the baked ASCII space under its own `glyph_index`. That rectangle is a full cell with no ink, so retained cells stay valid and draw nothing. Cells that break a rule become background-only or lose an unpaired wide marker. Non-opaque colours are masked, and an invalid region is disabled.
  3. It resubmits the same frame once. render.h allows this: "rejected frames are retryable" and a failed submit "does not consume an ID".
  4. If nothing could be repaired (pages, storage, dimensions, capacity, strips) or the resubmit is still rejected, the frame is dropped. `dirty` clears, so nothing resubmits before the next change, and `render_dropped` makes that next frame full, because the backend never received the dropped damage.

Why not the edit-2ghs objection ("the next step builds the same frame again; frozen window, no visible error"): the repair changes the inputs, so the resubmitted frame is valid and carries the notice. A grid that cannot be repaired is dropped once per change, not once per step. It does not spin (the test checks that the loop settles). The user can still save and close. Before this change, the editor exited, and the next launch replayed the same paint.

Rejected: relaxing `render_atlas_validate`. That would change a frozen contract for every backend; edit-2ghs gave the same reason.

## 2. Crash-loop guard: journal PAINT record

New wire type `JOURNAL_PAINT` (11) with an empty payload. Its `buffer_id` is the buffer whose first frame of this launch is being painted, or zero when none is.

- `submit` calls `editor_journal_paint` before `editor_compose`, so a crash in composition, validation or the backend is covered. While `e->buffer` has not presented in this launch, the call appends PAINT(id). `journal_append` returns only once the record is in the page cache, so a process crash keeps it. After that, the check is one comparison per frame: one record per buffer per launch, no allocation, typing path intact. BUSY holds the frame for the next credit, as metadata does. Any other refusal paints without the guard. The rules for skipping are the same as metadata's: no tab journalled yet (the empty-placeholder contract), a save holding the fence, or recovery in progress.
- `present` → `editor_journal_painted`. The buffer is marked `painted`, then PAINT(0) is appended. If another unpainted buffer is active by then, PAINT names that buffer instead.
- A clean close (`close_result`, after a successful flush) appends PAINT(0). An error exit from `editor_run` does **not** append it: that error may be the paint failure itself.
- `editor_checkpoint` carries a pending PAINT, so a rotation inside a first paint keeps the marker.
- Recovery (`restore_record`) keeps the last PAINT id. `install_session` installs that buffer last and closes it, so it is the newest closed tab, the one Ctrl+Shift+T reopens. It also sets the notice "a restored tab died in its first paint last time; it is closed (reopen: Ctrl+Shift+T)". The active index shifts to the same position, or to its left neighbour. `session_fits` counts the tab as closed. Text and edits are kept. Reopening it paints it again, and if that kills the process, the next launch closes it again: no loop. The lazy large-journal scan (restore.c) ignores PAINT.

The guard covers every buffer's first paint, not only restored ones. A tab that dies the first time it is opened is therefore closed on the very next launch, without one more crash. The cost of a false positive (power loss inside a first paint, after the page cache reached the disk) is one closed tab with a notice. No data is lost.

## Known limits

- A save checkpoint (session.c capture, on the worker) does not carry the marker. A save that rotates inside a first paint loses it, which allows one more crash before the guard engages. The worker cannot read the UI's `journal_paint` without a race.
- `painted` is per launch, not per backend. A crash that only the GPU backend hits after raster already painted the tab during bootstrap is not guarded.
- Builds older than this type treat PAINT as an invalid record. Their `journal_replay_file` truncates the journal at the first invalid record, so an older binary recovering a journal written by this build loses every record after the first PAINT. Downgrades are not supported. Never point an old build at a journal directory a new build has used.

## Downgrade hazard and install rule (landing, s8)

This bead changes the journal format: it adds the `JOURNAL_PAINT` record (type 11). A binary built before it treats PAINT as invalid, and its `journal_replay_file` truncates the journal at the first PAINT record, so every edit journalled after that record is lost on recovery. Rule: reinstall the binary in the same step as landing this, and never run an older build against a journal directory this build has written.
