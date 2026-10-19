Implemented the peer checkpoint and retention lease fixes. Release build, new regressions, and malloc guard passed.

Coordinator reruns remain necessary: LeakSanitizer failed with its ptrace diagnostic, and `:99` socket access was denied. Synchronous exit behavior is documented.

[Worker report](/home/tobias/Projects/sublimite/.wt/edit-9fzc-s9/docs/worker-reports/edit-9fzc-s9.md)