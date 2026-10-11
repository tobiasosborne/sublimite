Fixed the null timeout: visual-row queries discarded their input window at every soft wrap, causing repeated reads. They now retain it between rows.

`make all`, font and wrap suites pass. The whole editor suite has no remaining headless failures; typing reports zero allocations.

Raster P4 still needs coordinator verification because sandbox IPC setup fails first. Cause, fix and outputs are appended to [the report](/home/tobias/Projects/sublimite/.wt/edit-m3g/docs/worker-reports/edit-m3g.md).

No `git` or `bd` commands ran.