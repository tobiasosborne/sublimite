Fixed: empty `session-*.lock` sidecars were counted as journals. Locks are now hidden, unused empty journals are removed, and permissions stay 0600.

CLI expectation changed to one journal: recovery replaces and retires the prior session.

ASan journal/savectl/session tests, editor save/startup probes, and `make all` pass. Full editor testing hit sandbox IPC/X11 restrictions; CLI awaits the coordinator.

[Worker report](/home/tobias/Projects/sublimite/.wt/edit-ck4/docs/worker-reports/edit-ck4.md) appended. No `git` or `bd` run.