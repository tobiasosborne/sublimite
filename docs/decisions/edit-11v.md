# edit-11v — editor highlighting publication

Bead ids, grammar releases and compiler versions are provenance. Ordinals,
empty values and return codes in the implementation are (P), derived from
C and the existing mailbox ABI. Fixture offsets and geometry are (P), derived
from literal bytes and existing editor reservations.

The editor owns a serialized highlighting lease on its existing bulk pool.
After completed layout it pins the active buffer's immutable piece snapshot,
copies visible grapheme byte ranges into reserved storage and submits without
waiting. Piece storage uses the editor's existing src/base arena allocator.
No tree-sitter initialization, query compilation, source scanning or tree
access happens in the input-to-submit path. Request pointers stay inside the
borrowed worker argument allowed by hl_request_init; only encoded values cross
the result mailbox. Buffer retirement retains the owner until physical lease
completion. Close cancels and waits outside typing, before buffer teardown.

Request identity includes buffer, source generation, revision, viewport and
geometry. Open, edits, scroll, wrap, resize, reload and save-as language changes
therefore request a replacement. Rapid changes cancel logically and coalesce
while the old worker retires. The UI never reuses a parser, snapshot or request
until work_handle_finished and mailbox reservations are drained. Identity
exhaustion and unsupported tree-sitter source lengths leave usable plain or
stale text rather than wrapping identities or truncating offsets.

Refresh comes from the first active X11 RandR CRTC mode timings at startup: (P)
`htotal * vtotal * ns_per_second / dot_clock`, with protocol interlace and
double-scan adjustments. The SI conversion is (P) `ns_per_second = 1000000000`.
Subsequent physical refresh observations can supply elapsed time divided by
elapsed MSC. Unknown refresh remains unknown, as the parse contract requires;
synthetic null-backend submit timestamps cannot create a refresh rate. Tests
supply a physical observation fixture using the previously recorded panel
rate in perf/00-hardware-profile.md: (M)[bat] `90 Hz`, hence (P)
`1000000000 / 90 ns`. This is test input, not a new display measurement.

Workers calculate exact viewport row/byte points, call hl_request_init and
hl_parse_slice, query the completed viewport tree, and then continue the full
document parse. The full tree queries the same viewport again to improve
context such as multiline comments. Both complete query batches publish
HL_VIEWPORT_READY covering the requested bytes. Missing incremental edit chains
force a fresh parse; no incorrect base tree is reused across buffer identities.
The existing incremental parser API and busy-parser benchmark remain intact.

Capture priority follows upstream query pattern order. Workers resolve scopes
only at the copied visible grapheme starts, including tab/wide-cell aliases,
so storage is bounded by the existing reserved cell geometry, even for long
lines with arbitrarily many captures. Each resolved nonempty cell byte range
uses hl_span_encode, including default scope ranges. Publication resumes from
the last acknowledged span when the mailbox fills; it does not restart a
partial query. The final hl_result closes the batch. UI storage consists of
worker, staged and published arrays: (P) array count is the number of these
named ownership roles, each with capacity equal to reserved cells. Only a
complete successful current batch swaps into paint. Errors and cancellation
retain the previous colours, and new buffer identities start with plain text.

Paint looks up the byte range and maps its scope through the current dark or
light config theme. Cell foreground is part of the existing frame. Existing
caret, selection, bracket and error attributes retain their precedence; syntax
does not introduce new font styles. Theme changes need no parser allocation.
The parser/query worker alone uses the existing thread-local counting-guard
exemption: upstream worker allocation is allowed, while UI submission and
paint remain counted. No allocator guard or G1 threshold was changed.

Extension dispatch strongly references the default C/Rust/Python/JavaScript/
JSON/TOML/YAML set. GNU weak references keep Julia/TypeScript/Markdown/Markdown
inline/LaTeX/Bash optional under the existing HL_OPTIN links. Both configured
compilers support this extension; no Makefile edits are required. Unknown
extensions remain plain. Embedded query bytes are regenerated from unmodified
vendored highlights.scm; escaping question marks prevents C trigraph rewriting.

Capture families inherit their common syntax theme key. Boolean captures use
constant, character captures use string, and keyword.operator uses operator.
Private underscore captures are predicate-only; unknown names remain default.
The query layer supports the exact vendored uppercase expressions, anchored
builtin alternatives, leading-dash matches, equality, any-of and ancestor
predicates. Unsupported predicates fail closed. As with a highlights-only
Tree-sitter client without a locals query, JS local-binding metadata is absent;
its is-not-local predicate is true. A Markdown-inline-only link uses that
grammar for Markdown extensions when the block grammar is absent. TypeScript uses its supplied highlights
query; no JS query inheritance or injected-language parsing is invented.

Minimap syntax colour, injected languages, full Sublime selectors and font
styles are outside this bead. Unknown Wayland/native refresh can delay syntax
until physical cadence observations exist; the platform currently discards
Wayland's advertised refresh interval, which is recorded for coordinator
follow-up rather than expanding this bead into platform work.
