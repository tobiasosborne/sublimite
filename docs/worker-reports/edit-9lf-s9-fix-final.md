Fixed the test fixture: rapid same-size writes sometimes retained identical mtime/ctime. It now advances mtime explicitly. Both contracts and all assertions remain intact; production code is unchanged.

After `rm -rf build`, `make all` and the requested `make check` exited 0. All 63 sanitizer binaries passed. **Leaks were off.**

Cause and failing/passing transcripts: [edit-9lf-s9-fix.md](/home/tobias/Projects/sublimite/.wt/edit-9lf/docs/worker-reports/edit-9lf-s9-fix.md).