# edit-v0o.6 — Markdown, LaTeX and Bash grammars (s6)

Only this bead was implemented. No bd or git commands ran; no sibling worktree
was edited. No HANDOFF.md, docs/worklog, CLAUDE.md, PLAN.md or PRD.md changes.
Read the finished sibling worker's Makefile/test changes with diff -u and its
header, wrappers and decisions. All writes stayed in this worktree.

(P) Compiler flags, versions, hashes, dates, bead IDs, source line locators and
process exit values are specification/artifact identities. Fixture lengths
follow sizeof/ftell; other numeric test identities follow C/Tree-sitter API
contracts. Measured values below are (M) [AC], checked against BAT0/status,
which reported `Not charging`. The supplied total-binary goal is (E), with no
physical derivation supplied in this bead; no new goal/budget/duration was invented.

## Files touched

- Makefile: source line and object-warning/include rules for each parser pair,
  plus explicit archive-extraction targets; every changed line is pasted below.
- src/hl/lang.h: only this bead's language declarations with shared boilerplate.
- src/hl/lang_markdown.c, lang_markdown_inline.c, lang_latex.c, lang_bash.c:
  static borrowed-language wrappers with no allocations.
- tests/hl_test.c: runtime ABI acceptance, real fixture parses, highlight-query
  compilation/capture inventory, and wrapper allocation guard.
- vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/scanner.c:
  scanner-create `(void)` prototype normalization; root PROVENANCE records it.
- vendor/tree-sitter-latex/src/scanner.c and vendor/tree-sitter-bash/src/scanner.c:
  the same ABI normalization; their PROVENANCE files record it.
- vendor/tree-sitter-latex/src/parser.c and src/tree_sitter/*.h: generated from
  the supplied archive's grammar because the archive lacked those artifacts.
- vendor/tree-sitter-latex/queries/highlights.scm: local query because the supplied
  archive lacked highlight queries; compiled and inventoried by hl_test.
- docs/decisions/edit-v0o.6.md and this report.

The supplied LaTeX archive itself also lacks its generated parser/headers and
highlight query. Downloaded the official generator into build/hl-evidence with
a reviewed network escalation, generated from the supplied grammar, and recorded
source/generator digests in PROVENANCE. Its original source archive digest remains
unchanged. A clarification was presented while independent work continued; no
response arrived, so proceeded with the stated generation/query assumption.

Vendor pruning retained only parser/scanner C, tree_sitter headers, highlights
queries, LICENSE and PROVENANCE. Both Markdown grammar subdirectories are kept.
All deleted files (including hidden metadata) follow, from the pruning manifest:

```text
vendor/tree-sitter-markdown/.editorconfig
vendor/tree-sitter-markdown/.gitattributes
vendor/tree-sitter-markdown/.gitignore
vendor/tree-sitter-markdown/.tsqueryrc.json
vendor/tree-sitter-markdown/CMakeLists.txt
vendor/tree-sitter-markdown/CONTRIBUTING.md
vendor/tree-sitter-markdown/Cargo.toml
vendor/tree-sitter-markdown/Makefile
vendor/tree-sitter-markdown/Package.resolved
vendor/tree-sitter-markdown/Package.swift
vendor/tree-sitter-markdown/README.md
vendor/tree-sitter-markdown/binding.gyp
vendor/tree-sitter-markdown/common/common.js
vendor/tree-sitter-markdown/common/common.mak
vendor/tree-sitter-markdown/common/html_entities.json
vendor/tree-sitter-markdown/package-lock.json
vendor/tree-sitter-markdown/package.json
vendor/tree-sitter-markdown/pyproject.toml
vendor/tree-sitter-markdown/scripts/build.js
vendor/tree-sitter-markdown/scripts/test.js
vendor/tree-sitter-markdown/setup.py
vendor/tree-sitter-markdown/tree-sitter-markdown/CMakeLists.txt
vendor/tree-sitter-markdown/tree-sitter-markdown/Makefile
vendor/tree-sitter-markdown/tree-sitter-markdown/grammar.js
vendor/tree-sitter-markdown/tree-sitter-markdown/package.json
vendor/tree-sitter-markdown/tree-sitter-markdown/queries/injections.scm
vendor/tree-sitter-markdown/tree-sitter-markdown/src/grammar.json
vendor/tree-sitter-markdown/tree-sitter-markdown/src/node-types.json
vendor/tree-sitter-markdown/tree-sitter-markdown-inline/CMakeLists.txt
vendor/tree-sitter-markdown/tree-sitter-markdown-inline/Makefile
vendor/tree-sitter-markdown/tree-sitter-markdown-inline/grammar.js
vendor/tree-sitter-markdown/tree-sitter-markdown-inline/package.json
vendor/tree-sitter-markdown/tree-sitter-markdown-inline/queries/injections.scm
vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/grammar.json
vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/node-types.json
vendor/tree-sitter-markdown/tree-sitter.json
vendor/tree-sitter-latex/.clang-format
vendor/tree-sitter-latex/.editorconfig
vendor/tree-sitter-latex/.gitattributes
vendor/tree-sitter-latex/.gitignore
vendor/tree-sitter-latex/.prettierrc
vendor/tree-sitter-latex/CHANGELOG.md
vendor/tree-sitter-latex/Cargo.toml
vendor/tree-sitter-latex/Makefile
vendor/tree-sitter-latex/Package.swift
vendor/tree-sitter-latex/README.md
vendor/tree-sitter-latex/benches/bench_main.rs
vendor/tree-sitter-latex/binding.gyp
vendor/tree-sitter-latex/grammar.js
vendor/tree-sitter-latex/package-lock.json
vendor/tree-sitter-latex/package.json
vendor/tree-sitter-latex/pyproject.toml
vendor/tree-sitter-latex/setup.py
vendor/tree-sitter-latex/src/grammar.json
vendor/tree-sitter-latex/src/node-types.json
vendor/tree-sitter-latex/tree-sitter.json
vendor/tree-sitter-bash/.editorconfig
vendor/tree-sitter-bash/.gitattributes
vendor/tree-sitter-bash/.gitignore
vendor/tree-sitter-bash/CMakeLists.txt
vendor/tree-sitter-bash/Cargo.lock
vendor/tree-sitter-bash/Cargo.toml
vendor/tree-sitter-bash/Makefile
vendor/tree-sitter-bash/Package.swift
vendor/tree-sitter-bash/README.md
vendor/tree-sitter-bash/binding.gyp
vendor/tree-sitter-bash/eslint.config.mjs
vendor/tree-sitter-bash/go.mod
vendor/tree-sitter-bash/go.sum
vendor/tree-sitter-bash/grammar.js
vendor/tree-sitter-bash/package-lock.json
vendor/tree-sitter-bash/package.json
vendor/tree-sitter-bash/pyproject.toml
vendor/tree-sitter-bash/script/known-failures.txt
vendor/tree-sitter-bash/script/parse-examples
vendor/tree-sitter-bash/setup.py
vendor/tree-sitter-bash/src/grammar.json
vendor/tree-sitter-bash/src/node-types.json
vendor/tree-sitter-bash/tree-sitter.json
```

## Red run before implementation

Command: `make build/rel/tests/hl_test.o`, before the header/wrappers/build rules.

```text
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER -include tests/display_guard.h -MMD -MP -c tests/hl_test.c -o build/rel/tests/hl_test.o
tests/hl_test.c:4:10: fatal error: hl/lang.h: No such file or directory
    4 | #include "hl/lang.h"
      |          ^~~~~~~~~~~
compilation terminated.
make: *** [Makefile:81: build/rel/tests/hl_test.o] Error 1
```

## Green grammar run

Command: `build/tests/hl_test`. Parsing and full query compilation pass for every
parser. ABI values are generated specification identities accepted by the runtime's
minimum/maximum macros, not guessed version numbers.

```text
hl_test: markdown ABI=15; fixture without ERROR; highlights query compiled
  @text.title
  @punctuation.special
  @text.literal
  @punctuation.delimiter
  @none
  @text.uri
  @text.reference
  @string.escape
hl_test: markdown_inline ABI=15; fixture without ERROR; highlights query compiled
  @text.literal
  @punctuation.delimiter
  @text.emphasis
  @text.strong
  @text.uri
  @text.reference
  @string.escape
hl_test: latex ABI=15; fixture without ERROR; highlights query compiled
  @comment
  @function
  @keyword
  @punctuation.delimiter
  @embedded
  @text.title
  @text.literal
  @text.reference
  @text.uri
hl_test: bash ABI=15; fixture without ERROR; highlights query compiled
  @string
  @function
  @property
  @keyword
  @comment
  @number
  @embedded
  @operator
  @constant
hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard
```

ASan/UBSan command:
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/hl_test`.
It passes with the same output above and no sanitizer diagnostics.

The initial default-leak-detection sandbox run returned:

```text
==3==LeakSanitizer has encountered a fatal error.
==3==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==3==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

An authorized rerun outside tracing, with leak detection enabled and
`UBSAN_OPTIONS=halt_on_error=1 build/san/tests/hl_test`, also passes:

```text
hl_test: markdown ABI=15; fixture without ERROR; highlights query compiled
  @text.title
  @punctuation.special
  @text.literal
  @punctuation.delimiter
  @none
  @text.uri
  @text.reference
  @string.escape
hl_test: markdown_inline ABI=15; fixture without ERROR; highlights query compiled
  @text.literal
  @punctuation.delimiter
  @text.emphasis
  @text.strong
  @text.uri
  @text.reference
  @string.escape
hl_test: latex ABI=15; fixture without ERROR; highlights query compiled
  @comment
  @function
  @keyword
  @punctuation.delimiter
  @embedded
  @text.title
  @text.literal
  @text.reference
  @text.uri
hl_test: bash ABI=15; fixture without ERROR; highlights query compiled
  @string
  @function
  @property
  @keyword
  @comment
  @number
  @embedded
  @operator
  @constant
hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard
```

Repository results are appended below after final verification.

The decision document lists every compiled capture name per parser for edit-v0o.3.
The test uses the same probe and allocation boundaries as the sibling worker.
The same complete Markdown document is parsed by both grammars. LaTeX's verbatim
fixture and Bash's heredoc fixture exercise their external scanners. No query
predicate evaluation, dispatch, scope mapping or paint integration is claimed.

## Size and opt-in extraction

Command: `size` on each gcc release grammar/scanner/wrapper object, stamped
immediately before running. Every value is (M) [AC]; dec sums text + data + bss.

```text
Not charging
   text	   data	    bss	    dec	    hex	filename
 331071	   1968	      0	 333039	  514ef	build/rel/vendor/tree-sitter-markdown/tree-sitter-markdown/src/parser.o
  12081	    520	      0	  12601	   3139	build/rel/vendor/tree-sitter-markdown/tree-sitter-markdown/src/scanner.o
 333015	   1512	      0	 334527	  51abf	build/rel/vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/parser.o
   2561	      0	      0	   2561	    a01	build/rel/vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/scanner.o
3548992	   5656	      0	3554648	 363d58	build/rel/vendor/tree-sitter-latex/src/parser.o
   1003	      0	      0	   1003	    3eb	build/rel/vendor/tree-sitter-latex/src/scanner.o
1308784	   2688	      0	1311472	 1402f0	build/rel/vendor/tree-sitter-bash/src/parser.o
  10774	      0	      0	  10774	   2a16	build/rel/vendor/tree-sitter-bash/src/scanner.o
     89	      0	      0	     89	     59	build/rel/src/hl/lang_bash.o
     89	      0	      0	     89	     59	build/rel/src/hl/lang_latex.o
     89	      0	      0	     89	     59	build/rel/src/hl/lang_markdown.o
     89	      0	      0	     89	     59	build/rel/src/hl/lang_markdown_inline.o
```

Command: `size` and file stat for the saved baseline, current default editor and
explicit extraction targets; strip --strip-debug creates measurement copies.
Each JSON row's elf and stripped fields are file stat bytes. Every value is
(M) [AC]; the stamp was checked immediately before measurement.

```text
Not charging
   text	   data	    bss	    dec	    hex	filename
1350289	 140952	17574660	19065901	122ec2d	build/hl-evidence/sublimite-baseline
1350289	 140952	17574660	19065901	122ec2d	build/sublimite
1701022	 143544	17574660	19419226	128505a	build/sublimite-markdown
1690069	 142512	17574660	19407241	1282189	build/sublimite-markdown_inline
4916953	 146640	17574660	22638253	1596ead	build/sublimite-latex
2678309	 143712	17574660	20396681	1373a89	build/sublimite-bash
2040790	 145080	17574660	19760530	12d8592	build/hl-evidence/sublimite-markdown-both
{"file": "build/hl-evidence/sublimite-baseline", "text": 1350289, "data": 140952, "bss": 17574660, "dec": 19065901, "elf": 5131656, "stripped": 1584984}
{"file": "build/sublimite", "text": 1350289, "data": 140952, "bss": 17574660, "dec": 19065901, "elf": 5131656, "stripped": 1584984}
{"file": "build/sublimite-markdown", "text": 1701022, "data": 143544, "bss": 17574660, "dec": 19419226, "elf": 5603568, "stripped": 1941216}
{"file": "build/sublimite-markdown_inline", "text": 1690069, "data": 142512, "bss": 17574660, "dec": 19407241, "elf": 5562568, "stripped": 1928600}
{"file": "build/sublimite-latex", "text": 4916953, "data": 146640, "bss": 17574660, "dec": 22638253, "elf": 8858912, "stripped": 5160328}
{"file": "build/sublimite-bash", "text": 2678309, "data": 143712, "bss": 17574660, "dec": 20396681, "elf": 6603712, "stripped": 2921416}
{"file": "build/hl-evidence/sublimite-markdown-both", "text": 2040790, "data": 145080, "bss": 17574660, "dec": 19760530, "elf": 6031272, "stripped": 2284080}
```

Per-grammar sums and linked deltas (each subtracting build/sublimite) are in the
decision document. Default editor delta is zero versus the pre-change baseline.
LaTeX object sections exceed the supplied goal alone. LaTeX, Bash and complete
Markdown exceed it in the stripped linked editor. Raw debug-enabled build/sublimite
already exceeds that planning line without grammars; no full size pass is claimed.
The explicit targets match the sibling's --undefined wrapper/archive mechanism.
Future registration must retain optional references so default linking does not
silently incur these costs. All grammars are always built and tested.

Opt-in red probe, before adding the target rules:
`make -n build/sublimite-markdown build/sublimite-markdown_inline build/sublimite-latex build/sublimite-bash`.

```text
make: *** No rule to make target 'build/sublimite-markdown'.  Stop.
```

Opt-in green build commands (each completed successfully):

```text
gcc -O2 -g -msse2 -pthread build/rel/src/main.o -Wl,--undefined=hl_language_markdown build/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/sublimite-markdown
gcc -O2 -g -msse2 -pthread build/rel/src/main.o -Wl,--undefined=hl_language_markdown_inline build/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/sublimite-markdown_inline
gcc -O2 -g -msse2 -pthread build/rel/src/main.o -Wl,--undefined=hl_language_latex build/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/sublimite-latex
gcc -O2 -g -msse2 -pthread build/rel/src/main.o -Wl,--undefined=hl_language_bash build/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/sublimite-bash
PASS: default editor unchanged; each explicit target extracts its wrapper/parser/scanner
```

The symbol check used nm -g on the default and each target. Complete Markdown
was also measured in build/hl-evidence/sublimite-markdown-both, linked with both
wrapper flags using the existing Makefile variables in a temporary stdin rule.
No extra shared dispatch/build rule was introduced for that measurement.

## Makefile changes, every line

```diff
--- build/hl-evidence/Makefile.before	2026-10-13 00:18:04.219093633 +1000
+++ Makefile	2026-10-13 00:22:11.871248078 +1000
@@ -33,6 +33,10 @@
 B := build
 TS_SRC := vendor/tree-sitter/lib/src/lib.c
 SRCS   := $(sort $(wildcard src/*/*.c)) $(TS_SRC)
+SRCS += vendor/tree-sitter-markdown/tree-sitter-markdown/src/parser.c vendor/tree-sitter-markdown/tree-sitter-markdown/src/scanner.c
+SRCS += vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/parser.c vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/scanner.c
+SRCS += vendor/tree-sitter-latex/src/parser.c vendor/tree-sitter-latex/src/scanner.c
+SRCS += vendor/tree-sitter-bash/src/parser.c vendor/tree-sitter-bash/src/scanner.c
 MAINC  := $(wildcard src/main.c)
 TESTS  := $(sort $(wildcard tests/*_test.c))
 BENCHS := $(sort $(wildcard bench/*_bench.c))
@@ -49,6 +53,20 @@
 TS_OBJ := $(addprefix $(B)/,$(addsuffix /$(TS_SRC:.c=.o),rel san fuzz))
 $(TS_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN))
 
+# Generated grammar/scanner warnings are relaxed only for vendored objects.
+TS_MARKDOWN_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-markdown/tree-sitter-markdown/src/,parser.o scanner.o))
+$(TS_MARKDOWN_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN))
+$(TS_MARKDOWN_OBJ): INC := -Ivendor/tree-sitter-markdown/tree-sitter-markdown/src $(INC)
+TS_MARKDOWN_INLINE_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/,parser.o scanner.o))
+$(TS_MARKDOWN_INLINE_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN))
+$(TS_MARKDOWN_INLINE_OBJ): INC := -Ivendor/tree-sitter-markdown/tree-sitter-markdown-inline/src $(INC)
+TS_LATEX_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-latex/src/,parser.o scanner.o))
+$(TS_LATEX_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-parameter -Wno-unused-label -Wno-unused-but-set-variable
+$(TS_LATEX_OBJ): INC := -Ivendor/tree-sitter-latex/src $(INC)
+TS_BASH_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-bash/src/,parser.o scanner.o))
+$(TS_BASH_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-value
+$(TS_BASH_OBJ): INC := -Ivendor/tree-sitter-bash/src $(INC)
+
 REL_LIB  := $(B)/libedit.a
 SAN_LIB  := $(B)/san/libedit.a
 FUZZ_LIB := $(B)/fuzz/libedit.a
@@ -107,6 +125,15 @@
 	rm -f $@; $(AR) rcs $@ $^
 
 # ---- binaries ----
+# Explicit grammar extraction follows the sibling worker; large grammars stay opt-in.
+$(B)/sublimite-markdown: $(B)/rel/src/main.o $(REL_LIB)
+	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_markdown $(REL_LIB) $(LDLIBS) -o $@
+$(B)/sublimite-markdown_inline: $(B)/rel/src/main.o $(REL_LIB)
+	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_markdown_inline $(REL_LIB) $(LDLIBS) -o $@
+$(B)/sublimite-latex: $(B)/rel/src/main.o $(REL_LIB)
+	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_latex $(REL_LIB) $(LDLIBS) -o $@
+$(B)/sublimite-bash: $(B)/rel/src/main.o $(REL_LIB)
+	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_bash $(REL_LIB) $(LDLIBS) -o $@
 $(B)/sublimite: $(B)/rel/src/main.o $(REL_LIB)
 	$(CC_RELEASE) $(REL_F) -pthread $< $(REL_LIB) $(LDLIBS) -o $@
 $(B)/tools/%: $(B)/rel/tools/%.o $(REL_LIB)
```

The original runtime source/object rule, existing link recipes and check driver
are unchanged. Both Markdown objects relax conversion/shadow warnings only;
Bash additionally needs unused-value relaxation for array_pop; LaTeX additionally
needs unused ABI parameters and unused lexer scaffolding relaxed. Strict compiler
evidence is saved in build/hl-evidence/*-strict.log. The LaTeX parser diagnostic:

```text
In file included from vendor/tree-sitter-latex/src/parser.c:3:
vendor/tree-sitter-latex/src/parser.c: In function ‘ts_lex_keywords’:
vendor/tree-sitter-latex/src/tree_sitter/parser.h:189:3: error: label ‘next_state’ defined but not used [-Werror=unused-label]
  189 |   next_state:                   \
      |   ^~~~~~~~~~
vendor/tree-sitter-latex/src/parser.c:24396:3: note: in expansion of macro ‘START_LEXER’
24396 |   START_LEXER();
      |   ^~~~~~~~~~~
vendor/tree-sitter-latex/src/tree_sitter/parser.h:187:11: error: variable ‘lookahead’ set but not used [-Werror=unused-but-set-variable]
  187 |   int32_t lookahead;            \
      |           ^~~~~~~~~
vendor/tree-sitter-latex/src/parser.c:24396:3: note: in expansion of macro ‘START_LEXER’
24396 |   START_LEXER();
      |   ^~~~~~~~~~~
cc1: all warnings being treated as errors
```

No sanitizer checks were suppressed. Scanner-create prototype normalization
follows the sibling's correction for old-style definitions and is documented
in PROVENANCE.
