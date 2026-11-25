# v2-tk8 layout start, parked for render round three

The advisor moved render round three ahead of layout. The active checkout carries the viewport mutant receipt. The layout draft is parked under `build/layout-draft/` in this worktree, and the unfinished layout test is outside active suite registration.

- `build/layout-draft/src/layout/layout.h` sketches the signed layout API and documents ownership, credits, candidate lifetime and the dedicated navigation pool.
- `build/layout-draft/src/layout/layout.c` contains the initial storage types, arena prepayment, snapshot span reads, asynchronous reader submission and physical retirement, resident window borrowing and the rank/select helper. The draft has no integrated layout step yet.
- `build/layout-draft/tests/layout_test.c` is the first red fixture. It exercises CRLF and final-LF rows, style-run reuse, zero credit and accept-before-hit. The intended red failed to link the missing layout API. [Receipt](../../build/layout-run/red.log)
- `build/layout-deps/` contains exact development copies of the public render value header and the edit header from the available worktrees. This checkout's render and edit modules still expose smoke stubs. Normal layout compilation needs the reviewed render and signed edit declarations on its base.

The production A29 constructor and staging, viewport provider integration, Q10 dependency publication, S6 repair, S7 painting, S9 differential tests and the benchmark are pending. The draft and dependency copies are worktree-local. The signed contracts and their errata remain the integration authority.

After parking the draft and removing the layout suite registration, the active checkout built successfully with `nice make -j2 all`. [Build receipt](../../build/layout-run/parked-all.log)
