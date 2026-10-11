# hl — edit-v0o.2 runtime continuation

The official tree-sitter v0.27.1 runtime is vendored verbatim and built into the
release, sanitizer and fuzzer static archives. `hl.h` / `parse.c` implement a
snapshot parse worker with refresh-derived resumable slices, viewport-first
publication, full-document continuation and incremental `ts_tree_edit` on a
copy of the prior completed tree. The original mailbox/readiness helpers stay
in place. Request setup and mailbox/readiness operations allocate nothing.
Tree-sitter initialization and worker parsing use upstream allocation.

Results are pointer-free mailbox values. Snapshots and edit arrays remain
caller-owned through physical lease completion. Deadline checks are
cooperative; upstream callback/lexer/tree-operation granularity can overshoot
a refresh interval. Source fault detection fails closed. No paint/query/theme
or production grammar wiring was added; the editor remains plain text.

Real parse, slice, cancellation, edit-chain, snapshot, mapped-fault and pool
ordering tests pass in release and ASan/UBSan builds. The edit fuzzer compares
incremental trees against fresh trees; the mailbox fuzzer remains. The
busy-parser G1/G3 endpoint harness is available as `build/bench/hl_bench
--refresh-ns=INTERVAL`, using the caller's measured display interval.

`make all` passed. Full `make check` and busy-parser endpoint validation are
blocked by the occupied display lock in this environment; IPC socket setup is
also sandbox-denied. The coordinator must rerun display/leak checks and the
comparison drive. Details and provenance hash are in
`docs/decisions/edit-v0o.2.md` and the appended worker report. No git/bd ran.
