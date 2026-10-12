Implemented Python/Julia grammar integration, tests and vendor pruning. `make all` and grammar ASan/UBSan tests pass.

Julia exceeds the size goal; explicit opt-in linking is available. Repository checks still have unrelated failures, and display suites need coordinator reruns on Xvfb `:99`.

[Report with red/green runs, measurements and remaining work](/home/tobias/Projects/sublimite/.wt/edit-v0o.4-s6/docs/worker-reports/edit-v0o.4-s6.md).