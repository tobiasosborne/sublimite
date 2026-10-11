Reconciled the IPC tests: kept main’s `t_astra_many_open`, removed overlapping 200-open tests, and retained the useful tab reservation and readable-error changes.

`make all`, ASan `tabs_test`, and listener-free IPC checks pass. Full ASan `ipc_test` and `editor_test` remain blocked by sandbox IPC/Xvfb restrictions; the requested full-pass requirement is unmet.

Appended evidence to [edit-jmn.md](/home/tobias/Projects/sublimite/.wt/edit-jmn/docs/worker-reports/edit-jmn.md:267). No `git` or `bd` ran.