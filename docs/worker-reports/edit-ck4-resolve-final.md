Resolved all three conflict hunks, preserving both behaviors. No `git` or `bd` run.

`make all` passed. ASan save, startup, minimap, journal, savectl, and file checks passed. Full `editor_test` failed on X11/IPC assertions and replay/piece-recycling timeouts; the replay timeout reproduced independently.

Hunks and outputs: [edit-ck4-resolve.md](/home/tobias/Projects/sublimite/.wt/edit-ck4/docs/worker-reports/edit-ck4-resolve.md).