No BLOCKER findings. One MAJOR and four MINOR findings.

1. **MAJOR — Missing shared cache preserves the half-installed-tab failure.** [src/editor/open.c:86](/home/tobias/Projects/sublimite/.wt/edit-m25u-s27/src/editor/open.c:86), [src/editor/buffers.c:903](/home/tobias/Projects/sublimite/.wt/edit-m25u-s27/src/editor/buffers.c:903).

   If the startup cache open fails, controllers still acquire individual cache descriptors. At the descriptor boundary, file preparation can succeed while the subsequent controller creation fails with EMFILE. The tab and journal records are already published; the request returns success with `b->save == NULL`, no rollback, and no exhaustion notice. This is an unresolved case of the original bug, acknowledged in the report.

   **Fix:** stage cache/controller acquisition before journal registration and tab installation, using the planned buffer IDs. Roll back every prepared resource on failure. Add an exhaustion test with the shared cache deliberately absent.

2. **MINOR — Unrelated failures can receive the exhaustion notice.** [src/editor/buffers.c:305](/home/tobias/Projects/sublimite/.wt/edit-m25u-s27/src/editor/buffers.c:305).

   The failure handler combines the worker’s recorded error with ambient UI-thread `errno`. After an earlier synchronous EMFILE, a worker can report EACCES while UI `errno` still contains EMFILE. The editor then incorrectly reports “too many open files.”

   **Fix:** capture native errors at their failing operation. Use the worker’s recorded error for asynchronous open failures; use a separately captured error for synchronous failures.

3. **MINOR — A hidden notice can prevent subsequent exhaustion notices.** [src/editor/buffers.c:891](/home/tobias/Projects/sublimite/.wt/edit-m25u-s27/src/editor/buffers.c:891).

   Refuse an open while the find panel is visible, then edit the document. The panel suppresses the notice, leaving `recovery_notice_shown` false. Consequently, painting does not clear its pointer after the mutation. Later refusals see the same pointer and skip updating `recovery_notice_mutations`; after closing the panel, the notice remains invisible.

   **Fix:** rearm when either the pointer or mutation generation differs, and reset `recovery_notice_shown`, following `input_notice()`.

4. **MINOR — The advertised over-ceiling rlimit handling cannot succeed.** [src/os/os_posix.c:1043](/home/tobias/Projects/sublimite/.wt/edit-m25u-s27/src/os/os_posix.c:1043).

   If `nr_open` was lowered below an inherited hard limit, clamping `target` still submits the oversized original `rlim_max`. Linux checks the requested **hard** limit against `nr_open`, so `setrlimit` returns EPERM and leaves the soft limit unchanged. The failure is safe, but the documented handling is ineffective. [Linux implementation](https://github.com/torvalds/linux/blob/master/kernel/sys.c).

   **Fix:** preserve the never-lower contract by explicitly treating this case as unsupported, documenting it, and testing unchanged limits. Correct the header’s claim about a larger *soft* value causing EPERM.

5. **MINOR — Tests leave the central transactional and shared-reload contracts unpinned.** [tests/ipc_test.c:480](/home/tobias/Projects/sublimite/.wt/edit-m25u-s27/tests/ipc_test.c:480).

   Exhaustion coverage uses single-path requests without a journal. An implementation that publishes the first path before failing the second could pass. The descriptor-count test also never exercises shared-cache reload backing or releases the creator’s reference before controller teardown.

   **Fix:** add a multi-path request crossing the limit with journaling enabled; verify descriptors, tab IDs, retained views, and recovered journal state. Add shared-cache fallback coverage after renaming/replacing its pathname and releasing the creator reference while controllers remain alive.

I found no shared-cache reference-counting/UAF defect, changed create/unlink directory authority, or typing-path allocation regression. POSIX declarations and implementation are guarded; today’s Windows build excludes the already Linux-specific `main.c`.

Warning-clean Linux syntax checks passed, as did `_WIN32` branch syntax checks for savectl and the guarded POSIX source. Runtime suites and a genuine Windows cross-build were not run. No files were modified.

LAND AFTER FIXES