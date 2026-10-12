# edit-v0o.5 worker report (s6)

Implemented the assigned grammar bead only. Added the JavaScript, TypeScript,
JSON, TOML and YAML static wrappers, ABI/parse/query tests, vendored archive
build rules and TypeScript opt-in link target. TSX is omitted after measured
size; the decision and complete capture inventories are in
[edit-v0o.5.md](../decisions/edit-v0o.5.md).

Release build and grammar probes pass; full repository acceptance remains
incomplete due to the unrelated/environment failures detailed below.

No bd or git ran. The sibling pattern was read and compared using ordinary
diff, respecting the explicit prohibition on git. All source/document edits stayed in this
worktree. HANDOFF.md, docs/worklog, CLAUDE.md, PLAN.md and PRD.md were not edited.
No downstream registration/query/paint work was added.

Numeric literals in the following pasted tool outputs are (M) [AC] observations
or compiler/ABI/path identities. Exit codes have the (P) process-contract
derivation: success is zero, nonzero is failure. Compiler version identities
were checked with gcc --version and clang --version. BAT0/status was
Not charging before the baseline/size/grammar runs. New test constants carry
(P) C/API identity derivations, and fixture/query lengths derive from
sizeof/ftell. Generated upstream literals retain vendored release provenance.

## Files touched

- Makefile: grammar source entries, per-grammar warning/include rules and
  explicit TypeScript extraction target; every changed line is pasted below.
- src/hl/lang.h: this bead's language declarations only, plus include/guard.
- src/hl/lang_javascript.c, lang_typescript.c, lang_json.c, lang_toml.c,
  lang_yaml.c: static-language wrappers.
- tests/hl_test.c: same shared grammar probe as the Python/Julia worker,
  language fixtures and release wrapper allocation guard.
- docs/decisions/edit-v0o.5.md and this report.
- vendor/tree-sitter-{javascript,typescript,json,toml,yaml}/PROVENANCE:
  pruning notes and documented local scanner changes.
- JavaScript, TypeScript, TOML and YAML scanner.c: create(void) callback
  normalization. YAML also inlines the unchanged upstream core schema source
  to allow all separate schema files to be pruned.
- Vendored files deleted: exact inventory at the end. Retained parsers, ABI
  headers, highlights queries, common/scanner.h and LICENSE remain unchanged.
- build/hl-evidence/: local logs, pre-bead snapshots and measurement binaries;
  these are build evidence, not product sources.

## Red run, before implementation

Command: make build/tests/hl_test. It fails because the language interface has
not yet been implemented. Observed make exit: nonzero.

```text
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER -include tests/display_guard.h -MMD -MP -c tests/hl_test.c -o build/rel/tests/hl_test.o
tests/hl_test.c:4:10: fatal error: hl/lang.h: No such file or directory
    4 | #include "hl/lang.h"
      |          ^~~~~~~~~~~
compilation terminated.
make: *** [Makefile:81: build/rel/tests/hl_test.o] Error 1
```

## Green grammar runs

Commands: make build/tests/hl_test build/san/tests/hl_test, then
build/tests/hl_test and ASAN_OPTIONS=detect_leaks=0 build/san/tests/hl_test.
Compilation and both probes exit successfully. Release allocation interposition
is active; sanitizer builds deliberately leave that interposition inert to
allow ASan to own malloc, as established in src/base/malloc_guard.c.

Release output:

```text
hl_test: javascript ABI=15; fixture without ERROR; highlights query compiled
  @variable
  @property
  @function
  @function.method
  @constructor
  @constant
  @variable.builtin
  @function.builtin
  @constant.builtin
  @comment
  @string
  @string.special
  @number
  @punctuation.delimiter
  @operator
  @punctuation.bracket
  @punctuation.special
  @embedded
  @keyword
hl_test: typescript ABI=14; fixture without ERROR; highlights query compiled
  @type
  @type.builtin
  @punctuation.bracket
  @variable.parameter
  @keyword
hl_test: json ABI=14; fixture without ERROR; highlights query compiled
  @string.special.key
  @string
  @number
  @constant.builtin
  @escape
  @comment
hl_test: toml ABI=14; fixture without ERROR; highlights query compiled
  @type
  @string
  @property
  @boolean
  @comment
  @number
  @string.special
  @punctuation.delimiter
  @operator
  @punctuation.bracket
hl_test: yaml ABI=14; fixture without ERROR; highlights query compiled
  @boolean
  @constant.builtin
  @string
  @number
  @comment
  @label
  @type
  @attribute
  @property
  @punctuation.delimiter
  @punctuation.bracket
  @punctuation.special
hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard
```

Clang ASan/UBSan output, retaining both sanitizers:

```text
hl_test: javascript ABI=15; fixture without ERROR; highlights query compiled
  @variable
  @property
  @function
  @function.method
  @constructor
  @constant
  @variable.builtin
  @function.builtin
  @constant.builtin
  @comment
  @string
  @string.special
  @number
  @punctuation.delimiter
  @operator
  @punctuation.bracket
  @punctuation.special
  @embedded
  @keyword
hl_test: typescript ABI=14; fixture without ERROR; highlights query compiled
  @type
  @type.builtin
  @punctuation.bracket
  @variable.parameter
  @keyword
hl_test: json ABI=14; fixture without ERROR; highlights query compiled
  @string.special.key
  @string
  @number
  @constant.builtin
  @escape
  @comment
hl_test: toml ABI=14; fixture without ERROR; highlights query compiled
  @type
  @string
  @property
  @boolean
  @comment
  @number
  @string.special
  @punctuation.delimiter
  @operator
  @punctuation.bracket
hl_test: yaml ABI=14; fixture without ERROR; highlights query compiled
  @boolean
  @constant.builtin
  @string
  @number
  @comment
  @label
  @type
  @attribute
  @property
  @punctuation.delimiter
  @punctuation.bracket
  @punctuation.special
hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard
```

Default LeakSanitizer invocation, before that environment-only adjustment:

```text
==4==LeakSanitizer has encountered a fatal error.
==4==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==4==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

## Release build

Command: make all build/sublimite-typescript. Observed exit: success.
The opt-in target links by --undefined=hl_language_typescript, matching the
sibling Julia target. Default binary has no grammar/wrapper symbols; forced
TypeScript binary has them. Symbol output is in build/hl-evidence/*-symbols.log.
Final build output:

```text
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER -include tests/display_guard.h -MMD -MP -c bench/x11_bench.c -o build/rel/bench/x11_bench.o
gcc -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER -include tests/display_guard.h -MMD -MP -c bench/zygote_bench.c -o build/rel/bench/zygote_bench.o
gcc -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
gcc -O2 -g -msse2 -pthread build/rel/src/main.o -Wl,--undefined=hl_language_typescript build/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/sublimite-typescript
```

## Size measurements

Commands: size on release parser/scanner objects and editor binaries; stat file
size on the same binaries. Baseline linked before implementation; all measured
links use the same release main, libraries and flags. Per-language links force
one wrapper reference using --undefined; the combined link forces this bead's
language wrappers together. TSX was measured before pruning using its own
parser/scanner objects explicitly linked into the baseline editor. All size
entries below are (M) [AC], stamped with BAT0/status. Arithmetic and the size
selection decision are in the decision document.

```text
BAT0/status: Not charging (M) [AC]
   text	   data	    bss	    dec	    hex	filename
 407586	   2728	      0	 410314	  642ca	build/rel/vendor/tree-sitter-javascript/src/parser.o
   2452	      0	      0	   2452	    994	build/rel/vendor/tree-sitter-javascript/src/scanner.o
   5255	    488	      0	   5743	   166f	build/rel/vendor/tree-sitter-json/src/parser.o
  24509	    760	      0	  25269	   62b5	build/rel/vendor/tree-sitter-toml/src/parser.o
    649	      0	      0	    649	    289	build/rel/vendor/tree-sitter-toml/src/scanner.o
1406114	   3984	      0	1410098	 158432	build/rel/vendor/tree-sitter-typescript/typescript/src/parser.o
   2353	      0	      0	   2353	    931	build/rel/vendor/tree-sitter-typescript/typescript/src/scanner.o
 165584	   2728	      0	 168312	  29178	build/rel/vendor/tree-sitter-yaml/src/parser.o
  21188	      0	      0	  21188	   52c4	build/rel/vendor/tree-sitter-yaml/src/scanner.o
1438754	   4120	      0	1442874	 16043a	build/hl-evidence/tsx/parser.o
   2353	      0	      0	   2353	    931	build/hl-evidence/tsx/scanner.o
1350289	 140952	17574660	19065901	122ec2d	build/hl-evidence/sublimite-baseline
1350289	 140952	17574660	19065901	122ec2d	build/sublimite
2802290	 145136	17574660	20522086	1392466	build/hl-evidence/sublimite-tsx
1768150	 143752	17574660	19486562	1295762	build/hl-evidence/sublimite-javascript
2769238	 144976	17574660	20488874	138a2aa	build/hl-evidence/sublimite-typescript
1356529	 141464	17574660	19072653	123068d	build/hl-evidence/sublimite-json
1377457	 141752	17574660	19093869	123596d	build/hl-evidence/sublimite-toml
1545009	 143736	17574660	19263405	125efad	build/hl-evidence/sublimite-yaml
3412274	 151824	17574660	21138758	1428d46	build/hl-evidence/sublimite-combined

ELF file bytes (stat st_size):
5131656 build/hl-evidence/sublimite-baseline
5131656 build/sublimite
6681992 build/hl-evidence/sublimite-tsx
5635224 build/hl-evidence/sublimite-javascript
6637512 build/hl-evidence/sublimite-typescript
5146240 build/hl-evidence/sublimite-json
5184264 build/hl-evidence/sublimite-toml
5434952 build/hl-evidence/sublimite-yaml
7507232 build/hl-evidence/sublimite-combined
```

Default binary's measured delta is zero by subtracting identical baseline and
default rows. TypeScript's forced link and the combined grammar contribution
miss the supplied total binary aspiration (E, from the brief). The pre-bead
unstripped ELF already misses it; this is reported, not repaired outside scope.
TSX is not small against that total goal and is deliberately omitted as allowed.

## Repository sanitizer check and non-display outcomes

All sanitizer binaries compiled successfully. Plain make check stopped at
base_test because LeakSanitizer cannot operate under the sandbox's ptrace.
The environment-only retry retained ASan and UBSan, disabled leak detection,
and used EDIT_DISPLAY_LOCK_TIMEOUT=0 to refuse an occupied display immediately
(P: zero waiting, not an invented timeout budget). It stopped at cli_test's
occupied display lock. An independent read-only X socket connection probe
also failed with Operation not permitted. Display suites fail inside this
sandbox for lack of accessible X; the coordinator must rerun them on Xvfb :99.
The full display suite and final display-lock shell contract are not claimed.

Plain make check tail (observed nonzero exit):

```text
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/x11_xi2_test.o build/san/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/x11_xi2_test
== build/san/tests/base_test
==439==LeakSanitizer has encountered a fatal error.
==439==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==439==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:159: check] Error 1
```

Command: ASAN_OPTIONS=detect_leaks=0 EDIT_DISPLAY_LOCK_TIMEOUT=0 make check.
Observed nonzero exit:

```text
== build/san/tests/base_test
0.0.1-dev
arena: ok
malloc guard: SKIP (AddressSanitizer owns malloc)
pool: ok
malloc guard exempt: SKIP (sanitizer owns malloc)
cpu: avx2=1 popcnt=1
base_test: PASS
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test: display lock timeout: display :99 (held by unknown)
make: *** [Makefile:159: check] Error 1
```

Read-only socket-access probe:

```text
X socket connection unavailable: [Errno 1] Operation not permitted
```

After the check binaries built, run every non-display TEST_SAN binary serially
using the Makefile's exact DISPLAY_TEST_SAN exclusion patterns. This reproduces
the non-display binary loop while continuing after failures so later suites
are not hidden. Command: python3 build/hl-evidence/run-non-display.py.
Environment: ASAN_OPTIONS=detect_leaks=0, DISPLAY=:99, EDIT_DISPLAY=:99.
The driver and complete output are retained under build/hl-evidence.

(M) [AC]: 55 passing + 3 failing = 58 binaries,
derived by counting completed entries in non-display-results.json and comparing
against the Makefile-derived non-display-binaries.txt. Each run records BAT0
and /proc/loadavg; all BAT0 readings were Not charging. No non-display suite
was skipped. Repository non-display acceptance is not green.

| Non-display suite | Observed exit (M) [AC] |
| --- | ---: |
| base_test | 0 |
| config_editor_test | 0 |
| config_test | 0 |
| config_worker_test | 0 |
| file_kill_test | 1 |
| file_test | 0 |
| find_bench_test | 0 |
| find_bestof_test | 0 |
| find_p1r_test | 0 |
| find_simd_filter_verify_test | 0 |
| find_test | 0 |
| find_timeout_test | 0 |
| findui_test | 0 |
| font_selection_test | 0 |
| font_size_test | 0 |
| font_test | 0 |
| harness_test | 0 |
| hl_parse_test | 0 |
| hl_test | 0 |
| hl_worker_test | 0 |
| indent_test | 0 |
| ipc_test | 1 |
| journal_kill_test | 1 |
| journal_test | 0 |
| keys_test | 0 |
| layout_test | 0 |
| lineidx_test | 0 |
| minimap_test | 0 |
| piece_bounds_test | 0 |
| piece_mem_test | 0 |
| piece_mt_test | 0 |
| piece_test | 0 |
| prewake_test | 0 |
| raster_scroll_test | 0 |
| render_test | 0 |
| replay_test | 0 |
| runtime_test | 0 |
| savectl_reload_test | 0 |
| savectl_test | 0 |
| scan_test | 0 |
| scroll_display_test | 0 |
| scroll_test | 0 |
| startup_test | 0 |
| tabs_test | 0 |
| trace_pairs_test | 0 |
| trace_test | 0 |
| undo_test | 0 |
| unicode_render_test | 0 |
| utf8_tables_test | 0 |
| utf8_test | 0 |
| view_test | 0 |
| wayland_bench_test | 0 |
| wayland_gl_test | 0 |
| wayland_test | 0 |
| window_size_test | 0 |
| work_foreground_test | 0 |
| work_test | 0 |
| wrap_test | 0 |

Shell check: ASAN_OPTIONS=detect_leaks=0 sh tools/test_replay_cli.sh build
passed after its required sanitizer replay binary was built:

```text
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

## Outside this bead's scope; left unchanged

The following failures are in unchanged suites/modules. None of these failed
binaries contains hl_language_* or this bead's tree_sitter_<language> symbols,
confirmed with nm in build/hl-evidence/unrelated-suite-symbols.log. This is not
a claim that the repository's existing failures have been fixed or that their
root causes have been fully diagnosed.

### file_kill_test

The existing fixture assertions fail; diagnosis/fixes are outside the assigned grammar bead.

```text
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
SELF_CHECK section=26 PASS
SELF_CHECK section=27 PASS
SELF_CHECK section=28 PASS
file_cancelled_open: PASS queued/pending-prefix/pending-map getters and filtered delivery
file_save_alloc: PASS pooled fresh ack guard=ASan-inert (release required)
file_kill_test: FAIL visibility/cancellation; power-loss barriers unverified
```

### ipc_test

IPC initialization reports the sandbox permission error directly.

```text
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
```

### journal_kill_test

The existing fixture assertions fail; diagnosis/fixes are outside the assigned grammar bead.

```text
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
```

Other boundaries: the unstripped pre-bead ELF already exceeds the supplied
whole-binary size aspiration; TypeScript stays opt-in and TSX is omitted after
measurement. TypeScript's retained query supplies extension scopes only;
composing inherited JavaScript scopes remains the shared query/scope worker's
work. LeakSanitizer under ptrace and the occupied shared display lock are
environment limits, not grammar acceptance successes.

## What remains

The grammar implementation and pruning are complete within this bead. The
coordinator needs an untraced leak check, the display suites/full check on
Xvfb :99 with the display lane available, and triage/reruns of the file-kill,
IPC and journal-kill failures above. Registration/query predicates/paint and
combined grammar size after other workers merge remain outside this bead.
No additional bead was implemented, no unrelated fixes were made, and no
protected coordination document or sibling worktree was edited. Stop here.

## Every Makefile change

Ordinary diff against the saved pre-bead Makefile, without git:

```diff
--- build/hl-evidence/Makefile.before	2026-10-13 00:17:52.149209754 +1000
+++ Makefile	2026-10-13 00:21:10.181623722 +1000
@@ -33,6 +33,11 @@
 B := build
 TS_SRC := vendor/tree-sitter/lib/src/lib.c
 SRCS   := $(sort $(wildcard src/*/*.c)) $(TS_SRC)
+SRCS += vendor/tree-sitter-javascript/src/parser.c vendor/tree-sitter-javascript/src/scanner.c
+SRCS += vendor/tree-sitter-typescript/typescript/src/parser.c vendor/tree-sitter-typescript/typescript/src/scanner.c
+SRCS += vendor/tree-sitter-json/src/parser.c
+SRCS += vendor/tree-sitter-toml/src/parser.c vendor/tree-sitter-toml/src/scanner.c
+SRCS += vendor/tree-sitter-yaml/src/parser.c vendor/tree-sitter-yaml/src/scanner.c
 MAINC  := $(wildcard src/main.c)
 TESTS  := $(sort $(wildcard tests/*_test.c))
 BENCHS := $(sort $(wildcard bench/*_bench.c))
@@ -48,6 +53,26 @@
 # C11, Wall/Wextra/Werror, project optimization and sanitizer flags.
 TS_OBJ := $(addprefix $(B)/,$(addsuffix /$(TS_SRC:.c=.o),rel san fuzz))
 $(TS_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN))
+# Javascript uses its own parser ABI headers and upstream scanner warnings.
+TS_JAVASCRIPT_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-javascript/src/,parser.o scanner.o))
+$(TS_JAVASCRIPT_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-parameter
+$(TS_JAVASCRIPT_OBJ): INC := -Ivendor/tree-sitter-javascript/src $(INC)
+# Typescript uses its own parser ABI headers and upstream scanner warnings.
+TS_TYPESCRIPT_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-typescript/typescript/src/,parser.o scanner.o))
+$(TS_TYPESCRIPT_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-parameter
+$(TS_TYPESCRIPT_OBJ): INC := -Ivendor/tree-sitter-typescript/typescript/src $(INC)
+# Json uses its own parser ABI headers and upstream scanner warnings.
+TS_JSON_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-json/src/,parser.o))
+$(TS_JSON_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN))
+$(TS_JSON_OBJ): INC := -Ivendor/tree-sitter-json/src $(INC)
+# Toml uses its own parser ABI headers and upstream scanner warnings.
+TS_TOML_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-toml/src/,parser.o scanner.o))
+$(TS_TOML_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-parameter
+$(TS_TOML_OBJ): INC := -Ivendor/tree-sitter-toml/src $(INC)
+# Yaml uses its own parser ABI headers and upstream scanner warnings.
+TS_YAML_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-yaml/src/,parser.o scanner.o))
+$(TS_YAML_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-value -Wno-unused-but-set-variable -Wno-implicit-fallthrough
+$(TS_YAML_OBJ): INC := -Ivendor/tree-sitter-yaml/src $(INC)
 
 REL_LIB  := $(B)/libedit.a
 SAN_LIB  := $(B)/san/libedit.a
@@ -107,6 +132,9 @@
 	rm -f $@; $(AR) rcs $@ $^
 
 # ---- binaries ----
+# TypeScript exceeds the assigned total binary goal when linked; archive extraction is opt-in.
+$(B)/sublimite-typescript: $(B)/rel/src/main.o $(REL_LIB)
+	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_typescript $(REL_LIB) $(LDLIBS) -o $@
 $(B)/sublimite: $(B)/rel/src/main.o $(REL_LIB)
 	$(CC_RELEASE) $(REL_F) -pthread $< $(REL_LIB) $(LDLIBS) -o $@
 $(B)/tools/%: $(B)/rel/tools/%.o $(REL_LIB)
```

## Deleted vendor files

Generator/package/build metadata, non-highlight queries, grammar.json,
node-types.json, alternate schemas and optional TSX were pruned. The required
TypeScript common/scanner.h stays. YAML's default core schema contents now
live verbatim in scanner.c. Exact deleted inventory:

```text
vendor/tree-sitter-javascript/.editorconfig
vendor/tree-sitter-javascript/.gitattributes
vendor/tree-sitter-javascript/.gitignore
vendor/tree-sitter-javascript/CMakeLists.txt
vendor/tree-sitter-javascript/Cargo.lock
vendor/tree-sitter-javascript/Cargo.toml
vendor/tree-sitter-javascript/Makefile
vendor/tree-sitter-javascript/Package.resolved
vendor/tree-sitter-javascript/Package.swift
vendor/tree-sitter-javascript/README.md
vendor/tree-sitter-javascript/binding.gyp
vendor/tree-sitter-javascript/eslint.config.mjs
vendor/tree-sitter-javascript/go.mod
vendor/tree-sitter-javascript/go.sum
vendor/tree-sitter-javascript/grammar.js
vendor/tree-sitter-javascript/package-lock.json
vendor/tree-sitter-javascript/package.json
vendor/tree-sitter-javascript/pyproject.toml
vendor/tree-sitter-javascript/queries/highlights-jsx.scm
vendor/tree-sitter-javascript/queries/highlights-params.scm
vendor/tree-sitter-javascript/queries/injections.scm
vendor/tree-sitter-javascript/queries/locals.scm
vendor/tree-sitter-javascript/queries/tags.scm
vendor/tree-sitter-javascript/setup.py
vendor/tree-sitter-javascript/src/grammar.json
vendor/tree-sitter-javascript/src/node-types.json
vendor/tree-sitter-javascript/tree-sitter.json
vendor/tree-sitter-json/.editorconfig
vendor/tree-sitter-json/.gitattributes
vendor/tree-sitter-json/.gitignore
vendor/tree-sitter-json/CMakeLists.txt
vendor/tree-sitter-json/Cargo.lock
vendor/tree-sitter-json/Cargo.toml
vendor/tree-sitter-json/Makefile
vendor/tree-sitter-json/Package.resolved
vendor/tree-sitter-json/Package.swift
vendor/tree-sitter-json/README.md
vendor/tree-sitter-json/binding.gyp
vendor/tree-sitter-json/eslint.config.mjs
vendor/tree-sitter-json/go.mod
vendor/tree-sitter-json/go.sum
vendor/tree-sitter-json/grammar.js
vendor/tree-sitter-json/package-lock.json
vendor/tree-sitter-json/package.json
vendor/tree-sitter-json/pyproject.toml
vendor/tree-sitter-json/setup.py
vendor/tree-sitter-json/src/grammar.json
vendor/tree-sitter-json/src/node-types.json
vendor/tree-sitter-json/tree-sitter.json
vendor/tree-sitter-toml/.editorconfig
vendor/tree-sitter-toml/.gitattributes
vendor/tree-sitter-toml/.gitignore
vendor/tree-sitter-toml/CMakeLists.txt
vendor/tree-sitter-toml/Cargo.lock
vendor/tree-sitter-toml/Cargo.toml
vendor/tree-sitter-toml/Makefile
vendor/tree-sitter-toml/Package.resolved
vendor/tree-sitter-toml/Package.swift
vendor/tree-sitter-toml/README.md
vendor/tree-sitter-toml/binding.gyp
vendor/tree-sitter-toml/eslint.config.mjs
vendor/tree-sitter-toml/go.mod
vendor/tree-sitter-toml/go.sum
vendor/tree-sitter-toml/grammar.js
vendor/tree-sitter-toml/package-lock.json
vendor/tree-sitter-toml/package.json
vendor/tree-sitter-toml/pyproject.toml
vendor/tree-sitter-toml/setup.py
vendor/tree-sitter-toml/src/grammar.json
vendor/tree-sitter-toml/src/node-types.json
vendor/tree-sitter-toml/tree-sitter.json
vendor/tree-sitter-typescript/.editorconfig
vendor/tree-sitter-typescript/.gitattributes
vendor/tree-sitter-typescript/.gitignore
vendor/tree-sitter-typescript/CMakeLists.txt
vendor/tree-sitter-typescript/Cargo.lock
vendor/tree-sitter-typescript/Cargo.toml
vendor/tree-sitter-typescript/Makefile
vendor/tree-sitter-typescript/Package.resolved
vendor/tree-sitter-typescript/Package.swift
vendor/tree-sitter-typescript/README.md
vendor/tree-sitter-typescript/binding.gyp
vendor/tree-sitter-typescript/common/common.mak
vendor/tree-sitter-typescript/common/define-grammar.js
vendor/tree-sitter-typescript/eslint.config.mjs
vendor/tree-sitter-typescript/go.mod
vendor/tree-sitter-typescript/go.sum
vendor/tree-sitter-typescript/package-lock.json
vendor/tree-sitter-typescript/package.json
vendor/tree-sitter-typescript/pyproject.toml
vendor/tree-sitter-typescript/queries/locals.scm
vendor/tree-sitter-typescript/queries/tags.scm
vendor/tree-sitter-typescript/setup.py
vendor/tree-sitter-typescript/tree-sitter.json
vendor/tree-sitter-typescript/tsx/CMakeLists.txt
vendor/tree-sitter-typescript/tsx/Makefile
vendor/tree-sitter-typescript/tsx/grammar.js
vendor/tree-sitter-typescript/tsx/package.json
vendor/tree-sitter-typescript/tsx/src/grammar.json
vendor/tree-sitter-typescript/tsx/src/node-types.json
vendor/tree-sitter-typescript/tsx/src/parser.c
vendor/tree-sitter-typescript/tsx/src/scanner.c
vendor/tree-sitter-typescript/tsx/src/tree_sitter/alloc.h
vendor/tree-sitter-typescript/tsx/src/tree_sitter/array.h
vendor/tree-sitter-typescript/tsx/src/tree_sitter/parser.h
vendor/tree-sitter-typescript/typescript/CMakeLists.txt
vendor/tree-sitter-typescript/typescript/Makefile
vendor/tree-sitter-typescript/typescript/grammar.js
vendor/tree-sitter-typescript/typescript/package.json
vendor/tree-sitter-typescript/typescript/src/grammar.json
vendor/tree-sitter-typescript/typescript/src/node-types.json
vendor/tree-sitter-yaml/.editorconfig
vendor/tree-sitter-yaml/.gitattributes
vendor/tree-sitter-yaml/.gitignore
vendor/tree-sitter-yaml/.gitmodules
vendor/tree-sitter-yaml/CMakeLists.txt
vendor/tree-sitter-yaml/Cargo.lock
vendor/tree-sitter-yaml/Cargo.toml
vendor/tree-sitter-yaml/Makefile
vendor/tree-sitter-yaml/Package.resolved
vendor/tree-sitter-yaml/Package.swift
vendor/tree-sitter-yaml/README.md
vendor/tree-sitter-yaml/binding.gyp
vendor/tree-sitter-yaml/eslint.config.mjs
vendor/tree-sitter-yaml/go.mod
vendor/tree-sitter-yaml/go.sum
vendor/tree-sitter-yaml/grammar.js
vendor/tree-sitter-yaml/package-lock.json
vendor/tree-sitter-yaml/package.json
vendor/tree-sitter-yaml/pyproject.toml
vendor/tree-sitter-yaml/schema/core/grammar.js
vendor/tree-sitter-yaml/schema/core/package.json
vendor/tree-sitter-yaml/schema/core/src/grammar.json
vendor/tree-sitter-yaml/schema/core/src/node-types.json
vendor/tree-sitter-yaml/schema/core/src/parser.c
vendor/tree-sitter-yaml/schema/core/src/tree_sitter/alloc.h
vendor/tree-sitter-yaml/schema/core/src/tree_sitter/array.h
vendor/tree-sitter-yaml/schema/core/src/tree_sitter/parser.h
vendor/tree-sitter-yaml/schema/json/grammar.js
vendor/tree-sitter-yaml/schema/json/package.json
vendor/tree-sitter-yaml/schema/json/src/grammar.json
vendor/tree-sitter-yaml/schema/json/src/node-types.json
vendor/tree-sitter-yaml/schema/json/src/parser.c
vendor/tree-sitter-yaml/schema/json/src/tree_sitter/alloc.h
vendor/tree-sitter-yaml/schema/json/src/tree_sitter/array.h
vendor/tree-sitter-yaml/schema/json/src/tree_sitter/parser.h
vendor/tree-sitter-yaml/schema/legacy/grammar.js
vendor/tree-sitter-yaml/schema/legacy/package.json
vendor/tree-sitter-yaml/schema/legacy/src/grammar.json
vendor/tree-sitter-yaml/schema/legacy/src/node-types.json
vendor/tree-sitter-yaml/schema/legacy/src/parser.c
vendor/tree-sitter-yaml/schema/legacy/src/tree_sitter/alloc.h
vendor/tree-sitter-yaml/schema/legacy/src/tree_sitter/array.h
vendor/tree-sitter-yaml/schema/legacy/src/tree_sitter/parser.h
vendor/tree-sitter-yaml/schema/update-schema.js
vendor/tree-sitter-yaml/setup.py
vendor/tree-sitter-yaml/src/grammar.json
vendor/tree-sitter-yaml/src/node-types.json
vendor/tree-sitter-yaml/src/schema.core.c
vendor/tree-sitter-yaml/src/schema.json.c
vendor/tree-sitter-yaml/src/schema.legacy.c
vendor/tree-sitter-yaml/tree-sitter.json
```
