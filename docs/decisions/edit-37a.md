# edit-37a: saved identity after branching undo

Use the existing `saved_lost` flag when `remember()` discards a redo branch
containing the saved state. Set it before assigning `history_count` from
`history_cursor`, whenever `saved_cursor` is beyond that cursor. A new edit
can then reuse the same numerical cursor without being mistaken for saved
bytes. This mirrors the adjacent invalidation of an in-flight save's cursor.

Keep the comparison strict: a saved state at the branch point remains
reachable by undo and must still become clean. Existing save completion can
establish a fresh valid marker; existing burst-extension and history-eviction
handling remain responsible for their own marker invalidation.

The inherited regression uses the null renderer and the real save, undo,
typing, source-check and external-replacement paths. It verifies both the
discarded saved state and the retained saved branch point. The production
change adds no allocation, error path, numeric constant or storage.
