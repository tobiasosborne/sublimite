Implemented vendored runtime, snapshot parsing, viewport-first slices, incremental edits, tests, and edit fuzzing. Preserved helpers; no paint wiring or typing-path malloc. Warnings relaxed only for the vendored object.

`make all`, module release/sanitizer tests, and fuzz smoke pass. Full check/G1/G3 remain display-blocked; leak checking hit the sandbox restriction.

Hash recorded in the decision; [report appended](/home/tobias/Projects/sublimite/.wt/edit-v0o.2/docs/worker-reports/edit-v0o.2.md). No `git` or `bd` ran.