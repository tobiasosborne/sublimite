# edit-v0o.3 — C/Rust grammars and worker queries

Versions and bead/task identifiers are provenance, not measured quantities.
The coordinator vendored C and Rust at `v0.24.2`; their release tarball hashes
and source URLs remain in each `PROVENANCE`. The runtime is `v0.27.1`, as recorded
in the preceding runtime decision. Upstream parser, headers, highlight queries and licenses stay unmodified.
The Rust scanner has a local C11 prototype clarification: `create()` becomes
`create(void)`, matching the runtime scanner function-pointer type. Clang
UBSan reports an incorrect function type with the upstream unspecified-argument
definition. No scanner logic changes and no sanitizer is disabled for this
compatibility fix; the sanitizer red/green runs are recorded in the report. Build-time-unused `grammar.json`,
`node-types.json`, `tags.scm` and `injections.scm` were removed where present.

## Build and storage

Each language has its own `hl_language_*` wrapper and declaration. The grammar
sources join `TS_SRC` in the same release, sanitizer and fuzz archives. The
object list now expands every vendored source across those configurations.
Only generated grammar objects and their scanner use
`-std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-label -D_GNU_SOURCE -pthread`,
plus the configuration's optimization/debug/sanitizer and include flags.
Generated labels/parameters are unused, while the scanner converts signed
lexer lookahead to the unsigned wide-character ABI. These vendored objects
omit `-Werror -Wshadow -Wconversion`. The runtime retains its previous warning
policy. Project `src/`, tests and fuzz entry points retain all required strict
warnings. No language-specific include path is needed: upstream sources find
their quoted headers relative to their own directory.

`tools/hl_embed.py` preserves highlight query bytes as adjacent C string
literals in `query_c.inc` and `query_rust.inc`. This avoids runtime file lookup
and packaging-path dependencies. These checked-in generated includes must be
regenerated when the vendored highlights change. C compiler dependencies track
them through the existing dependency rules.

`hl_query` is separate worker-owned state alongside `hl_worker`. Initialize it
on a worker before query work and serialize its use with completed tree access.
`hl_query_init` compiles the embedded query with `ts_query_new`; the source
variant allows future language workers to pass their own embedded query.
Query/cursor memory remains upstream worker-side allocation. Scope and
predicate metadata use an arena from `src/base`, sized from the query's capture
and pattern counts with alignment padding (P). No UI setup, mailbox encoding,
decoding or scope-key lookup allocates. There is no paint/editor integration.

## Capture mapping

Every distinct capture name in the supplied highlights is listed below.
Capture names appearing as predicate references use the same mapping.
Unknown names emit `HL_SCOPE_DEFAULT`, whose key is `editor.foreground`.

| Capture | C | Rust | Theme key |
| --- | --- | --- | --- |
| comment | yes | yes | syntax.comment |
| comment.documentation | | yes | syntax.comment |
| keyword | yes | yes | syntax.keyword |
| string | yes | yes | syntax.string |
| escape | | yes | syntax.string |
| number | yes | | syntax.number |
| constant | yes | yes | syntax.constant |
| constant.builtin | | yes | syntax.constant |
| function | yes | yes | syntax.function |
| function.special | yes | | syntax.function |
| function.method | | yes | syntax.function |
| function.macro | | yes | syntax.function |
| type | yes | yes | syntax.type |
| type.builtin | | yes | syntax.type |
| constructor | | yes | syntax.type |
| variable | yes | | syntax.variable |
| variable.parameter | | yes | syntax.variable |
| variable.builtin | | yes | syntax.variable |
| property | yes | yes | syntax.variable |
| label | yes | yes | syntax.variable |
| operator | yes | yes | syntax.operator |
| delimiter | yes | | syntax.punctuation |
| punctuation.bracket | | yes | syntax.punctuation |
| punctuation.delimiter | | yes | syntax.punctuation |
| attribute | | yes | syntax.attribute |

The required common scope set intentionally merges upstream distinctions.
Rust highlights assign integer/float/bool to `constant.builtin`; they therefore
all become `syntax.constant`, rather than remapping the same capture by node
kind. Function definitions/calls/macros also share a key. Fonts/styles and
Sublime's full selector system are outside this bead.

## Predicates, overlap and rows

Tree-sitter exposes predicates but does not evaluate text predicates for the
caller. The layer supports the exact ASCII `match?` expressions present here:
C's `^[A-Z][A-Z\\d_]*$` accepts uppercase identifiers with following uppercase
letters, ASCII digits or underscores; Rust's `^[A-Z]` accepts an uppercase
initial byte. Rust's supplied `^[A-Z][A-Z\\d_]+$'` requires an apostrophe after
an end assertion and therefore cannot match. This upstream quirk is preserved
and reported; no vendored query fix is made. Unsupported predicates/regexes
return `HL_QUERY_PREDICATE` during initialization, rather than silently dropping
their filtering. Metadata reflects the upstream predicate sequence (P):
operator + capture + expression + Done. Predicate text reads return errors;
there is no arbitrary identifier length cap. Chunk scratch uses the existing
Linux page-sized input convention documented by the runtime decision (P).

Rows are half-open caller-supplied ordinals. Snapshot line lookup derives the
byte window. Captures are clipped to that window, including multiline tokens.
Empty/out-of-document windows succeed with no captures. Matches are emitted in
upstream order with pattern-index priority; a later pattern wins an overlapping
byte and equal priority uses the last emitted capture. This follows the
vendored order, where more specific captures generally follow broad ones.
For example a function call overrides an earlier uppercase constructor capture.

Each `hl_span` has only byte endpoints, a scope id and pattern priority. Its
sibling mailbox kind is the ASCII letters H/S packed by `CHAR_BIT` (P).
Serialization copies fixed-width fields individually, excluding C padding,
into `work_msg.data`; the compile-time fit check derives from the mailbox ABI
(P). Generation remains in the existing transport field. After a successful
run the caller can close the staged span batch with the existing `hl_result`
for that covered byte range/phase and generation. Install only a complete,
successful, current-generation batch; empty batches clear old syntax. Tree and
query pointers never cross the mailbox. This bead supplies the contract and
encoding, not a production parse/query publisher or UI consumer. On callback
backpressure/error, discard the partial batch and rerun. Cursor match-limit
exhaustion returns `HL_QUERY_LIMIT` so partial output cannot masquerade as
complete highlighting. The cursor's upstream limit is not a new application
budget.

## Golden-master syntax colours

(M)[AC]: `BAT0/status` read `Not charging` while inspecting the installed
`/opt/sublime_text/Packages/Color Scheme - Default.sublime-package` zip.
Source entries: `Mariana.sublime-color-scheme` (dark) and
`Breakers.sublime-color-scheme` (light), read with `unzip -p`. No inferred
accessibility/contrast claim is made. Existing UI/chrome placeholder colours
were not changed in this syntax bead.

(P) HSL conversion divides hue by the full circle, saturation/lightness by
the percentage scale, converts HLS to sRGB and rounds each channel multiplied
by the unsigned octet maximum to nearest integer. In numeric units these are
hue/360 degrees, percentages/100 and channel*255, derived respectively from
the circle, percent definition and `UINT8_MAX`. The following hexadecimal RGB
values are (M)[AC] derived from the named installed HSL variables by that rule.

| Key | Source rule / interpretation | Mariana variable → RGB | Breakers variable → RGB |
| --- | --- | --- | --- |
| syntax.comment | comment | blue6 → a6acb9 | grey2 → 999999 |
| syntax.keyword | keyword excluding operator | pink → c695c6 | pink → c695c6 |
| syntax.string | string | green → 99c794 | green → 80b979 |
| syntax.number | constant.numeric | orange → f9ae58 | orange → f9ae58 |
| syntax.constant | constant.character/constant.other | pink → c695c6 | pink → c695c6 |
| syntax.function | entity.name.function | blue5 → 5fb4b4 | blue2 → 5fb4b4 |
| syntax.type | storage.type | pink → c695c6 | pink → c695c6 |
| syntax.variable | global default foreground | white3 → d8dee9 | grey3 → 333333 |
| syntax.operator | keyword.operator | red2 → f97b58 | red2 → f97b58 |
| syntax.punctuation | punctuation.separator/terminator | blue6 → a6acb9 | red3 → ac7a68 |
| syntax.attribute | entity.other.attribute-name | pink → c695c6 | pink → c695c6 |

To make colour derivation reproducible, the exact source HSL values are:

| Variable | Mariana HSL | Breakers HSL |
| --- | --- | --- |
| blue6 | 221°, 12%, 69% | unused |
| grey2 | unused | 0°, 0%, 60% |
| pink | 300°, 30%, 68% | same |
| green | 114°, 31%, 68% | 114°, 31%, 60% |
| orange | 32°, 93%, 66% | same |
| blue5 / blue2 | 180°, 36%, 54% | same |
| white3 / grey3 | 219°, 28%, 88% | 0°, 0%, 20% |
| red2 | 13°, 93%, 66% | same |
| red3 | unused | 16°, 29%, 54% |

These simplified theme keys choose a representative Sublime selector. They do
not reproduce its complete context-sensitive scheme: merged constants use the
user-defined constant colour, merged functions use the definition colour,
merged punctuation uses the separator colour, and merged variables use the
scheme foreground. That tradeoff follows the requested capture-to-one-key
contract and is explicit rather than claiming full selector equivalence.

## Size outcome and follow-up

(M)[AC], BAT0 `Not charging`: GNU `size` on the release objects that are
members of `build/libedit.a` gives these byte totals (`text + data + bss`;
all new objects have empty bss):

| Archive member source | text | data | total |
| --- | ---: | ---: | ---: |
| C parser | 602104 | 3520 | 605624 |
| Rust parser | 1109089 | 3384 | 1112473 |
| Rust scanner | 1911 | 0 | 1911 |
| C language wrapper | 89 | 0 | 89 |
| Rust language wrapper | 89 | 0 | 89 |
| Query layer, including embedded queries | 8758 | 512 | 9270 |
| Runtime, from preceding bead | 181883 | 32 | 181915 |

(M)[AC] Grammar contribution is the sum of the parser/scanner rows:
`605624 + 1112473 + 1911 = 1720008 bytes`. With wrappers/query it is
`1720008 + 89 + 89 + 9270 = 1729456 bytes`. Reconstructing the entry-time
syntax arrays in a temporary source and compiling with the same release
flags gives theme objects of `1364` before and `1656` after, a difference of
`1656 - 1364 = 292 bytes`. Thus this bead's archive-code/data contribution
is `1729456 + 292 = 1729748 bytes`, excluding debug sections. Source fixtures,
fuzzers, tests and documentation are not production archive members.

The supplied P6.3 acceptance target is **binary <= 2 MB total** (E: requested
planning target, no physics derivation supplied), shared by the runtime and
all assigned grammars. An equal share for the assigned language pair among
C/Rust/Python/Julia is (E) `2 / 4 * 2000000 = 1000000 bytes`, even before
reserving runtime/editor space. The grammars exceed that share. The result
also exceeds the corresponding binary-megabyte interpretation of that share.
Do not accept this measurement as satisfying the total target.

(M)[AC] The current editor has `1350673 text + 141144 data = 1491817 bytes`
and `17574660 bss` from GNU `size`. `nm` finds no query/runtime/production
grammar entry points in that executable: static-link dead-member elimination
keeps the new code out until the successor wires it. The release file with
debug information is `5131656 bytes`; a separate `strip --strip-debug` copy
is `1584984 bytes` (M)[AC], `wc -c`; the shipped artifact was not stripped.
The absent grammar code must not be mistaken for meeting the target after
highlighting integration. (E) Simply adding current text/data to the measured
syntax subsystem excluding the theme delta already present in that binary
gives `1491817 + 1729456 + 181915 = 3403188 bytes` before the other grammars
or paint wiring. This is a projection from measured object sums, not a final
linked-file measurement.

Recommended coordinator decision: make grammars selectable/opt-in at build
or package time, or revise the total target using complete linked-build
measurements. If retaining all grammars within that target is required,
investigate regeneration with reduced parse tables and verify parser/query
conformance. GNU `nm --size-sort` shows parse tables dominate these objects;
removing debug information does not remove those runtime-required tables.
No tables were discarded, no grammar silently excluded from the archives,
and no target was redefined in this bead.

## Landing (edit-v0o.3/.4/.5/.6 merged series, 2026-10-13)

The four grammar beads were cherry-picked in order v0o.3, .4, .5, .6 onto
`wt/edit-hl-merge-s6`. Conflicts and their resolution:

- `src/hl/lang.h` (add/add in every pick): one guard/include, declarations
  grouped in bead order (c, rust, python, julia, javascript, typescript, json,
  toml, yaml, markdown, markdown_inline, latex, bash).
- `tests/hl_test.c`: v0o.4/.5/.6 carried byte-identical `grammar_probe`; kept
  once. All per-language probes kept in bead order after `query_test`. `main`
  now takes every wrapper inside one counting-allocator window (all thirteen
  wrappers must allocate zero) and then runs every probe.
- `Makefile`, build scheme unified: v0o.3 appended C/Rust to `TS_SRC` and gave
  every `parser.o`/`scanner.o` a hand-written flag set without `-Werror`;
  v0o.4/.5/.6 appended to `SRCS` with a per-language `TS_<LANG>_OBJ` block
  (runtime relaxation `-Wconversion -Wshadow` filtered out, `-Werror` kept, own
  `src/` include first, plus only the `-Wno-*` that language trips). C and Rust
  now use the second scheme; `TS_SRC`/`TS_OBJ` are back to main's single-runtime
  form. With that scheme C and Rust compile clean under gcc and clang with no
  extra `-Wno-*` (checked with `-Werror` before dropping v0o.3's
  `-Wno-unused-parameter -Wno-unused-label`).
- Opt-in, unified: v0o.4 (Julia) and v0o.5 (TypeScript) chose the same
  mechanism (archive extraction forced by `-Wl,--undefined=hl_language_<lang>`
  on an extra editor link); v0o.6 copied it for its four grammars. The six
  explicit rules became one static pattern rule over
  `HL_OPTIN := julia typescript markdown markdown_inline latex bash`
  (`make build/sublimite-<lang>`). Nothing in the editor references a wrapper
  yet, so the default `build/sublimite` links **no** grammar (nm: zero
  `hl_language_*`/`tree_sitter_*` symbols); `HL_OPTIN` names the grammars a
  future dispatch must keep optional. The would-be default set is c, rust,
  python, javascript, json, toml, yaml.

### Sizes at landing (M)[AC] BAT0 `Not charging`, gcc 13 `-O2 -g`

GNU `size` text+data+bss of the release objects (parser + scanner):
C 605624; Rust 1112473 + 1911; Python 461061 + 3672; Julia 2623800 + 1443;
JavaScript 410314 + 2452; TypeScript 1410098 + 2353; JSON 5743; TOML 25269 +
649; YAML 168312 + 21188; Markdown 333039 + 12601; Markdown inline 334527 +
2561; LaTeX 3554648 + 1003; Bash 1311472 + 10774. Runtime `lib.o` 181915; query
layer 9270; each wrapper 89. Default-set objects sum to 2818668; `HL_OPTIN`
objects to 9598319; all thirteen to 12416987 (sums of the listed rows).

Linked editors (`wc -c` raw / after `strip --strip-debug` / `size` text+data):

| Link | raw | stripped | text+data |
| --- | ---: | ---: | ---: |
| main 88d0ef3 `build/sublimite` | 5131640 | 1584984 | 1491241 |
| landed `build/sublimite` | 5131664 | 1584984 | 1491817 |
| default set forced (c rust python javascript json toml yaml) | 8412576 | 4462624 | 4355282 |
| all thirteen forced | 18702000 | 14131704 | 14004104 |
| `sublimite-julia` | 7839320 | 4224240 | 4123793 |
| `sublimite-typescript` | 6637512 | 3008752 | 2914790 |
| `sublimite-markdown` | 5603576 | 1941216 | 1845142 |
| `sublimite-markdown_inline` | 5562576 | 1928600 | 1833157 |
| `sublimite-latex` | 8858912 | 5160328 | 5064169 |
| `sublimite-bash` | 6603712 | 2921416 | 2822597 |

The landed default grows by 1491817 - 1491241 = 576 text+data bytes (the
syntax.* theme tables). Against P6.3 "binary <= 2 MB total (grammars +
runtime)", itself (E): the default set alone adds 4355282 - 1491817 = 2863465
text+data bytes, so the accept line is **missed** as soon as dispatch links
the default set, under either 2000000 or 2097152 bytes. Parse tables dominate
(`size -A`: `.rodata`); `-Os` cannot remove them. Decision left to the
coordinator/Tobias: revise P6.3 against these measurements, or shrink the
default set.
