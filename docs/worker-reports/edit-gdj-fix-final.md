Fixed the capacity fixture: it dispatched keys while cooperative cursor repair was pending, causing `VIEW_ERR_BUSY` (`5`). It now resumes repair between keys while keeping index maintenance paused. Production code needed no change.

Capacity/refusal tests, all five requested regressions, and `make all` pass. The full sanitizer suite ran but exited `1` on native initialization and checkpoint/replay failures.

Cause, fix, and outputs appended to [edit-whl.md](/home/tobias/Projects/sublimite/.wt/edit-gdj/docs/worker-reports/edit-whl.md:136). No `git` or `bd` commands ran.