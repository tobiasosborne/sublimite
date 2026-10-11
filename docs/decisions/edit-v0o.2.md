# edit-v0o.2: static tree-sitter runtime and snapshot parse worker

This continuation supersedes the earlier source-acquisition block. The
coordinator supplied the official tree-sitter/tree-sitter release archive for
tag `v0.27.1` at `/tmp/ts-dl/tree-sitter-v0.27.1.tar.gz`. Its SHA-256, measured
with `sha256sum` and independently checked with Python hashlib, is:

```text
982cd3d4d9eb7be18c243240a622423fd2e4ecb4bec2cd98832c2f3fa1f0f333
```

Release tag and digest identify provenance; they are not performance quantities.
Only `lib/src/`, `lib/include/tree_sitter/`, and the root `LICENSE` were
extracted into `vendor/tree-sitter/`, stripping the archive's enclosing release
directory and preserving the upstream layout. A manifest check compared every
vendored file byte-for-byte with its selected archive member and rejected extra
or missing files. Upstream sources are unmodified, including the nested notices
inside `lib/src/`. No grammar or other archive directory was extracted.

The Makefile explicitly adds `vendor/tree-sitter/lib/src/lib.c` to the release,
sanitizer and fuzzer static archives. It uses the project's C11, optimization,
threading, dependency and sanitizer flags. The upstream amalgamation triggers
conversion warnings (captured in `build/hl-evidence/vendor-strict.log`);
`-Wconversion` and `-Wshadow` are relaxed **only for that object**. Project
sources, tests, fixtures and fuzz entry points retain
`-Wall -Wextra -Werror -Wshadow -Wconversion`.

## Slice budget and continuation

(P) A frame occupies one refresh interval. For a caller-supplied measured
interval `R_ns`, the parse invocation budget is `1 frame * R_ns/frame = R_ns`.
The existing `hl_frame_budget_ns` helper preserves this identity and refuses an
unknown/zero interval. No rate or fallback is guessed. `hl_parse_slice` records
its monotonic start time before setup, and the parser progress callback stops
when `now_ns - start_ns >= R_ns`. A stopped parser resumes on the same
snapshot, edited old tree and included ranges on the next invocation.
`hl_worker_run` uses `work_continue` on the existing bulk lease so another job
can run between slices.

Upstream checks progress after its `OP_COUNT_PER_PARSER_CALLBACK_CHECK` parse
operations (the upstream constant is 100, a runtime implementation detail,
not an application time budget). Deadline/cancellation checks also surround
setup and each incremental edit. Callback granularity, lexing a long token,
an external scanner, a single tree edit/copy/reset/delete, input reads, or OS
scheduling can overrun the frame interval. Actual elapsed slice duration must
be measured with the production grammar. Syntax errors still produce usable
trees; failed input or unavailable parser state produces `HL_PARSE_FAILED`.
A detected file backing fault also fails the request; consumers must continue
checking backing validity before using any previously delivered result.

## Viewport first and plain-text readiness

The first parse includes exactly the caller's viewport `TSRange`, with byte
and UTF-8 byte-column coordinates in the immutable snapshot. This produces
`HL_VIEWPORT_READY` before any document parse begins. The pending result is
retained until mailbox publication succeeds; a full mailbox yields the lease
and retries without advancing to document work. Only after that publication
does a later slice reset included ranges to the whole document and produce
`HL_DOCUMENT_READY`. An empty viewport/document is also parsed and published.
Viewport-only parsing can lack enclosing language context; full-document
parsing and later grammar/query integration can refine those results.

The original mailbox encoding/readiness helpers remain. Results contain only
a half-open byte range, phase and the transport generation. Trees stay in the
worker. A UI caller keeps plain text until `hl_viewport_ready` validates the
current generation and complete viewport coverage. A document result alone,
an old generation, an incomplete range or failure does not satisfy readiness.
The module adds no editor paint, query, theme, or grammar wiring. The editor
continues to render plain text.

## Snapshots, edits and allocation

`hl_worker` owns the parser and completed viewport/document trees. One request
uses a worker at a time. `hl_request_init` only initializes caller-owned
storage and validates metadata: no allocation, parser operation, tree edit or
source scan occurs on the typing path. Snapshots and ordered edit arrays are
borrowed; the caller retains them unchanged through `work_handle_finished`,
including cancellation. Source mutation/destruction cannot change a retained
piece snapshot. Discard a cancelled request's suspended state before reusing
or destroying its worker. Cancellation of a queued job can bypass the worker
entirely; `hl_request_discard` handles that case as well.

For a matching completed base generation, the worker copies the previous
document tree with `ts_tree_copy`, applies each `TSInputEdit` with
`ts_tree_edit`, checks the resulting byte length against the next snapshot,
and parses against that edited copy. Byte/point coordinates must exactly
match the mutation chain, including intermediate coordinates for multiple
edits. A missing/mismatched base falls back to a fresh parse. No-edit reuse
requires the same content generation. Generations identify exact content
snapshots within this worker's document identity. On cancellation/failure,
the previous completed document remains intact.

Tree-sitter uses its upstream allocator during initialization and worker
parsing; no process-wide allocator override is installed. These allocations
are outside input-to-submit. The earlier partial decision's proposed custom
worker arena was an unimplemented extension; the assigned runtime scope does
not require replacing upstream allocation. Release tests use the real
counting guard around request setup and mailbox/readiness operations.
ASan owns malloc, so its build uses the existing inert guard.

The input cache is one 4096-byte page-sized chunk on the project's Linux
platform, selected as worker-local storage, not a source-size/work budget.
Files above `UINT32_MAX` bytes are refused: this bound comes from tree-sitter's
uint32 byte-offset ABI. Those callers retain plain text. No arbitrary source
capacity or parser throughput estimate is introduced.

## Verification and busy-worker endpoint harness

The handwritten fixture in `tests/hl_fixture.h` has productions
`document -> empty | list` and `list -> unit | unit list`, with a token per
Unicode scalar. Its symbols/states follow those productions. It is compiled
only into tests, fuzzers and the benchmark, never the production static
library/editor. Tests cover real viewport/document parsing, publication order
and backpressure retention, snapshot lifetime, multi-edit insertion/deletion,
unchanged prior-tree lifetime, forced callback expiry/resumption, cancellation,
restart, failure, empty input, mapped-file invalidation, and actual pool
publication. Logical clock ticks in slice tests force expiry deterministically
and make no physical refresh/latency claim.

`fuzz/hl_edit_fuzz.c` selects replacements and viewport boundaries from input,
mutates the piece tree, parses immutable before/after snapshots, and compares
every visible node's symbol, bytes and points against a fresh parse. Seeds
include multiline, UTF-8, malformed bytes, NUL, empty input and input-cache
boundaries. The existing mailbox fuzzer remains.

`bench/hl_bench.c` calls the existing editor benchmark's G1 input-to-T4 and G3
tab-to-T5 rows while a dedicated bulk lease continuously parses fresh trees.
It accepts `--refresh-ns=INTERVAL` and skips when refresh is unknown. Sample
counts follow the existing endpoint harness contract. The stress worker uses
an independent pool and its results are consumed inside the benchmark;
production mailbox transport is covered by the worker tests. The existing
malloc guard counts the whole process, so endpoint allocation counts include
worker allocations; those counts cannot isolate typing allocations. This
harness measures CPU contention, not editor highlighting/paint integration.
The coordinator must run it on the assigned display with the measured refresh
interval and evaluate its timing rows alongside an idle baseline.

`make all` and the highlight release/sanitizer tests pass. Full `make check`
and the busy-worker endpoint attempt were blocked by the occupied display
lock; broader suite accounting is in the appended worker report. No all-green
repository check, parser-busy G1/G3 result, or visible-highlighting acceptance
is claimed in this sandbox. Leak checks and the comparison drive remain
coordinator validation.
