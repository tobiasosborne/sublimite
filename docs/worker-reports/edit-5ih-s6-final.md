Implemented config font selection, hot reload, and CLI overrides: CLI > environment > config.

`make all`, scoped tests, and headless editor suites pass; typing allocation checks remain green. Full verification still has sandbox LSan/X blockers and unrelated journal/IPC failures, left untouched.

[Worker report](/home/tobias/Projects/sublimite/.wt/edit-5ih-s6/docs/worker-reports/edit-5ih-s6.md) includes red/green runs. [Decisions](/home/tobias/Projects/sublimite/.wt/edit-5ih-s6/docs/decisions/edit-5ih.md) records exact precedence and new G1/G3 baselines.