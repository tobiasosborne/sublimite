Fixed the worker-side allocator accounting and bounded/documented frame pin work.

Release headless `editor_test` passed 3/3 with zero allocations. Sanitizer Unicode and font suites passed. Full runs remain blocked by X/IPC access; the unrelated journal-kill failure is recorded.

Red/green runs, files touched and remaining checks are in the [worker report](/home/tobias/Projects/sublimite/.wt/edit-r1l-s6/docs/worker-reports/edit-r1l-s6.md). No `bd` or `git` commands were run.