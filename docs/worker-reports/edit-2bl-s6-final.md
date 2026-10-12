Fixed WINDOW v2 parsing, retained legacy coverage, and added BEGIN/COMMIT kill cases.

`make all` and all requested journal suites passed. Full-check verification has sandbox X/IPC blockers; leak detection was disabled.

Red/green output and outside-scope findings are in the [worker report](/home/tobias/Projects/sublimite/.wt/edit-2bl-s6/docs/worker-reports/edit-2bl-s6.md). Stopping after edit-2bl.