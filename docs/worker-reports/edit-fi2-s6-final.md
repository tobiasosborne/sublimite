Implemented edit-fi2: recovery now uses immutable originals and preserves edits after external rewrites or deletion.

`make all` and scoped/headless tests pass. Full checks still have X/IPC sandbox failures, the existing journal-kill mismatch, and the sibling orphan failure.

[Report with red/green evidence and remaining checks](/home/tobias/Projects/sublimite/.wt/edit-fi2-s6/docs/worker-reports/edit-fi2-s6.md).