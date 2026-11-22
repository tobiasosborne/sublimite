# Wave 0 scaffold — adversarial review (lane M1, Muse)

Reviewer: Muse Code powered by muse-wonder-full-1.0 (T3 Code, review sub-agent).
Targets: `Makefile`, `mk/layers.mk`, `mk/grammars.mk`, `tools/check_dag.py`,
`tools/check_no_c_includes.py`, `tools/check_isolation.py`, `tools/clex.py`,
`tools/README.md`. Governing text: `CLAUDE.md` (laws + "v2 mechanical
anti-decay rules"), `docs/PLAN_V2.md` §2 and §5.

Method: every hole below was built as a concrete counter-example in a scratch
copy outside the tree (`/tmp/m1-scratch-*`, a `cp -r` of the repo) and the
actual linter / `make` output recorded verbatim. Nothing in the real tree was
modified except this file. Law 4: no invented numbers — counts below are
counts of probes actually run in this session (M, measured by running them);
no budgets, thresholds or durations are proposed.

Severity per CLAUDE.md Law 8 triage (would it let real decay through into
trial use?): **blocks Wave 1** / **should fix** / **nit**.

## Baseline (scratch, clean tree)

`make check-lints` and `make check` pass on the pristine scaffold
(`check_dag: ok (13 files, 6 cross-module includes, 35 modules in 6 layers)`,
`check-san: 5 test binaries passed`, `check: ok`). Note: `vendor/` (13
tree-sitter grammars) landed in the real tree from another lane while this
review ran; the frozen scratch snapshot under `/tmp/m1/base` includes it, so
the baseline above exercised `mk/grammars.mk` in its enabled state and it
built green. Two baseline observations become findings below: the empty
Law-2 lane inside that green `make check` (F12) and the dynamically
linked binary (F14).

## Findings

### F1 — Angle-bracket includes are invisible to `check_dag` — **blocks Wave 1**

`check_dag.py:55` matches only quoted includes (`QUOTED`). With `-Isrc`
(`Makefile:28`), `<x11/x11.h>` resolves to the same file as `"x11/x11.h"`, so
every DAG rule (inter-layer, intra-layer, L5-forbidden) has a one-character
escape hatch. This is also the *plausible accidental* form: harvested v1 code
or an editor's auto-import can produce angle includes without any intent to
evade.

Counter-example (probe P03, scratch `src/app/app.c`, with a stub
`src/x11/x11.h` added):

```c
#include <x11/x11.h>
size_t app_probe_angle(void){ return sizeof(struct x11_cfg); }
```

Actual output: `check_dag: ok (15 files, 6 cross-module includes, ...)`
(exit 0) and the file compiles clean under the full `WARN` set. Compare P05:
the quoted form `"../x11/x11.h"` is correctly rejected as `[forbidden]`.

Fix (`tools/check_dag.py`, `QUOTED` regex + `main` loop): match angle
includes too and run them through the same `resolve()` + rule checks.
Angle includes that resolve under `src/` (or whose first path component is a
known module, per the existing not-yet-written fallback at line 129) are
edges; genuine system headers (`<stddef.h>`) resolve to `None`/unknown and
stay ignored. Cheapest correct shape: reuse `clex` `header` tokens plus the
quoted regex, i.e. one include regex
`^[ \t]*#[ \t]*(?:include|include_next|import)[ \t]*[<"]([^>"\n]+)[>"]`
(exactly what `check_no_c_includes.py:16` already does), then filter in
`resolve()`.

Regression fixture: `src/app/app.c` += `#include <x11/x11.h>` (with
`src/x11/x11.h` present) → expect
`error: [forbidden] app (L5) includes ...`, exit 1. Positive control:
`#include <stddef.h>` in the same file → still ok.

### F2 — Macro (computed) includes are invisible to all three linters — **blocks Wave 1**

`clex.py` states it does not expand macros (line 6), and every include regex
requires a literal `"..."`/`<...>` after `include`. A computed include is
therefore unseen by `check_dag` and `check_no_c_includes` alike.

Counter-example (probe P04, scratch `src/app/app.c`):

```c
#define H "x11/x11.h"
#include H
size_t app_probe_macro(void){ return sizeof(struct x11_cfg); }
```

Actual output: `check_dag: ok ...` (exit 0), compiles clean. The same shape
in `tests/` with `#define C "../src/os/os.c"` defeats
`check_no_c_includes.py` (probe P21a, §F7).

Fix: do not try to evaluate macros in the linters — ban the form. In
`tools/check_dag.py` and `tools/check_no_c_includes.py`, after
`clex.blank_comments`, flag any line matching
`^[ \t]*#[ \t]*(?:include|include_next|import)[ \t]+[^<"\s]` (an include
whose operand is neither `"` nor `<`) as an error: "computed #include:
spell the header literally". No legitimate file in this codebase needs a
computed include, and the ban is self-documenting. (Vendored tree-sitter is
outside `src/`/`tests/`/`fuzz/`, so it is unaffected.)

Regression fixture: the P04 snippet above in `src/app/app.c` → expect exit 1
from `check_dag`; the P21a snippet in `tests/probe_test.c` → expect exit 1
from `check_no_c_includes`.

### F3 — Translation-phase-2/3 spellings defeat the line-based include regexes — **should fix**

The include regexes run per physical line on comment-blanked text, but the
compiler splices lines (phase 2) and replaces each comment with one space
(phase 3) *before* directives are parsed, and accepts digraphs and all C
whitespace. Each form below compiled clean under the full `WARN` set and
passed `check_dag: ok` (probes P06a/b/c, P07, P08, P10; scratch
`src/app/app.c` including forbidden `"x11/x11.h"` each time):

```c
#include \
"x11/x11.h"              /* P06a splice before operand */
#include "x11/x1\
1.h"                     /* P06b splice inside the filename */
#\
include "x11/x11.h"      /* P06c splice after # */
#include /* multi
line */ "x11/x11.h"      /* P07 comment containing a newline */
%:include "x11/x11.h"    /* P08 digraph for # */
#<VT>include "x11/x11.h" /* P10 vertical tab (regex allows only space/tab) */
```

Individually these are spellings nobody writes by accident, which is why
this is *should fix* rather than a Wave 1 blocker; collectively they mean
the regex is not parsing the same language the compiler is, and P07 in
particular can arise from an innocent multi-line comment. The same gaps
exist in `check_no_c_includes.py:16` and in `check_isolation` adjacency
(P20h, §F6).

Fix (one place, `tools/clex.py`): normalize the text before any regex runs,
preserving line numbers. Add `clex.desplice(text)` that replaces every
backslash-newline with a single space *and* returns the text with an
accompanying newline-offset table taken from the original, or simpler:
join spliced lines and have `line_mapper` map offsets through the recorded
splice points. Additionally treat a comment token as a single space
(instead of space-filling with newlines kept) in a new
`clex.blank_comments_one_space` used by the include matchers, translate a
line-leading `%:` to `#`, and widen the regex whitespace class from
`[ \t]` to `[ \t\v\f]`. Apply in both include linters; `check_isolation`
needs only the splice part (treat a code token containing only whitespace
and splices as whitespace in `literals()`).

Regression fixtures: each P06/P07/P08/P10 snippet above in `src/app/app.c`
→ expect `[forbidden]` exit 1 after the fix.

### F4 — No-include `extern` coupling: intra-layer orders and the L5-forbidden rule have no link backstop — **blocks Wave 1**

The `--whole-archive` isolated link (`mk/layers.mk:65-71`) enforces only
*inter-layer* symbol references. Inside one archive, and for L5→L3 where L3
is a legitimate lower archive, a hand-written `extern` declaration (never
including the other module's header) satisfies both the compiler and the
linker, and `check_dag` sees no include edge. Both probes below passed
`check_dag: ok`, `check-layers: 6 isolated layer links passed`, and (P14b)
full `make check: ok`:

```c
/* P14a, scratch src/x11/x11.c — x11 and wayland are unrelated in L3 */
extern size_t wayland_smoke(void);
size_t x11_probe_wl(void) { return wayland_smoke(); }

/* P14b, scratch src/app/app.c — FORBIDDEN pair, include-free */
extern size_t x11_smoke(void);
size_t app_probe_x11(void) { return x11_smoke(); }
```

This is the exact v1 failure class (`x11/clip.c` ↔ `wayland/dispatch.c`)
one level down: the coupling that the private.h cycle expressed as
includes can be re-expressed as declarations, e.g. during harvest when
"just declare it extern" is the path of least resistance. Wave 1 lands the
L1/L2 intra-layer orders (`piece < undo/lineidx`, `journal < edit`), so
this is not Wave-2-only.

Fix (`tools/check_dag.py` or a new `tools/check_symbols.py` in
`check-lints`, plus `mk/layers.mk` plumbing of the rel archives): audit
symbols, not just includes. For each rel object, collect its undefined
symbols (`nm -u`), map each to the defining module via the archive symbol
tables (`nm -A` over `build/rel/lib*.a`), and apply the same three rules
(inter-layer, `INTRA_RULES`, `FORBIDDEN`). The `module_` symbol-prefix
convention (CLAUDE.md, Code conventions) makes the table buildable even
for symbols whose definition is not yet linked. Keep the include check —
it catches type-level coupling that symbol audit cannot (F6).

Regression fixtures: the two snippets above → expect exit 1 naming the
undefined symbol, the defining module, and the violated rule; the clean
scaffold → ok.

### F5 — Weak undefined symbols defeat the inter-layer isolated link — **should fix**

Probe P13 (scratch `src/os/os.c`):

```c
__attribute__((weak)) extern size_t app_smoke(void);
size_t os_probe_weak(void) { return app_smoke ? app_smoke() : 777; }
```

Actual output: all linters ok, `check-layers: 6 isolated layer links
passed`, `make all` exit 0. `nm build/rel/libcore.a` shows `w app_smoke`
(weak undefined in L0), `nm build/sublimite-v2` shows `T app_smoke` — the
shipped binary binds L0→L5 at run time while every gate is green. A plain
non-weak upward `extern` *is* caught (P12, see rejected list); weak is the
hole. The same escape exists via `dlsym(RTLD_DEFAULT, ...)` on an internal
name; that one cannot be banned outright (GL loads via `dlopen` per
PLAN §2), so it is recorded here as a known residual for code review, not
counted as a separate hole.

Fix: in the F4 symbol audit, treat weak undefined symbols (`nm` type
`w`/`W` undefined) exactly like strong undefined ones; until the audit
lands, the cheap interim form is an `nm` scan in `check-layers`
(`Makefile` target body or `mk/layers.mk` link-check recipe) that fails on
any weak undefined symbol in a layer archive that is defined in a
non-lower archive.

Regression fixture: the P13 snippet in `src/os/os.c` → expect
`check-layers` (or the audit) exit 1; positive control: a weak reference
to a *lower*-layer symbol stays legal.

### F6 — Transitively included files outside the scanned set are invisible — **should fix**

All three linters scan only files whose names end `.c`/`.h`/`.inc`, and
`check_dag`/`check_isolation` only under `src/`. Any file outside that set
can carry an upward include, and the include of the shim itself resolves to
the includer's own module (skipped at `check_dag.py:168`). Four variants
each passed all linters *and* full `make check: ok` (probes P17, P17b,
P18, P19; each makes L0 `os.c` use `struct app_cfg` from L5 `app.h`, a
type-level coupling no link can see):

```c
/* P17:  src/os/up.def (extension not scanned) contains:
           #include "app/app.h"
   src/os/os.c:  #include "up.def"  + sizeof(struct app_cfg) */
/* P17b: same with src/os/up.H (capital extension not scanned)  */
/* P18:  evil.h at repo root includes "src/app/app.h";
   os.c:  #include "../../evil.h"   (resolves outside src/ → None) */
/* P19:  ln -s ../app src/os/up;  os.c: #include "up/app.h"
           (lexical path → module "os", self-include, skipped) */
```

Fix (`tools/check_dag.py`): (a) scan includer files by content reachability,
not extension: walk `src/` for *all* regular files (skip only binary
payloads) and treat any file containing an include directive as an
includer, attributing it to its first path component under `src/`; (b) in
`resolve()`, `os.path.realpath` the candidate before `module_of`, and
report an include whose real path escapes `src/` while its target is under
a different module via symlink (P19) — simplest is to reject symlink
components under `src/` outright (`os.path.islink` on each path component
in the `src/` walk, in `main`); (c) an include from `src/` whose resolved
file is *outside* `src/` (other than system/vendor include dirs) is an
error: production code must not include repo-root or `../` escapees (P18).
Apply (a) to `check_isolation.py` and `check_no_c_includes.py` `EXTS` too.

Regression fixtures: each of P17/P17b/P18/P19 → expect `check_dag` exit 1;
a same-module `.def` including a same-module header stays ok.

### F7 — `src/<m>/LDLIBS` is appended unvalidated to the isolated layer links — **should fix**

`Makefile:50` folds every `src/*/LDLIBS` file into `$(LDLIBS)`, and
`mk/layers.mk:68` puts `$(LDLIBS)` on the isolated link line. A module can
therefore add the upper archives to its own isolation check. Probe P15fix
(scratch): `os.c` with the P12 upward `extern size_t app_smoke(void);`
call, plus `src/os/LDLIBS` containing

```
-Wl,--start-group build/rel/libapp.a build/rel/libui.a build/rel/libgfx_plat.a build/rel/libpersist.a build/rel/libtext.a -Wl,--end-group
```

Actual output: `build/rel/tests/layer_L0_test` links and runs (exit 0);
the control with `LDLIBS` removed fails with
`undefined reference to 'app_smoke'`. No linter reads `LDLIBS` files.
(The same channel also injects the *rel* archives into san/tsan/fuzz
links, since `LDLIBS` is configuration-independent.)

Fix (`mk/layers.mk` + `Makefile`): the isolated links must not consume
the global `LDLIBS`. Split it: `LDLIBS_SYS` (only `-l`/`-L` tokens,
validated) for layer links, full `LDLIBS` only for the final binary and
regular tests. Validation where the files are read (`Makefile:50`):
reject any token that is not `-l*`/`-L*` (a one-line `$(foreach)` check or
a `tools/check_ldlibs.py` in `check-lints`); archive paths, `-Wl,` flags
and shell metacharacters in a per-module `LDLIBS` become errors.

Regression fixture: P15fix as above → expect the L0 isolated link to fail
with the undefined reference (or the `LDLIBS` validation to reject the
file); `src/x11/LDLIBS` containing `-lxcb -lxkbcommon` stays ok.

### F8 — `check_dag.read_layers` and `make` can disagree about `mk/layers.mk` — **should fix**

`read_layers` (`tools/check_dag.py:69`) parses only `^L<n>_MODS\s*:=`
lines; `make` also honors `+=` and `=`. Probe P16fix: inserting
`L0_MODS += piece` directly after the `L0_MODS :=` line leaves
`check_dag: ok` (piece still L1 to the parser) while `ar t` shows
`piece.o` in **both** `libcore.a` and `libtext.a`, and `check-layers`
passes — the "module listed in two layers" config error at
`check_dag.py:75` is bypassed, and the docstring claim that layers.mk is
the single source of truth becomes false. (The naive end-of-file append
is inert — `*_SRCS` was already `:=`-computed at `mk/layers.mk:36` — which
is itself a trap: the same edit does nothing in one position and silently
duplicates a module in another.)

Fix (`tools/check_dag.py`, `read_layers`): fail with exit 2 on *any* line
matching `^L\d+_MODS\s*(\+?=)` that is not the single `:=` form, on a
second `:=` for the same layer, and on a `\`-continued `L<n>_MODS` line
(the parser is line-based; make joins continuations). That makes the
makefile fragment's grammar exactly the one the comment in
`mk/layers.mk:1-3` already mandates, mechanically enforced.

Regression fixtures: `L0_MODS += piece` after line 9 → exit 2; a
continued `L1_MODS := scan \` line → exit 2; pristine file → ok.

### F9 — `check_isolation` needle set misses the natural spellings of both leaf kinds — **blocks Wave 1**

`check_isolation.py:19,41-47` rejects a literal only if it contains
`/sublimite`, `sublimite.` or `sublimite-`, or *equals* `sublimite` — and
it compares the **raw, undecoded** literal body (`literals()` never
processes backslash escapes). Each line below was appended alone to
scratch `src/os/os.c` and passed with `check_isolation: ok` (exit 0),
while the controls `"sublimite"`, `"/home/x/.config/sublimite/config.toml"`,
`"sub" "limite"` and `"sublimite-v2"` were correctly rejected (exit 1):

```c
const char *a  = "sublim\x69te";          /* P20a escape mid-word        */
const char *a2 = "\x2fsublimite";         /* P20a2 escaped slash         */
const char *b  = "sublimite/config.toml"; /* P20b slash AFTER the leaf   */
const char *c  = "\0sublimite";           /* P20c abstract socket leaf   */
const char *d  = "sublimite_sock";        /* P20d underscore suffix      */
const char *d2 = "org.sublimite";         /* P20d2 dot BEFORE the leaf   */
const char *h  = "sublim" \
"ite";                                    /* P20h adjacency via splice   */
```

P20b and P20c are not adversarial spellings: a relative XDG leaf
(`"sublimite/…"`) and an abstract-socket name (`"\0sublimite"`, cf.
PLAN §1: *abstract + filesystem socket leaf names*) are precisely the two
forms the Wave 1 `os`/`ipc` harvest will type if it hardcodes anything.
The linter as written would wave both through, and v2 would open v1's
config or forward into the running daily editor — the failure this lane
exists to prevent.

Fix (`tools/check_isolation.py`): (a) decode each literal before testing —
add a `decode(body)` next to `literals()` handling `\xHH`, octal
(`\0` included), `\u`/`\U` and the simple escapes via a small state machine
over the raw body; (b) replace the needle list with a single rule: any
decoded literal containing `sublimite` anywhere is an error (all identity
must come from `EDIT_APP_ID`; even a correct `sublimite-v2` literal is
already rejected today via `sublimite-`, so this loses no legitimate use —
prose occurrences belong in comments, which are already ignored); (c) for
P20h, treat a code token consisting only of whitespace and backslash-newline
splices as whitespace in `literals()` (the F3 desplice covers this).

Regression fixtures: each P20a/a2/b/c/d/d2/h line above in `src/os/os.c` →
exit 1; `const char *ok = EDIT_APP_ID "/config.toml";` → stays ok
(concatenation with the macro is the sanctioned form).

### F10 — Isolation is literal-only and `APP_ID` itself is unguarded — **should fix**

Three compositional spellings pass `check_isolation: ok` (scratch
`src/os/os.c`), as expected of a literal scanner — recorded so the
boundary is explicit rather than assumed:

```c
const char *e = STR(sublimite); /* #define STR_(x) #x ... STR(x) STR_(x) */
const char f[] = {'s','u','b','l','i','m','i','t','e','\0'};
snprintf(buf, n, "%s%s", "sub", "limite");       /* pieces as args */
```

More consequential is P28: `make APP_ID=sublimite build/rel/src/os/os.o`
compiles with `-DEDIT_APP_ID=\"sublimite\"` (`strings` on the object shows
`sublimite`) and `check_isolation` still reports ok — it never reads the
Makefile. The entire isolation property rests on one `:=` at
`Makefile:26` that any command line or stray edit can retarget, including
the binary name (`$(B)/$(APP_ID)`). `tools/README.md` meanwhile claims
the tool "grep-audits … `OS_APP_DATA`, `OS_APP_CONFIG`,
`config/runtime.c`, `ipc_posix.c`": none of those files exist yet and the
script contains no such site-specific (positive) audit — the registry
over-promises what is a purely negative check.

Fix: (a) `Makefile`, right after line 26:
`ifneq ($(APP_ID),sublimite-v2)` → `$(error APP_ID must stay sublimite-v2)`,
plus the same assertion in `check_isolation.main` (parse the `APP_ID :=`
line); (b) in `check_isolation`, also flag the bare *identifier*
`sublimite` in code tokens (kills P20e stringification; no legitimate
identifier is spelled exactly that — module symbols are `module_`-prefixed);
(c) correct the `tools/README.md` row to describe the actual negative
check, and add the positive audit (each of the four sites references
`EDIT_APP_ID`) when those files land. P20f/P20g (char arrays, `snprintf`
pieces) are accepted residuals for code review: flagging them statically
without false positives is not proportionate, but the F9 fix means the
pieces can no longer be completed into a full literal anywhere.

Regression fixtures: P20e snippet → exit 1; `make APP_ID=sublimite all` →
make error before compiling; pristine tree → ok.

### F11 — `#include "*.c"` returns through shims, symlinks and unscanned dirs — **should fix**

`check_no_c_includes` checks only whether an include *operand string* in a
scanned (`.c`/`.h`/`.inc`, under `tests/`+`fuzz/`) file ends in `.c`. The
direct form and the `.C` uppercase form are caught (exit 1); all of these
passed `check_no_c_includes: ok` (probes P21a–d), and P21c's test binary
built and ran exit 0 with `os.c` textually inside it:

```c
/* P21a */ #define C "../src/os/os.c"
           #include C                      /* (F2 fix bans this form) */
/* P21b */ /* tests/shim.def: */ #include "../src/os/os.c"
           /* test: */ #include "shim.def"
/* P21c */ /* ln -s ../src/os/os.c tests/os_impl.h */
           #include "os_impl.h"
/* P21d */ /* bench/shim.h — bench/ is not in DIRS — same .c include */
           #include "../bench/shim.h"
```

Fix (`tools/check_no_c_includes.py`): judge the *resolved file*, not the
spelling. Resolve each include operand the way `check_dag.resolve` does
(includer dir, then `-Isrc`), `os.path.realpath` it (P21c), and fail if
the real path ends `.c`, or — stronger and simpler — if the real path is a
file under `src/` that is a compilation unit (member of any `L<n>_SRCS`
per `mk/layers.mk`). Scan includers regardless of extension (F6a) in
`tests/`+`fuzz/`, and follow the shim one level by applying the same
resolved-target test to includes inside any file a test includes from
outside `src/` (P21d) — or equivalently also scan `bench/` and any other
repo dir a test can include from. Add the F2 computed-include ban here
for P21a.

Regression fixtures: P21a–d each → exit 1; a test including
`"os/os.h"` → ok.

### F12 — Fail-closed holes: suites are glob-discovered, and three lanes pass empty — **blocks Wave 1 (the Law-2 clause)**

CLAUDE.md: "A lane that silently passes on a missing mock, inert guard,
or skipped suite is a bug." Measured against that:

- **P22a**: a failing test at `tests/sub/fail_test.c` (`return 1`) → full
  `make check: ok`. `TESTS := $(wildcard tests/*_test.c)`
  (`Makefile:57`) is top-level only, so any suite in a subdirectory —
  including the planned `tests/contract/` (HANDOFF Wave 0 item 4) — is
  silently never built or run.
- **P22b**: a failing `tests/probe_test.c` correctly fails `check-san`;
  renaming it `probe_test.c.off` makes `make check` green again with no
  diagnostic. There is no manifest of expected suites anywhere: deleting
  or misnaming a suite is indistinguishable from it never having existed.
- **P22c (nuance)**: deleting `tests/layer_L2_test.c` fails loudly from a
  clean tree (`No rule to make target 'build/rel/tests/layer_L2_test.o'`,
  exit 2 — the fixed `LAYER_TEST_IDS` list is the one manifest that
  exists), but in a dirty tree the stale prebuilt binary is simply re-run
  and `check-layers` reports 6 passed. Normal dev trees are dirty.
- **P22e / P01**: `check-law2`, `check-tsan` and `fuzz-smoke` each print
  `NOTE … nothing ran` and exit 0 (`Makefile:147-165`), and `check-law2`
  is *inside* `make check`. HANDOFF:14 declares this temporary, but the
  trigger to flip is manual and unowned by any gate: a Wave 1 bead can
  close on a green `make check` whose Law-2/G1 lane — the single enforced
  perf guard (Law 1) and the live malloc guard (Law 2) — ran nothing,
  e.g. because its suite landed as `tests/law2/law2_x_test.c` (P22a) or
  `tests/g1_test.c` (misses both `law2_%` and `g1_%` filters at
  `Makefile:61` while still "running" in the san lane, where the release
  guard requirement does not apply).

Fix (`Makefile`): (a) discover suites recursively —
`$(sort $(wildcard tests/*_test.c tests/*/*_test.c))` at minimum, or
`$(shell find tests -name '*_test.c')` — and mirror the `LAW2_TESTS` /
`TSAN_TESTS` filters onto the recursive list; (b) the moment the first
`law2_*`/`g1_*` suite lands (same bead), flip the `check-law2` NOTE to a
hard `FAIL … exit 1` on `n == 0`, and likewise `fuzz-smoke` when the
first fuzzer lands (its per-fuzzer corpus check already fails closed —
P22d, rejected list); `check-tsan` flipping can wait for its first
threaded suite since it is a wave-end gate outside `make check`;
(c) make suite disappearance loud in dirty trees: have `check-san` /
`check-layers` verify each test binary's source still exists
(the binary list is glob-derived, so this needs the manifest in (d));
(d) cheapest manifest: a committed `tests/MANIFEST` (one suite path per
line) checked by a `check-lints` step — glob result must equal the
manifest, so add/rename/delete of a suite is a reviewed diff. (b)+(d)
together close P22b/P22c-dirty.

Regression fixtures: P22a's `tests/sub/fail_test.c` → `make check` exit
nonzero (built and run, or manifest mismatch); empty Law-2 glob after the
flip → `check-law2` exit 1; pristine Wave 0 tree → the flip is the only
intentional red, landing with the first suite per (b).

### F13 — The tools-registry and no-fossils rules are not script-enforced — **should fix**

CLAUDE.md puts "No unindexed `tools/` scripts: every tool is registered in
`tools/README.md`" and "No `variants/` directory" under the header
"script-enforced in `make check-lints`". Nothing enforces either.
P23a: adding `tools/unregistered_tool.py` → `make check-lints` all ok.
P23b: adding `variants/old.c` → all ok. (The README itself hedges:
"`make check-lints` **may** fail on it".) Both rules are currently
review-only, i.e. exactly the manual discipline whose decay in v1
produced the fossils these rules cite.

Fix: small `tools/check_registry.py` in `check-lints` (`Makefile:131-134`
gains a line; the tool registers itself in the same change): every regular
file under `tools/` (excluding `__pycache__`, `README.md`) must appear as
a backticked name in `tools/README.md`, every backticked `.py`/`.sh` name
in the registry table must exist on disk, and no directory named
`variants` may exist anywhere in the tree (excluding `build/`, `.git/`,
`vendor/`).

Regression fixtures: P23a and P23b each → `check-lints` exit 1; deleting
a registered tool file but not its row → exit 1; pristine tree → ok.

### F14 — Settled build properties absent: no static link, no bench target — **nit**

- Law 5 lists "static link" as settled, but `grep -n static Makefile
  mk/*.mk` finds nothing (P26) and P01's `file` on the baseline binary
  reports `dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2`,
  with `ldd` resolving `libc.so.6`. Either the decision lands in a later
  packaging step (then CLAUDE.md's phrasing should say where) or the link
  line needs `-static` — currently code and law disagree with no bead
  visible in the tree to reconcile them.
- `Makefile:5-6` promises "Drop files in … `bench/<m>_bench.c` … globs
  pick them up", but the only `bench` match in `Makefile`/`mk/*.mk` is
  that comment (P27): no `BENCH` variable, no pattern rule, no target
  (`make -n bench_x_bench` → `No rule to make target`). Law 1 wants a
  bench per module as modules land; the first bench bead will discover
  the harness does not exist. Fix when the first bench lands: add
  `BENCHS := $(sort $(wildcard bench/*_bench.c))` + a rel pattern rule
  mirroring `$(B)/rel/tests/%`, and a `bench` target that builds (not
  gates) them; correct the comment meanwhile.

### F15 — Minor residuals and rule-table drift — **nit** (grouped)

- **FSM `default:` guard is type-dependent (P24).** Over an enum-typed
  operand the scaffold is sound: missing `PS_C` + `default:` →
  `error: enumeration value 'PS_C' not handled in switch
  [-Werror=switch-enum]`. The identical switch over `(int)s` compiles
  clean and passes all lints — there is no check that multi-mode state is
  even *stored* in an enum type (or not cast at the switch). Proportionate
  response is a convention note, not tooling: FSM state fields are
  enum-typed, never `int`; casts at a `switch` are a review flag.
- **Tests may include private headers (P25).** CLAUDE.md says tests use
  "public headers", but nothing distinguishes public from private: a test
  including `"x11/private.h"` passed all lints and built. Needs a naming
  convention first (e.g. `*_private.h` / `private.h`); then
  `check_no_c_includes` can reject the pattern in `tests/`+`fuzz/`.
- **Isolated layer links exist only for `rel` (desk check).**
  `LAYER_LINK_CHECK` is instantiated only against `build/rel`
  (`mk/layers.mk:70-71`); san/tsan/fuzz objects are linked in tests
  against *all* layers (`libs_desc`), so an upward reference compiled
  only under a sanitizer configuration would pass every lane. Acceptable
  for Wave 0; note it where the F4 symbol audit lands and audit `rel`
  (plus one sanitizer tree if it stays cheap).
- **INTRA_RULES vs PLAN §2 drift (desk check).** PLAN §2's L3 order is
  header-granular (`plat_types < …`, `render_types < font`), which the
  module-granular `INTRA_RULES` approximates as `render < raster,gl,font`
  — `font` may therefore include all of `render`, not just its types
  header. PLAN §3 also anticipates `config` glue "on `cmd/layout`", but
  `config` and `layout` are unordered in L4, so that include is currently
  *banned* (fail-closed direction: it will need a rule edit, which is the
  documented process — recorded so nobody is surprised in Wave 3).
  `findui` as one module vs PLAN's `find_session < find_panel` is already
  acknowledged in the `check_dag.py:43` comment.
- **Grammars silently off if the runtime file vanishes (P31b).** With
  `vendor/tree-sitter/lib/src/lib.c` deleted, a clean `make all` +
  `check-lints`/`check-layers` pass with zero grammars and no diagnostic
  (`mk/grammars.mk:22` gate). Partial vendor the other way (runtime
  present, one grammar dir deleted) fails loudly from clean
  (`No rule to make target '…/tree-sitter-c/src/parser.o'`, P31-clean) —
  good. Once `hl` references `hl_language_*`, the silent-off case becomes
  a loud link error for the editor binary; until then, a one-line
  `$(info)`/`TS_ENABLED` assertion in a chosen lane would close it, but
  it is Wave 3 (`hl`) territory.

## Tried and correctly rejected

- **Direct upward quoted include** (P02): `os.c` += `#include "app/app.h"` →
  `src/os/os.c:11: error: [inter-layer] os (L0) includes "app/app.h" (app, L5)`,
  exit 1.
- **`../` path spelling** (P05): `app.c` += `#include "../x11/x11.h"` →
  `[forbidden]`, exit 1. `resolve()` normalizes `..` correctly.
- **Comments without newlines** (P11): `#/**/include/*x*/"../x11/x11.h"` →
  `[forbidden]`, exit 1 (comment-blanking handles this; contrast F3/P07).
- **Trigraph** (P09): `??=include "x11/x11.h"` — `check_dag` misses it, but
  the compiler rejects it under `-Werror` (`cc1: all warnings being treated
  as errors`), so the tree does not build. No linter fix needed while
  `-Werror` stands; noted in case warning flags ever relax.
- **Upward include inside `#if 0`** (P29): still flagged `[inter-layer]`,
  exit 1. Conservative (dead code counts), which is the right default.
- **Unassigned module dir / stray `src/` file** (P32): `src/evilmod/` and
  `src/stray.c` both reported, exit 1.
- **Upward `extern` call, non-weak, no include** (P12): linters pass it
  (no edge), but the isolated links catch it exactly as designed —
  `undefined reference to 'app_smoke'` in the layer tests; `make all`
  also fails since `all` builds the layer test bins. The inter-layer
  link backstop works for strong symbols (contrast F5).
- **Isolation basics** (P20 controls): bare `"sublimite"`,
  `"/home/x/.config/sublimite/config.toml"`, adjacent `"sub" "limite"`,
  and spelled-out `"sublimite-v2"` each → exit 1 with a precise message.
  Adjacent-literal concatenation is genuinely handled — except across a
  line splice (F9/P20h).
- **Direct `#include "*.c"` in tests, both cases** (P21-ok, P21e):
  `"../src/os/os.c"` and `"../src/os/os.C"` → exit 1.
- **Failing suite present and correctly globbed** (P22b first half):
  `check-san` → Error 1. The lane machinery works; only discovery is
  hole-prone (F12).
- **Missing layer-test source from a clean tree** (P22c-clean):
  `No rule to make target 'build/rel/tests/layer_L2_test.o'`, exit 2.
- **Fuzzer without committed seeds** (P22d): `fuzz-smoke: FAIL
  fuzz/corpus/probe missing (commit minimal seeds)`, Error 1 — the one
  empty-lane neighbour that already fails closed per artifact.
- **FSM `switch` over an enum with `default:` hiding a case** (P24a):
  `error: enumeration value 'PS_C' not handled in switch
  [-Werror=switch-enum]` — `-Wswitch-enum` does the job the CLAUDE.md
  FSM rule asks of it, for enum-typed operands (contrast F15).
- **Partial vendor import** (P31-clean): runtime present but
  `vendor/tree-sitter-c/` deleted → `No rule to make target
  '…/tree-sitter-c/src/parser.o', needed by 'build/rel/libui.a'`, exit 2.
- **Baseline itself** (P01): clean `make check` green *with* the
  13-grammar table enabled, so `mk/grammars.mk`'s active path (relaxed
  warns, per-grammar include dirs, the 13-count assertion) is exercised
  and working as written.

## Verdict

VERDICT: NOT READY for Wave 1 until F1, F2, F4, F9 and the F12 Law-2 flip land; the scaffold's happy path is sound and its diagnostics are precise, but every mechanical rule has at least one demonstrated bypass that ordinary (not malicious) code can trip.

Tally (M — counted from the findings above; 59 distinct probes were run
in the scratch tree, enumerated in `/tmp/m1/batchA.sh`,
`/tmp/m1/batchB.sh`, `/tmp/m1/batchB2.sh`, `/tmp/m1/batchC.sh` with logs
alongside):

- **Blocks Wave 1 (5):** F1 angle includes invisible to the DAG checker;
  F2 computed/macro includes invisible to all linters; F4 no-include
  `extern` coupling has no link backstop for intra-layer orders or the
  L5-forbidden rule; F9 isolation needles miss the relative-leaf
  (`"sublimite/…"`) and abstract-socket (`"\0sublimite"`) spellings the
  Wave 1 `os`/`ipc` harvest will actually write, and never decodes
  escapes; F12 glob-only suite discovery plus empty-pass Law-2 lane
  inside `make check`.
- **Should fix (8):** F3 phase-2/3 spellings (splice, newline-in-comment,
  `%:` digraph, `\v`/`\f`); F5 weak undefined symbols vs the isolated
  link; F6 unscanned transitive files (`.def`, `.H`, outside-`src/`,
  symlink dirs); F7 unvalidated `LDLIBS` on the isolated link lines;
  F8 `layers.mk` parser/make divergence (`+=`); F10 unguarded `APP_ID`
  and literal-only isolation (stringification; registry over-claim);
  F11 `.c`-include via shims/symlinks/`bench/`; F13 tools-registry and
  `variants/` rules unenforced despite the "script-enforced" header.
- **Nit (2 grouped):** F14 no static link and no bench target despite
  settled/commented claims; F15 residuals — `(int)`-cast FSM switches,
  private headers includable from tests, `rel`-only layer links,
  PLAN/INTRA_RULES granularity drift, grammars silently off when the
  runtime file vanishes.
- **Correctly rejected (19 probes):** direct and `../` upward quoted
  includes, same-line comment padding, `#if 0` dead-code includes,
  unassigned dirs/stray files, trigraphs (via `-Werror`), non-weak upward
  `extern` (via the isolated links), the four plain isolation spellings,
  direct/uppercase `.c` includes in tests, a present-and-globbed failing
  suite, missing layer-test source from clean, fuzzer without seeds,
  enum `switch` missing a case under `default:`, partial vendor import,
  and the full baseline with grammars enabled.

Per Law 8: the five blockers each let a named Wave 1 decay mode through
invisibly (upward or cross-module coupling during harvest, v1-profile
collision, an empty Law-2 gate at bead close) and should become beads
before Wave 1 modules land; the should-fix set fits one or two hardening
beads (the F4/F5 symbol audit subsumes several); F14/F15 are backlog
material unless a user or trace points at them.
