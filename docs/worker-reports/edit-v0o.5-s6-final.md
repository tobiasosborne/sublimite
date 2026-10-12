Implemented edit-v0o.5. TypeScript is opt-in; TSX was omitted after size measurement.

`make all` and grammar probes pass. Non-display failures remain in `file_kill_test`, `ipc_test` and `journal_kill_test`. Display and LeakSanitizer checks need coordinator reruns.

[Report with red/green runs, measurements and all changes](/home/tobias/Projects/sublimite/.wt/edit-v0o.5-s6/docs/worker-reports/edit-v0o.5-s6.md). Stopping after this bead.