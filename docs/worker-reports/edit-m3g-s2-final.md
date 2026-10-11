Implemented display-derived font/window sizing, worker atlas zoom/reset, and `EDIT_FONT_SIZE_PT`.

Red/green font tests and `make all` pass. Full `make check` remains unpassed due to display-lock and X/IPC socket restrictions. Native ink-height verification awaits the coordinator.

[Worker report]( /home/tobias/Projects/sublimite/.wt/edit-m3g/docs/worker-reports/edit-m3g.md) includes evidence, failed suites, derivations, and remaining checks.