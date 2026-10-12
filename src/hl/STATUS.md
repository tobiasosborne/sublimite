# hl — edit-11v editor paint integration

The editor now submits immutable active-buffer snapshot leases after completed
open/edit/scroll layout, queries the viewport-first and completed document trees,
and publishes pointer-free per-cell scope ranges through the existing mailbox.
Paint maps scopes to the current dark/light theme; new buffers start plain,
stale batches keep the previous colours, and partial/failed batches never land.
Prepaid arrays derive from reserved frame cells; parser/query allocations stay
on the bulk worker. The highlighting-enabled counting-allocator case remains
zero on the UI typing path. Physical refresh is required by the parse contract;
X11 mode timing supplies it at startup, or physical observations supply cadence.
Unknown cadence keeps plain text until available (including Wayland startup
whose advertised interval is not exposed by the existing platform interface).

Default extension dispatch links C/Rust/Python/JavaScript/JSON/TOML/YAML.
Julia/TypeScript/Markdown/Markdown inline/LaTeX/Bash remain HL_OPTIN links.
All vendored highlights queries are embedded and their predicates/scopes run
through the production layer. Minimap syntax, injected languages and full
Sublime selectors/styles remain outside this bead. Decisions and validation:
docs/decisions/edit-11v.md and docs/worker-reports/edit-11v-s6.md.

The following sections describe the preceding grammar/runtime landings.

# hl — grammars landed (edit-v0o.3/.4/.5/.6, 2026-10-13)

Thirteen vendored grammars build into the release, sanitizer and fuzzer
archives: C, Rust, Python, Julia, JavaScript, TypeScript, JSON, TOML, YAML,
Markdown, Markdown inline, LaTeX, Bash (TSX omitted). Each has a `lang.h`
wrapper and an `hl_test` probe (ABI, ERROR-free fixture, highlights query
compiles). The query layer and embedded queries cover C and Rust only. No
editor code references a wrapper yet, so the default binary links none;
`HL_OPTIN` in the Makefile lists the grammars a future dispatch keeps
optional (`make build/sublimite-<lang>`). Sizes and the P6.3 miss are in
`docs/decisions/edit-v0o.3.md`, Landing.

# hl — edit-v0o.3 C/Rust grammar and query layer

C/Rust language wrappers, embedded highlight queries, capture-to-theme scopes,
worker-only query initialization and row-range querying are available in
`lang.h` / `query.h`. Pointer-free scope spans use a sibling of the existing
hl mailbox contract. Dark/light syntax colours come from installed Sublime
Mariana/Breakers. Runtime-owned completed trees remain on the worker. No editor
or paint consumer was added; the assigned successor handles that wiring.

C/Rust query tests pass in release and ASan/UBSan with LeakSanitizer disabled
for this sandbox's thread-inspection restriction. The Rust scanner's empty
parameter list was made explicit `(void)` to match the runtime C11 callback
ABI; no sanitizer checks were disabled for it. The fuzzer compares query scopes
from incrementally edited trees with fresh trees. Build/test/size evidence and
remaining coordinator validation are recorded in
`docs/worker-reports/edit-v0o.3-s6.md` and the corresponding decision document.

The grammar object measurements exceed their equal share of the supplied
shared binary-size target; do not treat size acceptance as achieved. Display
and other sandbox-dependent suite outcomes are detailed in the report.

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
a refresh interval. Source fault detection fails closed. The runtime bead added no paint/query/theme
or production grammar wiring; the editor remains plain text.

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
