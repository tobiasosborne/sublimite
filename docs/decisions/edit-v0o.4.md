# edit-v0o.4: Python and Julia grammar integration

## Build and allocation

Keep the coordinator-vendored parser, parser ABI headers, highlights query and
LICENSE verbatim. Each scanner has one local ABI declaration normalization:
`tree_sitter_<language>_external_scanner_create()` becomes `create(void)`.
Both generated parsers and runtime declare the callback with a void prototype;
Clang's UBSan function-type instrumentation rejected the upstream old-style
definition. Preserve the release tarball digest and append this local change
to each PROVENANCE. This avoids suppressing any sanitizer checks. Delete grammar.json, node-types.json,
tags.scm, injections.scm and locals.scm where present: these are generator or
other query-layer inputs, not build/runtime dependencies for this bead.
Release tags and hashes are provenance identities, not measured quantities.

Each language has a wrapper with its upstream entry point and static borrowed
TSLanguage pointer. `src/hl/lang.h` has only this bead's Python and Julia
language declarations, one per line, plus shared include/guard boilerplate.
The wrappers allocate nothing; parsing and query compilation remain worker-side
operations. This bead does not add dispatch, predicates, theme scopes or paint.

The language-specific Makefile additions append parser.c and scanner.c to
SRCS, after its existing assignment, putting them in the same release,
sanitizer and fuzzer archives as the runtime. TS_SRC remains the original
single runtime source: its existing target expansion assumes a single path.
The final build-contract red/green probe records that invariant in the report. Each object uses
its own grammar's src include directory before the runtime/public includes,
so the scanner and generated parser share the upstream internal ABI headers.
Project files retain all strict warnings. Vendored objects retain C11,
Wall/Wextra/Werror, dependency flags and configuration optimization/sanitizers.
Only their conversion and shadow warnings are relaxed, matching the runtime;
Python additionally uses `-Wno-unused-value` because array_pop's result is
intentionally discarded, and Julia uses `-Wno-unused-parameter` for scanner
callbacks with unused fixed-ABI parameters. Strict scanner compiler evidence
is in build/hl-evidence/*-strict.log. No generated parser/header is patched; the scanner prototype changes are
documented above.

## Tests

The failing test was added before the language API/build implementation.
Each grammar probe checks the runtime's own minimum/maximum ABI macros,
requires ts_parser_set_language to accept the language, rejects ERROR/missing
nodes via the root's recursive has_error flag, and compiles the complete
vendored highlights.scm using ts_query_new. Python exercises decorators,
indentation, annotations and f-string interpolation. Julia exercises a module,
typed function, nested block comments and string interpolation. Query lengths
come from the actual file size; source lengths come from sizeof, excluding the
C terminator. (P) Numeric identity constants follow C/API contracts, not time,
size or count budgets. The release counting allocator also probes both wrappers.

## Capture inventory for edit-v0o.3

These names are derived from the vendored highlights.scm capture tokens;
the test also prints ts_query_capture_name_for_id for each compiled query.
The Julia `_pipe` capture is a predicate helper, not a visible theme scope.
Compiling a query does not implement its predicates; predicate evaluation is
the other worker's shared query-layer scope.

### Python

`@comment`, `@constant`, `@constant.builtin`, `@constructor`, `@embedded`, `@escape`, `@function`, `@function.builtin`, `@function.method`, `@keyword`, `@number`, `@operator`, `@property`, `@punctuation.special`, `@string`, `@type`, `@variable`.

### Julia

`@_pipe`, `@boolean`, `@character`, `@comment`, `@constant.builtin`, `@function.builtin`, `@function.call`, `@function.macro`, `@keyword`, `@keyword.conditional`, `@keyword.conditional.ternary`, `@keyword.exception`, `@keyword.function`, `@keyword.import`, `@keyword.modifier`, `@keyword.operator`, `@keyword.repeat`, `@keyword.return`, `@keyword.type`, `@number`, `@number.float`, `@operator`, `@punctuation.bracket`, `@punctuation.delimiter`, `@punctuation.special`, `@string`, `@string.documentation`, `@string.escape`, `@string.special`, `@string.special.symbol`, `@type`, `@type.builtin`, `@type.definition`, `@variable`, `@variable.builtin`, `@variable.member`.

## Compiled size and Julia opt-in

(M) [AC]: BAT0/status was `Not charging` immediately before the measurements.
The command output is saved in build/hl-evidence/size.log. `du -h` reports `23M`
for Julia parser.c; `wc -c` reports 24,076,582 source bytes. The source count is
not a compiled-size claim. GNU `size` on gcc release objects gives bytes:

| Component | text | data | bss | text + data + bss |
| --- | ---: | ---: | ---: | ---: |
| Python parser | 458293 | 2768 | 0 | 461061 |
| Python scanner | 3672 | 0 | 0 | 3672 |
| Julia parser | 2621504 | 2296 | 0 | 2623800 |
| Julia scanner | 1443 | 0 | 0 | 1443 |
| Runtime | 181883 | 32 | 0 | 181915 |

Every table entry is (M) [AC], directly from `size`; the final column is its
sum. Python = 461061 + 3672 = 464733 bytes; Julia = 2623800 + 1443 = 2625243
bytes; these grammars plus runtime = 464733 + 2625243 + 181915 = 3271891 bytes
(M) [AC]. This is loaded object-section accounting, excluding debug information,
ELF headers, wrapper objects, linker padding and the other worker's grammars.
It cannot establish the whole-editor/all-grammar size goal.

The supplied P6.3 acceptance is “binary <= 2 MB total.” (E: supplied planning
goal, no physical derivation provided in this bead.) Julia alone already
exceeds it under either decimal MB or binary MiB conventions. `size -A` finds
Julia parser's .rodata alone is 2533858 bytes (M) [AC], exceeding the goal.
Its upstream parser explicitly disables function optimization for both gcc and
clang; a command-line -Os override cannot remove the parse tables. Do not
patch generated tables or claim that -Os solves this miss.

The cheap opt-in is archive extraction: default `build/sublimite` has no Julia
reference and therefore omits its wrapper/parser/scanner. New explicit target
`make build/sublimite-julia` links the same main object and static archive with
`-Wl,--undefined=hl_language_julia`, deliberately extracting the wrapper and its
grammar/scanner dependencies. No shared Makefile pattern or existing editor
link rule changes. Both language probes still run unconditionally in the
normal test suite; Julia is never silently skipped. The opt-in target does
not enable editor highlighting: registration/query/paint integration remains
outside this bead. Future dispatch must preserve an optional Julia reference
rather than unconditionally taking its wrapper address in the default editor.

Default/opt-in linked measurements and symbol verification are recorded in the
worker report. The combined grammar goal remains missed when Julia is selected;
this opt-in keeps that choice explicit, it does not make the goal pass.

## Verification boundary

`make all` and both grammar probes pass in release and ASan/UBSan builds.
Both languages report the generated ABI identity accepted by the runtime's
minimum/maximum macros. Scanner prototype normalization retained all sanitizer
checks. LeakSanitizer needs a run outside the sandbox's tracing environment.
Full repository acceptance is not claimed: X/IPC socket restrictions,
existing IPC launcher/journal/line-index fixture failures and an occupied
display-lock contract remain coordinator rerun/triage items. Exact results,
out-of-sandbox reruns and red/green output are in the worker report.
