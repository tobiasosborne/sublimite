Implemented the shared inotify service and required epoll integration. Targeted regressions, full file suites, and `make all` passed.

Full `make check` needs a coordinator rerun because the sandbox denies display and Unix socket operations.

[Worker report with red/green runs and remaining verification](/home/tobias/Projects/sublimite/.wt/edit-er2/docs/worker-reports/edit-er2.md)