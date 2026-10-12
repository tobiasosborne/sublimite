# edit-v0o.5: JavaScript, TypeScript, JSON, TOML and YAML grammars

## Integration and pruning

Follow the completed Python/Julia worker's wrapper, archive and test pattern.
Each src/hl/lang_<name>.c returns upstream static language storage without
allocation. lang.h contains only this bead's declarations plus the shared
include/guard. Grammars compile into release, sanitizer and fuzzer archives.
Project code retains the complete strict warning set. Grammar objects relax
conversion and shadow warnings like the runtime. JavaScript, TypeScript and
TOML additionally relax unused scanner parameters. YAML additionally relaxes
unused values, unused-but-set variables and implicit fallthrough. Compiler
diagnostics in build/hl-evidence/*-warnings.log justify those exceptions.
Optimization, C11, Wall/Wextra/Werror, ASan and UBSan remain enabled. Each
grammar uses its own parser ABI headers. Compiler/runtime/tag versions and
digest strings are provenance identities, not measured physical quantities.
Generated upstream numeric literals retain their derivation through the
vendored release identity and PROVENANCE digest; no parse tables were edited.

Prune generator sources, grammar/node metadata, package manifests/lockfiles,
build-system files, schema generators, ancillary configuration and all queries
except queries/highlights.scm. Retain LICENSE and PROVENANCE, parser.c,
scanner.c where present and tree_sitter/*.h. TypeScript retains its required
common/scanner.h and typescript/src layout. JSON has no scanner. The report
includes the exact deleted-file inventory.

Normalize external_scanner_create(void) in JavaScript, TypeScript, TOML and
YAML to match the parser/runtime callback ABI, following the sibling worker's
UBSan function-type correction. Append these local changes to PROVENANCE.
YAML scanner.c includes its generated default schema.core.c; inline that
source verbatim in place of the schema-selection preamble, then prune the
separate schema sources and alternate-schema trees. This preserves upstream
core-schema behavior while meeting the requested parser/scanner/header-only
layout. No runtime allocation or syntax-table behavior is changed.

## Test contract

Write the failing grammar probes before wrappers/build rules. Each checks the
runtime ABI range macros, ts_parser_set_language acceptance, a fixture root's
recursive has_error flag (ERROR and missing descendants), and ts_query_new on
the complete retained highlights query. The counting allocator probes every
wrapper together. Source lengths derive from sizeof minus the C terminator;
query lengths derive from ftell. (P) Other new numeric constants represent
C/API identities: success/failure, empty initialization, byte/element identity
and array iteration. Parser/query malloc occurs in the standalone worker-side
probe, outside typing. Production wrappers allocate nothing. Fixtures cover
JavaScript template substitution/regex/automatic semicolon insertion;
TypeScript generics/types/template substitution; JSON escaping/booleans/null;
TOML tables/arrays/multiline strings; YAML indentation/anchors/aliases/block
scalars/flow collections. No registration, predicate evaluation, dispatch,
theme or paint work is introduced.

## Capture inventory for the query/scope worker

Names below derive from each successfully compiled query's
ts_query_capture_name_for_id output. TypeScript highlights.scm contains
TypeScript-specific scopes; composing inherited JavaScript highlights belongs
to the shared query/scope layer. JavaScript JSX/parameter auxiliary queries
are pruned per the brief.

### JavaScript

`@comment`, `@constant`, `@constant.builtin`, `@constructor`, `@embedded`, `@function`, `@function.builtin`, `@function.method`, `@keyword`, `@number`, `@operator`, `@property`, `@punctuation.bracket`, `@punctuation.delimiter`, `@punctuation.special`, `@string`, `@string.special`, `@variable`, `@variable.builtin`.

### TypeScript

`@keyword`, `@punctuation.bracket`, `@type`, `@type.builtin`, `@variable.parameter`.

### JSON

`@comment`, `@constant.builtin`, `@escape`, `@number`, `@string`, `@string.special.key`.

### TOML

`@boolean`, `@comment`, `@number`, `@operator`, `@property`, `@punctuation.bracket`, `@punctuation.delimiter`, `@string`, `@string.special`, `@type`.

### YAML

`@attribute`, `@boolean`, `@comment`, `@constant.builtin`, `@label`, `@number`, `@property`, `@punctuation.bracket`, `@punctuation.delimiter`, `@punctuation.special`, `@string`, `@type`.

## Compiled and linked contribution

Every table entry and arithmetic below is (M) [AC], directly from GNU size
section bytes or stat ELF file bytes. BAT0/status was Not charging before
measurement. Raw output: build/hl-evidence/size.log. The object sum is text +
data + bss; linked deltas subtract the same editor without grammar references.
ELF file size includes debug information, headers and padding; size's bss is
storage rather than file content. These accountings are not interchangeable.

| Component | text | data | bss | sum |
| --- | ---: | ---: | ---: | ---: |
| javascript parser | 407586 | 2728 | 0 | 410314 |
| javascript scanner | 2452 | 0 | 0 | 2452 |
| typescript parser | 1406114 | 3984 | 0 | 1410098 |
| typescript scanner | 2353 | 0 | 0 | 2353 |
| json parser | 5255 | 488 | 0 | 5743 |
| toml parser | 24509 | 760 | 0 | 25269 |
| toml scanner | 649 | 0 | 0 | 649 |
| yaml parser | 165584 | 2728 | 0 | 168312 |
| yaml scanner | 21188 | 0 | 0 | 21188 |
| tsx parser | 1438754 | 4120 | 0 | 1442874 |
| tsx scanner | 2353 | 0 | 0 | 2353 |

| Grammar | object sum derivation | linked section delta derivation | ELF file delta derivation |
| --- | --- | --- | --- |
| javascript | 410314 + 2452 = 412766 | 19486562 − 19065901 = 420661 | 5635224 − 5131656 = 503568 |
| typescript | 1410098 + 2353 = 1412451 | 20488874 − 19065901 = 1422973 | 6637512 − 5131656 = 1505856 |
| json | 5743 = 5743 | 19072653 − 19065901 = 6752 | 5146240 − 5131656 = 14584 |
| toml | 25269 + 649 = 25918 | 19093869 − 19065901 = 27968 | 5184264 − 5131656 = 52608 |
| yaml | 168312 + 21188 = 189500 | 19263405 − 19065901 = 197504 | 5434952 − 5131656 = 303296 |
| tsx | 1442874 + 2353 = 1445227 | 20522086 − 19065901 = 1456185 | 6681992 − 5131656 = 1550336 |
| combined | 410314 + 2452 + 1410098 + 2353 + 5743 + 25269 + 649 + 168312 + 21188 = 2046378 | 21138758 − 19065901 = 2072857 | 7507232 − 5131656 = 2375576 |

Default build/sublimite contains no wrapper/grammar symbols; archive extraction
is demand-driven, as in the sibling worker. Its section and ELF file delta
from the pre-bead editor is zero (M) [AC], subtracting equal rows in size.log.
This does not mean registered grammars cost nothing. Forced-reference editor
links measure marginal cost including wrappers, tables, scanner and linker
effects. TypeScript's explicit make build/sublimite-typescript target uses
-Wl,--undefined=hl_language_typescript, exactly the Julia opt-in mechanism.
Tests always exercise TypeScript regardless of this opt-in target.

The supplied “binary <= 2 MB total” acceptance is (E), a planning goal in the
brief without a physical derivation or unit convention. The pre-bead default
ELF already exceeds it when debug information is counted; its bss also exceeds
it when total loaded storage is counted. Even the favorable text + data
accounting exceeds it with TypeScript or TSX selected:

- typescript: 2769238 + 144976 = 2914214 text/data bytes (M) [AC].
- tsx: 2802290 + 145136 = 2947426 text/data bytes (M) [AC].
- combined: 3412274 + 151824 = 3564098 text/data bytes (M) [AC].

TSX's incremental contribution is comparable to the entire TypeScript grammar
and duplicates most parse tables; it is not small against the supplied total
goal. Exercise the explicitly permitted omission: prune TSX after measuring
its parser/scanner objects and linked editor delta. Measurement objects live
only under build/hl-evidence, never in the product archive. No TSX wrapper,
declaration or test is claimed. Future registration must preserve optional
TypeScript selection; combined grammar size must be reassessed after the
other workers merge. This bead does not claim the shared size goal passes.

## Verification boundary

Release and clang ASan/UBSan probes pass; every retained highlights query
compiles and every fixture parses without ERROR. Leak detection is disabled
only for the sanitizer run because LeakSanitizer rejects sandbox ptrace; the
release allocation guard remains active. Full compiler/test outcomes, red/green output
and unrelated suite failures are in docs/worker-reports/edit-v0o.5-s6.md.
Non-display file_kill_test, ipc_test and journal_kill_test failed outside this
bead; every other non-display suite passed. Display suites require the
coordinator's Xvfb rerun because this sandbox has
no accessible X socket. No git/bd or sibling-worktree writes were performed.
