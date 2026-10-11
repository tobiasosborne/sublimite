Resolved the conflict, preserving main’s clipboard dispatch and the config application hook. No `git` or `bd` was run.

- GCC `make all` passed with the required flags.
- ASan config, clipboard, and minimap-flash tests passed.
- `--ck4-save` failed its saved-content assertion; the full editor suite failed during recovery setup.

Details: [resolver report](/home/tobias/Projects/sublimite/.wt/edit-v0o.7/docs/worker-reports/edit-v0o.7-resolve.md).