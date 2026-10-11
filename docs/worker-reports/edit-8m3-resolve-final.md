Resolved all conflict markers while preserving both behaviours.

`make all`, sanitizer minimap/typing-flash tests, and release regressions passed. Of the non-display suites, 45 passed; IPC failed on denied socket bind. The full editor suite failed and stalled under sandbox restrictions, then was interrupted.

Hunk resolutions and test outputs are in [edit-8m3-resolve.md](/home/tobias/Projects/sublimite/.wt/edit-8m3/docs/worker-reports/edit-8m3-resolve.md). No `git` or `bd` commands were run.