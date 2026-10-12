# edit-v0o.6: Markdown, LaTeX and Bash grammars

## Build and borrowed language API

Followed the finished sibling edit-v0o.4 worker using ordinary `diff -u` of its
Makefile and test against this worktree, and reading its wrappers/header/decision.
No git or bd command ran. Each parser has an allocation-free wrapper returning
upstream's static borrowed language pointer. `src/hl/lang.h` contains only this
bead's declarations and shared guard/include boilerplate for the mechanical merge.
Both Markdown grammars retain their upstream nested directory layout.

Append a source-list line per parser/scanner pair to SRCS; leave the original
single-runtime TS_SRC and its object rule unchanged. Grammar object rules use
the grammar's own src include directory first in release, sanitizer and fuzzer
configurations. Relax conversion and shadow warnings for vendored objects only,
matching the sibling and runtime. Bash additionally needs unused-value relaxation
for deliberately discarded array_pop results. LaTeX needs unused-parameter
relaxation for stateless scanner callbacks, and unused-label/unused-but-set-variable
relaxation for the generated empty keyword lexer's START_LEXER scaffolding.
Markdown needs neither extra relaxation. Strict compiler probes are retained in
build/hl-evidence; project code retains every required warning and sanitizer.

Normalize the upstream empty scanner-create parameter lists to `(void)` for
Markdown inline, LaTeX and Bash, as in the sibling. The block scanner already
uses the correct prototype. This matches the generated callback declarations
without disabling Clang's function-type sanitizer. PROVENANCE records the edits.

## Missing LaTeX artifacts and pruning

The coordinator-supplied LaTeX archive contains grammar.js and scanner.c but no
parser.c, src/tree_sitter headers, or highlights query. Confirmed the original
local archive also lacks these files. Generated parser.c and ABI headers from
that supplied grammar with the official Tree-sitter generator matching the
vendored runtime release. The exact generator URL/version/digest and grammar
input digest are in LaTeX PROVENANCE. Added a local highlight query using actual
generated node names for comments, commands, environment delimiters, math,
section titles, verbatim text, labels and URIs. It is project-authored, not an
upstream query; source archive provenance remains intact.

Pruned all vendored grammar directories to parser.c, scanner.c,
src/tree_sitter/*.h, queries/highlights.scm, LICENSE and PROVENANCE. Markdown
keeps both grammar subdirectories under the common LICENSE/PROVENANCE root.
Removed generator metadata, grammar.js, package/build/language-binding metadata,
scripts, readmes, hidden configuration files and every other query. The report
lists every deleted file. No runtime/build dependency uses the removed files.

(P) Numeric identities in C/API tests are success/failure values, first byte,
empty counts, or one byte/element; lengths derive from sizeof/ftell and integer
limits from the API types. Generated vendored numeric tables derive from grammar
states/transitions and scanner syntax, rather than invented capacities or time
budgets. Versions, dates, bead IDs, source locators, compiler flags and hashes
identify supplied artifacts/specifications, not performance claims.

## Tests and capture inventory for edit-v0o.3

The test was written and observed failing before adding the wrappers/header/build
rules. Each probe checks the runtime's minimum/maximum ABI macros and requires
ts_parser_set_language acceptance. Root has_error rejects ERROR and missing nodes
recursively. A single Markdown document includes a heading, fenced code block
and inline/strong emphasis and is parsed by both grammars. LaTeX exercises a
section, command, inline math and scanner-driven verbatim environment. Bash
exercises a function, conditional, expansion, pipeline and scanner-driven heredoc.
Every complete highlights query compiles with ts_query_new. The wrapper calls
also run under the release counting allocator; parser/query allocation is confined
to the standalone test, outside the typing path.

The capture names below come from ts_query_capture_name_for_id on the compiled
queries, including Markdown's @none sentinel. Predicate execution and scope/theme
mapping belong to the shared query worker; compiling queries does not implement
those features. No dispatch or editor highlighting is claimed by this bead.

### markdown

`@none`, `@punctuation.delimiter`, `@punctuation.special`, `@string.escape`, `@text.literal`, `@text.reference`, `@text.title`, `@text.uri`.

### markdown_inline

`@punctuation.delimiter`, `@string.escape`, `@text.emphasis`, `@text.literal`, `@text.reference`, `@text.strong`, `@text.uri`.

### latex

`@comment`, `@embedded`, `@function`, `@keyword`, `@punctuation.delimiter`, `@text.literal`, `@text.reference`, `@text.title`, `@text.uri`.

### bash

`@comment`, `@constant`, `@embedded`, `@function`, `@keyword`, `@number`, `@operator`, `@property`, `@string`.

## Size and explicit archive extraction

The supplied “binary <= 2 MB total” line is (E), a coordinator planning goal
without a physical derivation supplied here. It is not silently reinterpreted
as an object-only limit. All measurements below are (M) [AC]: BAT0/status was
`Not charging` immediately before measurement. Object values are GNU size bytes;
final column sums text + data + bss. Wrapper contributions are also included.

| Parser | parser sections | scanner sections | wrapper sections | sum |
| --- | ---: | ---: | ---: | ---: |
| markdown | 333039 | 12601 | 89 | 345729 |
| markdown_inline | 334527 | 2561 | 89 | 337177 |
| latex | 3554648 | 1003 | 89 | 3555740 |
| bash | 1311472 | 10774 | 89 | 1322335 |

Every sum derives by addition of the row's measured object sections.

The following linked values are direct file sizes; deltas subtract the same
measurement of build/sublimite. Stripped files are measurement copies made with
strip --strip-debug; release build outputs retain their debug information.
GNU size includes the unchanged bss in its total; file-size deltas avoid treating
that zero-fill memory as stored ELF payload. Detailed text/data/bss output and
raw ELF sizes are preserved in the worker report and build/hl-evidence.

| Link selection | raw ELF bytes | raw delta | stripped bytes | stripped delta | size sections delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| sublimite | 5131656 | 0 | 1584984 | 0 | 0 |
| sublimite-markdown | 5603568 | 471912 | 1941216 | 356232 | 353325 |
| sublimite-markdown_inline | 5562568 | 430912 | 1928600 | 343616 | 341340 |
| sublimite-latex | 8858912 | 3727256 | 5160328 | 3575344 | 3572352 |
| sublimite-bash | 6603712 | 1472056 | 2921416 | 1336432 | 1330780 |
| sublimite-markdown-both | 6031272 | 899616 | 2284080 | 699096 | 694629 |

LaTeX alone exceeds the supplied goal in object sections; the full linked editor
with either LaTeX or Bash also exceeds it even after removing debug information.
Both Markdown grammars together exceed it in the stripped editor as well.
Each individual Markdown target fits that stripped-file reading, but the raw
release editor already exceeds it before grammars. No whole-editor size pass is
claimed under an unspecified stripping convention.

Keep all grammar references optional, following the sibling's archive-extraction
mechanism. Explicit targets build/sublimite-markdown, build/sublimite-markdown_inline,
build/sublimite-latex and build/sublimite-bash force their corresponding wrapper
with --undefined=hl_language_<name>. Tests remain unconditional. Default
build/sublimite extracts none of these archive members and has zero measured
size delta from the saved pre-change baseline. nm verifies both omission and
explicit extraction. The both-Markdown binary is a measurement artifact linked
using the existing Makefile variables and both wrapper flags; no extra production
rule or dispatch policy was added. Future registration must preserve optional
LaTeX/Bash and a size-aware optional complete Markdown selection. Opt-in makes
the size cost explicit; it does not satisfy the goal when selected.

## Verification boundary

Release and ASan/UBSan grammar probes pass, including an authorized standalone
rerun with leak detection enabled outside tracing. The report records repository
results separately. Display suites require the coordinator's Xvfb rerun because
the sandbox denies the X socket. Unrelated suite failures are reported without
editing their code. Existing parser/worker fuzzers and busy-parser bench remain
the shared runtime's responsibility; no parser scheduling or typing-path behavior
changed here.
