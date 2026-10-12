# edit-v0o.4 — Python and Julia grammars (s6)

Work stayed in this worktree. No bd or git commands ran. No HANDOFF.md,
worklog, CLAUDE.md, PLAN.md or PRD.md edits. This report covers this bead only.

## Files touched

- Makefile: only Python/Julia source lists, object-specific flags/includes and
  an explicit Julia opt-in binary target; every changed line is pasted below.
- src/hl/lang.h: include guard/API include and this bead's language declarations.
- src/hl/lang_python.c and src/hl/lang_julia.c: allocation-free static-language wrappers.
- tests/hl_test.c: both real grammar ABI, parse and query probes; wrapper allocation guard.
- vendor/tree-sitter-python/src/scanner.c and vendor/tree-sitter-julia/src/scanner.c:
  normalize external scanner create's empty parameter list to `(void)`.
- Both grammar PROVENANCE files: document the local scanner normalization,
  preserving the supplied upstream release tarball digest.
- docs/decisions/edit-v0o.4.md and this report.

Deleted from vendor/tree-sitter-python: src/grammar.json, src/node-types.json,
queries/tags.scm. Deleted from vendor/tree-sitter-julia: src/grammar.json,
src/node-types.json, queries/injections.scm, queries/locals.scm. These generator
metadata and unused query files are not build dependencies. The highlight
queries, generated parsers, ABI headers and LICENSE files are retained.

## Red run, before implementation

Command: `make build/rel/tests/hl_test.o`. (P) Failure/success exit identities
are tool contracts; diagnostics' line numbers are source locators, flags/ABI
versions are specification identities, not invented measured budgets.

```text
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER -include tests/display_guard.h -MMD -MP -c tests/hl_test.c -o build/rel/tests/hl_test.o
tests/hl_test.c:4:10: fatal error: hl/lang.h: No such file or directory
    4 | #include "hl/lang.h"
      |          ^~~~~~~~~~~
compilation terminated.
make: *** [Makefile:81: build/rel/tests/hl_test.o] Error 1
```

## Sanitizer red and correction

The initial release grammar probes passed. The initial sanitizer run diagnosed
the upstream scanner create definition's old-style function type; the generated
parser and runtime declare a void parameter list. Rerunning the original binary
with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1` produced:

```text
vendor/tree-sitter/lib/src/./parser.c:369:40: runtime error: call to function tree_sitter_python_external_scanner_create through pointer to incorrect function type 'void *(*)(void)'
/home/tobias/Projects/sublimite/.wt/edit-v0o.4-s6/vendor/tree-sitter-python/src/scanner.c:419: note: tree_sitter_python_external_scanner_create defined here
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior vendor/tree-sitter/lib/src/./parser.c:369:40 
```

The local `(void)` normalization fixes that diagnostic for both scanners;
no function sanitizer check was disabled. The initial leaks-enabled run also
reported that LeakSanitizer cannot operate under this sandbox's ptrace/tracing.
The subsequent sandbox runs disable leak detection only and retain address and
undefined-behavior instrumentation, with UBSan stopping at the first diagnostic.
The coordinator must rerun leak detection outside the tracing sandbox.

## Green runs

`make -j2 all build/sublimite-julia build/san/tests/hl_test` passed after the final
scanner normalization. Compiler identities are gcc release and clang sanitizer
as specified in the brief. Full build output: build/hl-evidence/all-final.log.

`build/tests/hl_test` passed; output saved in build/hl-evidence/green-release.log.
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/hl_test`
passed with the same capture inventory and without sanitizer diagnostics:

```text
hl_test: python ABI=15; fixture without ERROR; highlights query compiled
  @variable
  @constructor
  @constant
  @function
  @function.method
  @function.builtin
  @property
  @type
  @constant.builtin
  @number
  @comment
  @string
  @escape
  @punctuation.special
  @embedded
  @operator
  @keyword
hl_test: julia ABI=15; fixture without ERROR; highlights query compiled
  @variable
  @variable.member
  @string.special.symbol
  @function.call
  @_pipe
  @function.macro
  @function.builtin
  @type.definition
  @type
  @operator
  @type.builtin
  @keyword
  @keyword.conditional
  @keyword.conditional.ternary
  @keyword.exception
  @keyword.repeat
  @keyword.modifier
  @keyword.function
  @keyword.return
  @keyword.import
  @punctuation.delimiter
  @keyword.type
  @punctuation.special
  @punctuation.bracket
  @keyword.operator
  @constant.builtin
  @variable.builtin
  @boolean
  @number
  @number.float
  @character
  @string.escape
  @string
  @string.special
  @string.documentation
  @comment
hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard
```

All highlight capture names, including Julia's `_pipe` helper, are enumerated
in docs/decisions/edit-v0o.4.md for the shared scope-table worker. No predicate
execution, query-to-theme mapping or editor highlighting is claimed here.

## Size and optional link

(M) [AC], checked BAT0/status immediately before `size`, `wc -c` and `du`:
`Not charging`. Object section sizes, sums and derivations are in the decision
document; raw output is build/hl-evidence/size.log. Julia alone exceeds the
supplied P6.3 total-binary planning goal. The default link omits the unreferenced
Julia archive members, while `make build/sublimite-julia` explicitly extracts
them. Both grammars remain unconditionally tested and archived.

The target's red test was `make -n build/sublimite-julia` before adding its rule:

```text
make: *** No rule to make target 'build/sublimite-julia'.  Stop.
```

Green verification inspected both linked binaries with `nm -g`:

```text
PASS: default editor omits Julia symbols; explicit target extracts Julia wrapper/parser/scanner
```

Final linked measurement output follows. Every size entry is (M) [AC], from the
named tool. GNU size's `dec` includes bss; `wc -c` measures on-disk ELF bytes;
`strip --strip-debug -o` wrote measurement copies only, preserving build outputs.
The release binaries include project debug information, so the raw ELF size is
also reported, not hidden behind section-only accounting.

```text
Not charging
   text	   data	    bss	    dec	    hex	filename
1350289	 140952	17574660	19065901	122ec2d	build/sublimite
3979937	 143280	17574660	21697877	14b1555	build/sublimite-julia
 5131656 build/sublimite
 7839320 build/sublimite-julia
12970976 total
1584984 build/hl-evidence/sublimite-stripped
4224240 build/hl-evidence/sublimite-julia-stripped
5809224 total
```

The stripped opt-in file remains above the supplied goal. The default editor
has no grammar dispatch yet; future shared registration must preserve optional
Julia selection. No complete all-grammar/whole-editor size acceptance is claimed.

## Makefile changes, every line

The additions leave existing shared rules and link recipes unchanged. Object
warning choices and their upstream reasons are in the decision document.
Diff hunk numbers below are source locators derived from the saved pre-edit file.

```diff
--- Makefile before
+++ Makefile after
@@ -33,6 +33,8 @@
 B := build
 TS_SRC := vendor/tree-sitter/lib/src/lib.c
 SRCS   := $(sort $(wildcard src/*/*.c)) $(TS_SRC)
+SRCS += vendor/tree-sitter-python/src/parser.c vendor/tree-sitter-python/src/scanner.c
+SRCS += vendor/tree-sitter-julia/src/parser.c vendor/tree-sitter-julia/src/scanner.c
 MAINC  := $(wildcard src/main.c)
 TESTS  := $(sort $(wildcard tests/*_test.c))
 BENCHS := $(sort $(wildcard bench/*_bench.c))
@@ -48,6 +50,14 @@
 # C11, Wall/Wextra/Werror, project optimization and sanitizer flags.
 TS_OBJ := $(addprefix $(B)/,$(addsuffix /$(TS_SRC:.c=.o),rel san fuzz))
 $(TS_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN))
+# Python upstream scanner narrows array sizes and discards array_pop values.
+TS_PYTHON_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-python/src/,parser.o scanner.o))
+$(TS_PYTHON_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-value
+$(TS_PYTHON_OBJ): INC := -Ivendor/tree-sitter-python/src $(INC)
+# Julia scanner callbacks leave unused ABI parameters; use its own parser ABI.
+TS_JULIA_OBJ := $(foreach c,rel san fuzz,$(addprefix $(B)/$(c)/vendor/tree-sitter-julia/src/,parser.o scanner.o))
+$(TS_JULIA_OBJ): WARN := $(filter-out -Wconversion -Wshadow,$(WARN)) -Wno-unused-parameter
+$(TS_JULIA_OBJ): INC := -Ivendor/tree-sitter-julia/src $(INC)
 
 REL_LIB  := $(B)/libedit.a
 SAN_LIB  := $(B)/san/libedit.a
@@ -107,6 +117,9 @@
 	rm -f $@; $(AR) rcs $@ $^
 
 # ---- binaries ----
+# Julia exceeds the assigned grammar size goal; archive extraction is opt-in.
+$(B)/sublimite-julia: $(B)/rel/src/main.o $(REL_LIB)
+	$(CC_RELEASE) $(REL_F) -pthread $< -Wl,--undefined=hl_language_julia $(REL_LIB) $(LDLIBS) -o $@
 $(B)/sublimite: $(B)/rel/src/main.o $(REL_LIB)
 	$(CC_RELEASE) $(REL_F) -pthread $< $(REL_LIB) $(LDLIBS) -o $@
 $(B)/tools/%: $(B)/rel/tools/%.o $(REL_LIB)
```

## Final build-rule contract correction

During the final Makefile review, found that appending grammar sources to
TS_SRC changed the existing single-source runtime warning-rule expansion into
phantom non-configuration targets. A build-contract probe compared its targets
to the runtime object in each declared configuration; red output:

```text
Traceback (most recent call last):
  File "/home/tobias/Projects/sublimite/.wt/edit-v0o.4-s6/build/hl-evidence/build-contract.py", line 4, in <module>
    assert result == expected, "runtime warning rule contains phantom non-runtime object targets"
           ^^^^^^^^^^^^^^^^^^
AssertionError: runtime warning rule contains phantom non-runtime object targets
```

Moved only this bead's source-list additions to SRCS after the existing shared
assignment. The archive inputs and object flags stay the same, while TS_SRC
and its original target rule remain untouched. Green contract output:

```text
PASS: original runtime warning rule retains only its configuration objects
```

Final `make all` and both release/sanitizer hl_test runs passed again after
this correction; the red/green report above and Makefile diff reflect the
final state. No additional module source changed.

## Repository verification and findings outside this bead

Final literal `make all` passed:

```text
make: Nothing to be done for 'all'.
```

Full command:
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 EDIT_DISPLAY_LOCK_TIMEOUT=0 make -j2 check`.
All sanitizer test binaries built, but the driver stopped in cli_test during
socket setup. Relevant tail from build/hl-evidence/check.log:

```text
base_test: PASS
== build/san/tests/cli_test
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:144: check] Error 1
```

To avoid that early failure hiding the other results, derived the headless
inventory with `$(filter-out $(DISPLAY_TEST_SAN),$(TEST_SAN))` from the actual
Makefile and ran each binary sequentially with the same sanitizer environment
and safe display. (M) [AC]: 58 named binaries = 53 zero exit statuses
+ 5 nonzero exit statuses, counted directly from the following result file.
BAT0 remained `Not charging`. Some suites in that Makefile inventory still have
display-dependent subfixtures; the label is not proof they are entirely headless.

```text
build/san/tests/base_test: exit 0
build/san/tests/config_editor_test: exit 0
build/san/tests/config_test: exit 0
build/san/tests/config_worker_test: exit 0
build/san/tests/file_kill_test: exit 1
build/san/tests/file_test: exit 1
build/san/tests/find_bench_test: exit 0
build/san/tests/find_bestof_test: exit 0
build/san/tests/find_p1r_test: exit 0
build/san/tests/find_simd_filter_verify_test: exit 0
build/san/tests/find_test: exit 0
build/san/tests/find_timeout_test: exit 0
build/san/tests/findui_test: exit 0
build/san/tests/font_selection_test: exit 0
build/san/tests/font_size_test: exit 0
build/san/tests/font_test: exit 0
build/san/tests/harness_test: exit 0
build/san/tests/hl_parse_test: exit 0
build/san/tests/hl_test: exit 0
build/san/tests/hl_worker_test: exit 0
build/san/tests/indent_test: exit 0
build/san/tests/ipc_test: exit 1
build/san/tests/journal_kill_test: exit 1
build/san/tests/journal_test: exit 0
build/san/tests/keys_test: exit 0
build/san/tests/layout_test: exit 0
build/san/tests/lineidx_test: exit 1
build/san/tests/minimap_test: exit 0
build/san/tests/piece_bounds_test: exit 0
build/san/tests/piece_mem_test: exit 0
build/san/tests/piece_mt_test: exit 0
build/san/tests/piece_test: exit 0
build/san/tests/prewake_test: exit 0
build/san/tests/raster_scroll_test: exit 0
build/san/tests/render_test: exit 0
build/san/tests/replay_test: exit 0
build/san/tests/runtime_test: exit 0
build/san/tests/savectl_reload_test: exit 0
build/san/tests/savectl_test: exit 0
build/san/tests/scan_test: exit 0
build/san/tests/scroll_display_test: exit 0
build/san/tests/scroll_test: exit 0
build/san/tests/startup_test: exit 0
build/san/tests/tabs_test: exit 0
build/san/tests/trace_pairs_test: exit 0
build/san/tests/trace_test: exit 0
build/san/tests/undo_test: exit 0
build/san/tests/unicode_render_test: exit 0
build/san/tests/utf8_tables_test: exit 0
build/san/tests/utf8_test: exit 0
build/san/tests/view_test: exit 0
build/san/tests/wayland_bench_test: exit 0
build/san/tests/wayland_gl_test: exit 0
build/san/tests/wayland_test: exit 0
build/san/tests/window_size_test: exit 0
build/san/tests/work_foreground_test: exit 0
build/san/tests/work_test: exit 0
build/san/tests/wrap_test: exit 0
```

Findings outside this bead; reported, not fixed:

- file_kill_test fails at its view_init fixture and queued-child assertions
  in this sandbox. The fixture needs the display; coordinator rerun is required.
- file_test's shared-inotify assertion fails at tests/file_test.c:308 in the
  sandbox. An authorized rerun outside the sandbox passed the entire suite
  (build/hl-evidence/file-test-unsandboxed.log). That does not establish whether
  the initial failure was policy-related or transient.
- ipc_test in the sandbox reports `init rc=4 errno=1 (Operation not permitted)`
  and fails socket setup at tests/ipc_test.c:130. An authorized full rerun with
  Unix sockets available progresses further but fails the existing main launcher
  assertion `FAIL tests/ipc_test.c:752: accepted`. No IPC/launcher source changed.
- journal_kill_test fails its initial recovery trial with the following output:

```text
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
```

- lineidx_test fails the existing mapped-file readiness fixture:

```text
tests/lineidx_test.c:339: FATAL file_open_ready(f) && file_open_mode(f) == FILE_MODE_MMAP
lineidx_test: 1 failure(s)
```

A representative display suite, x11_identity_test, fails
`plat_init(&p, &cfg) == PLAT_OK` on :99. A direct Unix X-socket probe reports
`[Errno 1] Operation not permitted`. Display suites cannot access the X socket
inside this sandbox; the coordinator must rerun them on Xvfb :99.

Focused command:
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 EDIT_DISPLAY_LOCK_TIMEOUT=0 make check TEST_SAN='build/san/tests/hl_test build/san/tests/hl_parse_test build/san/tests/hl_worker_test'`.
The highlight binaries and replay CLI shell checks pass, but the final unrelated
display-lock contract cannot acquire the already-held display lock. Tail:

```text
== build/san/tests/hl_parse_test
hl_parse_test: PASS viewport-first, snapshot lifetime, incremental edit, typing guard
== build/san/tests/hl_worker_test
hl_worker_test: PASS slice resume, cancellation, restart, failure, pool ordering, empty/faulted parse
check: 3 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
== tools/test_display_lock.sh contract
waiting for display :99 (held by unknown)
sh: display lock timeout: display :99 (held by unknown)
make: *** [Makefile:146: check] Error 1
```

The headless mock-Wayland and mock-Wayland GL suites pass both in the inventory
run and in authorized runs with local sockets available. No native GL hardware
coverage is claimed.

## What remains

The grammar implementation, pruning, capture inventory and size evidence for
this bead are complete. Whole-repository/non-display acceptance is **not all
green** because of the unrelated fixture failures listed above. Coordinator
reruns/triage are needed for those failures, the display suites and occupied
lock contract, and leaks-enabled checks outside tracing. Julia's combined-size
miss remains explicit, with the optional binary target available; future default
grammar registration must preserve optional Julia linking. The shared query
layer and C/Rust integration belong to the parallel worker; no changes were
made to them. No work beyond this bead was implemented.
