Reviewed HEAD `34699c5`, read-only. **The BLOCKER is not fully closed.**

**BLOCKER — Keeping the source does not preserve a mutable BASE’s bytes.** [src/editor/recovery.c:835](/home/tobias/Projects/sublimite/.wt/edit-zizz-s27/src/editor/recovery.c:835)

A source can contain deltas against an ordinary file larger than `FILE_PREFIX_MAX`, with retention unfinished when the original session crashed. Concrete failure:

1. Recovery successfully materializes BASE plus edits as text `S`.
2. Another process writes `S` to the target. `disk_equals` correctly returns true, and the planner drops that buffer.
3. Before checkpointing, the process overwrites the target with unrelated bytes.
4. The replacement checkpoint omits the dropped buffer. The source is renamed, but contains only deltas referencing the now-missing BASE generation. `fini` frees the materialized `S`.

The complete unsaved buffer is unrecoverable. Retirement’s reference pinning protects existing private copies; it cannot preserve an overwritten ordinary BASE.

**Fix:** durably preserve the materialized text of dropped buffers whose source lacks an immutable BASE dependency, before releasing recovery memory. Keeping the original journal alone is insufficient.

The race test misses this: [tests/editor_recovery_test.c:172](/home/tobias/Projects/sublimite/.wt/edit-zizz-s27/tests/editor_recovery_test.c:172) supplies `"m"` as BASE text, and line 219 uses `journal_scan_file`, which skips BASE validation. Add a delta fixture above `FILE_PREFIX_MAX` without a retained BASE, overwrite the target after comparison, and reconstruct the omitted text using only persisted artifacts.

**MAJOR: None.**

**MINOR — The new notice hides the first-paint recovery warning.** [src/editor/recovery.c:821](/home/tobias/Projects/sublimite/.wt/edit-zizz-s27/src/editor/recovery.c:821)

When partial recovery closes an unsaved PAINT-marked buffer and drops another clean buffer, installation sets the warning explaining the closed tab and Ctrl+Shift+T. Because that warning is not `session_notice`, the new `else` replaces it with “the old session journal was set aside,” contrary to the documented priority.

**Fix:** preserve the first-paint warning when adding the set-aside notice; cover PAINT plus partial recovery with dropped buffers.

By inspection, the new tests fail against round 1, and the crash-relaunch byte checks meaningfully strengthen coverage. Rename failure leaves the source present; the kept/BUSY path avoids deferred unlinking; retirement pins actual retained BASE copies. Those protections do not close the mutable-BASE failure above.

Clang syntax checks passed. Fixture tests were not run because they write files. No files modified.

DO NOT LAND