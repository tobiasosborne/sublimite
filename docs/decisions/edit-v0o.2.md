# edit-v0o.2: highlight mailbox contract; runtime acquisition blocked

This is a partial implementation of the assigned runtime bead, not a completed
parser or an accepted highlighting feature.

The mailbox result contains a half-open byte range and a publication phase.
The existing `work_msg.generation` identifies the immutable source snapshot.
No tree pointer crosses into the UI. The UI readiness predicate requires a
viewport publication for the current generation covering its entire requested
range. Before that publication, or after an edit invalidates the generation,
the caller keeps plain text. Document completion alone does not authorize a
viewport highlight. Encoding uses individual fixed-width fields, excluding
struct padding, and checks the payload against `WORK_MSG_DATA`.

The slice budget contract is (P) one frame multiplied by the caller's measured
refresh interval: `budget_ns = refresh_interval_ns`. The helper refuses an
unknown interval. No refresh rate, parser throughput, or default arena capacity
has been estimated. There is no parser yet to enforce this budget; the future
worker must use a cancellable progress callback and continuation and document
its callback-granularity overshoot.

The requested upstream runtime is not available in this worktree. Shell
retrieval failed with `curl: (6) Could not resolve host: codeload.github.com`
(M: curl's documented DNS error status, reproduced from this attempt).
The available web transport could read some tagged upstream source pages but
could not retrieve `lib/src/alloc.h`, and its rendering collapsed the original
line structure in `get_changed_ranges.c`. An alternate tagged header retrieval
also omitted angle-bracket includes. Reconstructing or silently replacing
that source would not constitute trustworthy vendoring. No such source has
been written into the tree, and no system shared library substitutes for it.

The attempted version `v0.25.10` is an upstream release identifier, not a
performance quantity. References:

- https://raw.githubusercontent.com/tree-sitter/tree-sitter/v0.25.10/lib/src/lib.c
- https://raw.githubusercontent.com/tree-sitter/tree-sitter/v0.25.10/lib/include/tree_sitter/api.h
- https://raw.githubusercontent.com/tree-sitter/tree-sitter/v0.25.10/lib/src/alloc.h

Continuation needs a complete upstream release archive or locally supplied
runtime source. That continuation must vendor the licence and transitive
headers/sources, compile the static runtime, implement worker arena allocation,
viewport-first parse and document continuation, and apply `ts_tree_edit` to the
prior tree against the next immutable snapshot. It must test real parsing,
incremental reuse, cancellation and result lifetime with a minimal grammar,
then measure typing and tab-switch endpoints with the parser busy. No editor
paint wiring belongs in this bead.
