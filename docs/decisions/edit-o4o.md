# edit-o4o — typing checkpoint diagnostics

The G1 checkpoint reports the backend and workload in the same row name used
by the timing verdict, all four failure predicates (`invalid`, `incomplete`,
`guard`, `suspended`), and actual/expected completion counts. It reports the
allocation count as well. Diagnostic output runs after disabling measurement
and ending any active allocation guard; the report preserves the original
guard flag and includes the last guarded span's count.

The reproduced allocation-count failures are distinct from the four checkpoint
predicates. A bounded gdb probe stopped in a raster completion worker:
`malloc -> libxcb -> xcb_poll_for_reply -> fence_job_fn -> worker_main`.
The guard is process-wide, so exempt library IO on that worker can be counted
while the UI guard is active. This does not confirm an editor/raster law-2
allocation. Attribution/filtering changes in the base guard are outside this
bead; no production source is changed on this evidence.

The existing allocation guard in `src/base/malloc_guard.c` records a count
only. Its public API exposes no allocation address or site. The diagnostic
therefore prints `allocation_site=unavailable_count_only_guard` explicitly.
Adding allocation-site recording to the base module is outside this bead's
allowed source files. An active `guard` flag means a missing closing submit
boundary; it alone does not prove an allocation occurred.

`--self-check-typing` exercises each checkpoint failure independently, checks
the diagnostic identity and all predicate fields, and checks that structural
failure dominates success/refusal during exit aggregation. The clean
checkpoint emits no failure record. The tests are benchmark self-checks,
requiring no display or new global state.

Reproduction and validation evidence, including remaining requirements, is
in `docs/worker-reports/edit-o4o-s9.md`.
