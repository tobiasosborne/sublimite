Fixed near-end prefix replay. Release regressions pass in both wrap modes, and `make all` passes.

Sanitizer functional checks pass; timing is tracked separately. Full `make check` remains blocked by X and local Unix socket permissions.

Red/green runs, failed suites and remaining verification are in [the worker report](/home/tobias/Projects/sublimite/.wt/edit-2ty/docs/worker-reports/edit-2ty.md). Stopped after this bead.