# edit-v0o.3 — worker s6 report

Implemented this bead's C/Rust grammar wrappers, archive rules, embedded
highlight queries, worker-only query initialization, row-range querying,
capture/theme mapping, pointer-free scope mailbox serialization and tests.
Dark/light syntax colours come from installed Sublime Mariana/Breakers.
No editor/paint integration was added. One bead only; no `bd` or `git` ran.
All work is in the assigned worktree (test temporaries use the permitted
system temporary directory). Forbidden coordinator documents were not edited.

The code builds and bead tests are green. **Full check and shared size
acceptance are not achieved**: sandbox/display restrictions and existing
non-display failures remain, and the grammar contribution exceeds its equal
share of the supplied total target. Details below are intentionally explicit.

Version, toolchain, bead, task, file-line and protocol identifiers are not
performance quantities. Measurements and output counts below are (M)[AC]:
BAT0 read `Not charging` during verification and size measurement. Counts are
computed from named fixtures, logged suite exits or fixed-width ABI fields;
no new duration/sample/memory budgets were invented. Parallel build concurrency
was derived from `getconf _NPROCESSORS_ONLN`; suites were run serially.

## Files touched

New:

- `src/hl/lang.h`
- `src/hl/lang_c.c`
- `src/hl/lang_rust.c`
- `src/hl/query.h`
- `src/hl/query.c`
- `src/hl/query_c.inc`
- `src/hl/query_rust.inc`
- `tools/hl_embed.py`
- `docs/decisions/edit-v0o.3.md`
- `docs/worker-reports/edit-v0o.3-s6.md`

Modified:

- `Makefile`
- `src/config/theme.c`
- `tests/hl_test.c`
- `fuzz/hl_fuzz.c`
- `src/hl/STATUS.md`
- `vendor/tree-sitter-rust/src/scanner.c`

Deleted, as requested (not used at build time):

- `vendor/tree-sitter-c/src/grammar.json`
- `vendor/tree-sitter-c/src/node-types.json`
- `vendor/tree-sitter-c/queries/tags.scm`
- `vendor/tree-sitter-rust/src/grammar.json`
- `vendor/tree-sitter-rust/src/node-types.json`
- `vendor/tree-sitter-rust/queries/tags.scm`
- `vendor/tree-sitter-rust/queries/injections.scm`

C had no `injections.scm`. Vendored highlights, licenses, provenance and
parser headers remain. The Rust scanner received only an explicit `(void)`
parameter list plus an explanatory comment; its released `create()` form
tripped Clang UBSan's indirect function-type check against the runtime callback.
The local scanner SHA-256 is provenance, not a timing/size claim:
`733fc1fff711a0d8e791de820ea80e324d7b3abbc442770bc28cec88ddcf5440`.

## Red run, before implementation

The tests were written before the language/query API existed. Command:
`make build/tests/hl_test`; make failed (exit status indicates failure).

```text
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -DEDIT_HAVE_WAYLAND -DEDIT_HAVE_WAYLAND_SERVER -include tests/display_guard.h -MMD -MP -c tests/hl_test.c -o build/rel/tests/hl_test.o
tests/hl_test.c:4:10: fatal error: hl/lang.h: No such file or directory
    4 | #include "hl/lang.h"
      |          ^~~~~~~~~~~
compilation terminated.
make: *** [Makefile:81: build/rel/tests/hl_test.o] Error 1
```

The first implemented test fixture treated a Rust `Some(value)` call as a
constructor scope; the vendored query's later function capture correctly wins.
The fixture was corrected to a standalone `Some` identifier, so the intended
constructor-predicate expectation is exercised without contradicting upstream
precedence. `value` still checks that lowercase identifiers fail that predicate.

A separate sanitizer red run identified the vendored scanner signature issue.
Command: `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
build/san/tests/hl_test`; it failed before the signature clarification:

```text
vendor/tree-sitter/lib/src/./parser.c:369:40: runtime error: call to function tree_sitter_rust_external_scanner_create through pointer to incorrect function type 'void *(*)(void)'
/home/tobias/Projects/sublimite/.wt/edit-v0o.3-s6/vendor/tree-sitter-rust/src/scanner.c:24: note: tree_sitter_rust_external_scanner_create defined here
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior vendor/tree-sitter/lib/src/./parser.c:369:40
```

## Green runs

`make all` completed successfully using GCC release flags. After the final test
changes, `make build/san/tests/hl_test` completed successfully using Clang
ASan/UBSan flags. Production `src/`, tests and fuzz code retain
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`.

`build/tests/hl_test`:

```text
hl_test: PASS C/Rust parse, queries, predicates, scopes, row range, pointer-free spans, worker publication/completion, themes
hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard
```

`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/hl_test`:

```text
hl_test: PASS C/Rust parse, queries, predicates, scopes, row range, pointer-free spans, worker publication/completion, themes
hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard
```

LeakSanitizer is disabled only because the sandbox denies its thread inspection.
Address/undefined/function sanitizers remain enabled, and UBSan stops at the
first error. This is not a leak-check pass; the coordinator must rerun leaks
outside this restriction. Tests parse both production grammars, compile their
actual queries, assert every supplied capture maps to a theme scope, check known
tokens and predicates, reject unsupported predicates and invalid query nodes,
exercise row filtering/default foreground and malformed wire scopes, guard
allocation-free span encode/decode, and verify real worker-side initialization,
ordered scope publication and completion through the existing pool/mailbox.

`make build/fuzz/hl_fuzz` built the new query-over-random-edits operation.
The operation compares per-byte scopes from incremental and fresh trees after
input-derived edits, preserving the original malformed mailbox operation.
A fixed-input smoke run with both grammars, multiline comments, raw strings,
UTF-8/malformed input and empty content passed under ASan/UBSan. Fixture count
is derived from those named seed files; no randomized fuzz campaign is claimed.
The fuzzer explicitly printed that it executed fixed inputs only.

`build/bench/hl_bench` ran and returned successfully with:

```text
hl_bench: SKIP unknown refresh; pass --refresh-ns=MEASURED_INTERVAL
```

No refresh interval was invented, and no endpoint latency claim is made.
The coordinator can run the existing busy-parser benchmark with the assigned
display's measured interval during successor integration.

## Repository check accounting and outside-scope findings

Unmodified `make check` built the sanitizer suites, then stopped at the
existing base suite because LeakSanitizer could not inspect sandboxed threads:

```text
clang -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -pthread build/san/tests/x11_xi2_test.o build/san/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/san/tests/x11_xi2_test
== build/san/tests/base_test
==645==LeakSanitizer has encountered a fatal error.
==645==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==645==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:138: check] Error 1
```

With `ASAN_OPTIONS=detect_leaks=0`, the Makefile-derived non-display invocation
stopped at `file_kill_test`, which contains real viewport/display sections.
I then ran each name in the Makefile's non-display list independently with
UBSan halt-on-error, to account for later suites. (M)[AC] The log tally is
54 passing + 4 failing = 58 suites,
derived from `build/hl-evidence/non-display-results.tsv`, not an assumed count.
The highlight parse/worker suites, config suites, and all other passing names
are listed with exact exits there. All suite output is retained in
`build/hl-evidence/<suite>.log`.

Failures, reported without modifying their modules:

| Suite | Evidence / remaining action |
| --- | --- |
| file_kill_test | viewport initialization failed at its existing display-dependent sections; coordinator reruns with Xvfb |
| ipc_test | sandbox socket setup returned Operation not permitted; full rerun with local socket access reached existing `main_launcher_primary` and failed `accepted` at its source assertion; coordinator reruns/diagnoses full launcher integration |
| journal_kill_test | kill-before-pump replay returned JOURNAL_CALLBACK; reproduced on a serial rerun; outside this grammar bead |
| lineidx_test | existing combined-ownership section could not reach file-ready/MMAP state; reproduced on a serial rerun; outside this grammar bead |

Representative diagnostics (source lines, trial and status values are measured
raw test output, not application budgets):

```text
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
tests/lineidx_test.c:339: FATAL file_open_ready(f) && file_open_mode(f) == FILE_MODE_MMAP
```

Named non-display IPC sections were then run with allowed local Unix socket
access, omitting the main/astra/CLI launch sections. (M)[AC] All
23 named sections passed, derived from
`build/hl-evidence/ipc-core-results.tsv`. The display-named IPC section only
checks display-specific endpoint namespaces, so it was included; it does not
open an X window. The complete IPC suite is still not claimed green.
`tools/test_replay_cli.sh build` passed with the same sanitizer environment.

The actual X socket probe returned:

```text
X socket unavailable in sandbox: [Errno 1] Operation not permitted
```

The representative display-suite attempt through `tools/with_display_lock.sh`
was blocked by the occupied shared display lock and interrupted. The display
suites cannot pass inside this sandbox without access to the X socket; no
shared-display lock was bypassed and no window was opened on the real display.
The coordinator must rerun them on Xvfb `:99`, including mixed file/IPC suites,
and run the display-lock shell contract there. I did not run every locked
display binary independently after that confirmed environment restriction.

## Every Makefile edit

Language additions are exactly one source-list line per language. Shared
header additions are exactly one declaration line per language, besides the
new header's guard/include. No Python/Julia lines were added or changed.
These are all new Makefile lines for the language source list:

```make
# Generated grammar sources share the runtime archives; one line per language.
TS_SRC += vendor/tree-sitter-c/src/parser.c
TS_SRC += vendor/tree-sitter-rust/src/parser.c vendor/tree-sitter-rust/src/scanner.c
```

The runtime-only object expression was replaced:

```make
TS_OBJ := $(addprefix $(B)/,$(addsuffix /$(TS_SRC:.c=.o),rel san fuzz))
```

with a source-list-safe expression, and the vendored grammar warning rule was
added. Every new/replaced line in that block is shown:

```make
TS_OBJ := $(foreach config,rel san fuzz,$(addprefix $(B)/$(config)/,$(TS_SRC:.c=.o)))
# Generated parsers also have unused labels/parameters; handwritten scanners
# use the same vendored-only flags. Keep project src/ warnings unchanged.
GRAMMAR_OBJ := $(filter %/parser.o %/scanner.o,$(TS_OBJ))
$(GRAMMAR_OBJ): WARN := -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-label -D_GNU_SOURCE -pthread
```

The existing runtime WARN assignment between these lines remains unchanged.
Grammar objects omit conversion/shadow/error warnings and suppress generated
unused labels/parameters; configuration optimization/debug/sanitizer flags
remain. The Rust scanner's signed lexer input meets unsigned wide-character
functions upstream, which also requires relaxing conversion warnings.
These changes affect vendored grammar objects only. Runtime warning policy
and project source warnings remain as before. No other Makefile line changed.

## Size and remaining acceptance

(M)[AC], BAT0 `Not charging`, `size` of the release archive-member objects:

```text
   text	   data	    bss	    dec	    hex	filename
 602104	   3520	      0	 605624	  93db8	build/rel/vendor/tree-sitter-c/src/parser.o
1109089	   3384	      0	1112473	 10f999	build/rel/vendor/tree-sitter-rust/src/parser.o
   1911	      0	      0	   1911	    777	build/rel/vendor/tree-sitter-rust/src/scanner.o
     89	      0	      0	     89	     59	build/rel/src/hl/lang_c.o
     89	      0	      0	     89	     59	build/rel/src/hl/lang_rust.o
   8758	    512	      0	   9270	   2436	build/rel/src/hl/query.o
 181883	     32	      0	 181915	  2c69b	build/rel/vendor/tree-sitter/lib/src/lib.o
1350673	 141144	17574660	19066477	122ee6d	build/sublimite
```

(M)[AC] Grammar bytes are `605624 + 1112473 + 1911 = 1720008`.
Wrappers/query add `89 + 89 + 9270`, yielding `1729456`.
Compiling reconstructed entry-time syntax palette arrays with the same release
flags gives the before/after theme object totals below; the palette delta is
`1656 - 1364 = 292`, so this bead contributes `1729456 + 292 = 1729748`
archive text/data bytes, excluding debug information and the prior runtime:

```text
   text	   data	    bss	    dec	    hex	filename
    660	    704	      0	   1364	    554	build/hl-evidence/theme-before.o
    760	    896	      0	   1656	    678	build/rel/src/config/theme.o
```

The requested P6.3 target is **binary <= 2 MB total** (E: supplied planning
bound), shared among runtime and C/Rust/Python/Julia. A language pair's equal
share before any runtime/editor allowance is (E) `2/4 * 2000000 = 1000000`
bytes. These grammars exceed it, also under a binary-megabyte interpretation.
`size` includes rodata in text; the new grammar/query objects have no bss.
The current editor omits all grammar/runtime/query symbols through static
archive member selection because production highlighting is not wired yet.
Its apparent current size therefore cannot establish integrated acceptance.
(M)[AC] Current release disk size is `5131656` bytes with debug info and
`1584984` for a separate no-debug copy; the actual release file was not stripped.

Decision and arithmetic are in `docs/decisions/edit-v0o.3.md`. Proposed follow-up:
choose opt-in grammar builds/packages or revise the total target after a complete
linked measurement; if all grammars must fit, investigate reduced generated
tables and verify conformance. Removing debug sections does not remove the
runtime-required tables. Nothing was silently stripped or omitted here.

Remaining: coordinator display/leak/full-suite validation, diagnosis of the
reported existing journal/lineidx/IPC failures, and an explicit shared-size
acceptance decision. Production parse/query publication and editor paint belong
to the assigned successor bead. `src/hl/STATUS.md` describes the available
layer and these integration/validation limits. Stop after this bead.
