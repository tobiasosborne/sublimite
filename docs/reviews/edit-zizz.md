Reviewed commit `75ee874` only. No files modified.

1. **BLOCKER — Equality can discard unsaved text from a changing file.** [src/editor/recovery.c:478](/home/tobias/Projects/sublimite/.wt/edit-zizz-s27/src/editor/recovery.c:478)

   `disk_equals` checks metadata before reading, but never validates the descriptor or pathname afterward. Concrete scenario: an intact retained BASE contains `A|C`, and replay produces unsaved text `A|B`, spanning two read chunks. The comparison reads `A`; another process writes the first chunk to `D`, then the second to `B`; the comparison reads `B` and returns true. The file never contained `A|B`.

   The planner can drop that buffer. Its immutable retained BASE produces no conflict, the replacement checkpoint omits it, and retirement deletes the source journal and retained copy. The unsaved text is lost. A successful comparison can also become stale during subsequent planning/install; the no-ops shortcut at line 502 bypasses current-target validation altogether.

   **Fix:** validate descriptor and pathname generations across comparison, retain the resulting identity, and revalidate drop candidates before committing retirement. Any change must cause replanning or preservation of the whole source and its BASE dependencies. Preserve durable replay bytes wherever a disappearing generation would otherwise leave no recovery copy. Add a deterministic test that changes the file between comparison chunks and before retirement.

**MAJOR: None.**

2. **MINOR — Retained-copy recovery hides the omitted-tab notice.** [src/editor/recovery.c:790](/home/tobias/Projects/sublimite/.wt/edit-zizz-s27/src/editor/recovery.c:790)

   A partial restore can correctly generate “M unmodified tabs … not reopened,” then install a retained-copy buffer whose target differs. Its `rewritten` flag makes this loop replace the entire notice with the generic “unsaved edits were recovered” message. The user loses the required explanation of missing tabs.

   **Fix:** append the recovered-edits message to the partial notice, as the conflict branch does, while preserving the first-paint notice’s intended priority. Test a partial restore with a divergent retained-copy target.

3. **MINOR — Restart tests do not verify surviving text.** [tests/editor_session_test.c:2814](/home/tobias/Projects/sublimite/.wt/edit-zizz-s27/tests/editor_session_test.c:2814)

   The initial checks inspect text, but the relaunch checks inspect only tab counts, notices, and journal names. A replacement checkpoint that preserved BASE/TABS records but omitted untitled INSERT payloads could restore empty buffers and still pass. Graceful shutdown also leaves opportunities for pending checkpoint work to complete.

   **Fix:** crash immediately after source retirement, then verify every unsaved buffer’s complete bytes and modified state, including closed buffers. Add partial-path checkpoint failure/BUSY tests and deterministic coverage for interrupted saves, mapped edits, retained-copy conflicts, occupied dirty closed pools, and PAINT handling.

The added partial-restore tests would fail against the parent’s whole-session deferral, so they catch the original bug. They do **not yet pin the no-loss guarantee**.

Outside the race above, capacity accounting reserves the temporary live slot and existing closed entries; active selection and unpainted-tab placement look consistent. Fitting sessions and explicit-journal recovery retain their previous path. Installation failures preserve the source through set-aside/retry, and BUSY checkpoint retirement remains deferred.

`disk_equals` runs only on the over-capacity startup path. Read/stat failures conservatively classify content as unsaved; no typing-path allocation was added. The notice allocation is exactly **240 bytes**, matching the longest format including NUL. The new calls use portable `os.h` APIs.

Clang syntax checks passed for both changed C files. Fixture tests were not run under the read-only constraint; a Windows cross compiler was unavailable.

LAND AFTER FIXES