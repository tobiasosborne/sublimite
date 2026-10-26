# edit-v4ee-s12 worker report

Verdict: partial pending the coordinator's resolution of the strict acceptance
grep versus the explicit GL scope exclusion. The requested implementation is
complete. Full unrestricted sanitizer verification is running; its final result
will replace this sentence before handover.

## Changes

- Added INIT-only `plat_display_size(plat_backend, plat_display_info *)` and
  `plat_display_interval_ns(const plat *)`. The existing X11 discovery and timing
  bodies moved into `src/x11/x11_posix.c`, including their evidence comments.
  A comparison against removed diff lines confirmed identical moved bodies
  after signature/type/error-code/owner substitutions and the null guard.
- Kept pure runtime sizing helpers in the editor, removed its duplicate display
  type, routed startup and highlight through the shared platform API, retained
  startup error translation, and moved RandR linkage to the X11 module.
- Delegated Wayland startup sizing to its existing query, with a mock-output
  regression through the new shared API. Unknown refresh timing still returns
  zero (P: the API's unknown sentinel).
- Vendored the installed keysym constants and original licences. The deterministic
  generator and generated output are supplied together. Lookup uses sorted
  arrays and binary search without allocation. Config and chord formatting now
  use `keys_*` functions. Every constants-only system-header include in active
  source, tests, benches, fuzzers and nested prewake fixtures uses the vendor
  copy. The Linux oracle alone adds libxkbcommon explicitly to its test target.

Design choices and compatibility rules are in `docs/decisions/edit-v4ee.md`.

## Red / green evidence

Commands below run from this worktree. Exit codes are measured process statuses
(M), with success/failure defined by each command, not performance budgets.

Platform red, before edits:

```sh
rg -n '#[[:space:]]*include[[:space:]]*<(xcb/|X11/|xkbcommon/xkbcommon\.h)' src \
  --glob '!src/x11/**' --glob '!src/wayland/**' --glob '!src/raster/**'
```

Exit `0` (M: matches present; a clean grep would return `1`):

```text
src/editor/display_size.c:6:#include <xcb/xcb.h>
src/editor/display_size.c:7:#include <xcb/randr.h>
src/editor/highlight.c:7:#include <xcb/xcb.h>
src/editor/highlight.c:8:#include <xcb/randr.h>
src/config/settings.c:6:#include <xkbcommon/xkbcommon.h>
src/gl/renderer.inc:16:#include <xcb/xcb.h>
src/gl/renderer.inc:17:#include <xcb/present.h>
src/gl/renderer.inc:18:#include <xcb/xcbext.h>
src/keys/keys.c:3:#include <xkbcommon/xkbcommon.h>
```

Keys red: wrote public declarations and the bidirectional oracle helper before
implementations. `make -j64 build/tests/keys_test` exited `2` on undefined
`keys_keysym_from_name` / `keys_keysym_name`. After generating the tables, wired
both complete-table loops into the same test and reran the same command before
implementation; it again exited `2` with:

```text
/home/tobiasosborne/Projects/sublimite/.wt/edit-v4ee-s12/tests/keys_test.c:295:(.text+0xb8): undefined reference to `keys_keysym_from_name'
/usr/bin/ld: /home/tobiasosborne/Projects/sublimite/.wt/edit-v4ee-s12/tests/keys_test.c:299:(.text+0x106): undefined reference to `keys_keysym_name'
/usr/bin/ld: /home/tobiasosborne/Projects/sublimite/.wt/edit-v4ee-s12/tests/keys_test.c:302:(.text+0x137): undefined reference to `keys_keysym_from_name'
/usr/bin/ld: /home/tobiasosborne/Projects/sublimite/.wt/edit-v4ee-s12/tests/keys_test.c:310:(.text+0x204): undefined reference to `keys_keysym_from_name'
/usr/bin/ld: /home/tobiasosborne/Projects/sublimite/.wt/edit-v4ee-s12/tests/keys_test.c:311:(.text+0x22c): undefined reference to `keys_keysym_from_name'
/home/tobiasosborne/Projects/sublimite/.wt/edit-v4ee-s12/tests/keys_test.c:336:(.text+0x42c): undefined reference to `keys_keysym_from_name'
collect2: error: ld returned 1 exit status
make: *** [Makefile:217: build/tests/keys_test] Error 1
```

The table run is preserved in `build/worker-edit-v4ee/keys-table-red.log`; the
initial run is in `keys-red.log` there. Additional numeric/legacy-alias regression
inputs produced a failing assertion (exit `134`, M: shell status for SIGABRT)
before fixing those compatibility details; see `keys-forms-red.log`.

Keys green:

```sh
make -j64 build/tests/keys_test && DISPLAY=:99 build/tests/keys_test
```

Exit `0` (M), with:

```text
keys_test: 2572 generated names and 2442 canonical values checked both directions against libxkbcommon
keys_test: 259 independent binding rows passed
keys_test: 20000 lookups mallocs=0 guard=active
keys_test: all passed
```

Counts above are (M), derived from the generated arrays' `sizeof` ratios, the
existing independent default expectations, and the existing allocation guard
loop. Every name entry is compared to `xkb_keysym_from_name` using
`XKB_KEYSYM_CASE_INSENSITIVE`; every numeric value is formatted with both APIs,
compared byte-for-byte and resolved back with both APIs. Uppercase/lowercase
variants are checked too. Canonical-index rows are independently traversed.
The existing default-keymap parser verifies its names against the new resolver
while retaining libxkbcommon's original oracle for the binding expectations.
Actual config parsing is checked against oracle-derived normalization for
literal, mixed-case, alias, numeric and invalid names; existing config fixtures
remain unchanged.

Platform green for the changed modules:

```sh
rg -n '#[[:space:]]*include[[:space:]]*<(xcb/|X11/|xkbcommon/xkbcommon\.h)' \
  src/editor src/keys src/config
```

Exit `1`, no matches (M). The complete acceptance grep still returns `0`:

```text
src/gl/renderer.inc:16:#include <xcb/xcb.h>
src/gl/renderer.inc:17:#include <xcb/present.h>
src/gl/renderer.inc:18:#include <xcb/xcbext.h>
```

Those are existing GL renderer dependencies, explicitly outside this brief's
edit scope. Their remaining count is (M), derived by counting the pasted lines.

```sh
DISPLAY=:99 sh tools/with_display_lock.sh build/tests/x11_font_size_test
```

The sandboxed attempt exited `1` because it could not open X11. The same command
outside the sandbox exited `0` (M):

```text
x11_font_size_test: DPI 99.965103 from X resources/physical display; em 14 = round(10.0*DPI/72); window 1728x1260 = floor(work-area 2880x1800 * .60,.70); native zoom passed
```

`DISPLAY=:99 build/tests/wayland_test` exited `0` (M), including the new shared
output query and existing lifecycle/input/protocol checks.

## Keysym compatibility

The table retains every define, including deprecated aliases; canonical output
uses the first header declaration for a value, matching the pinned library.
Case-insensitive collisions preserve that library's results rather than assuming
that the original or lowercase spelling identifies a unique value. The explicit
non-letter collision winners and their rationale are in the decision document.

The table itself does not contain arbitrary Unicode or raw numeric names. Small
procedural paths preserve the library's `UXXXX` and `0x...` forms and numeric
bounds. `U+XXXX` is rejected by both. Legacy `XF86_` aliases, including repeated
prefix underscores, are handled procedurally. Unicode and hexadecimal fallback
formatting, invalid values, return lengths and truncated output are oracle
checked. The pinned header bounds the supported named symbols; future library
names absent from that header require a vendor update. The new resolver exposes
the case-insensitive contract used by config, not the library's optional
case-sensitive flag or its unrelated keymap/compose APIs. Config's existing
single-character byte path stays intact.

## Verification

`python3 tools/keysymgen.py --check` and `git diff --check` exited `0` (M).
No system keysym-constants includes remain in the migrated active trees.

`nm -u build/rel/src/keys/*.o build/rel/src/config/*.o | rg 'xkb_'` exited
`1`, with no undefined XKB references (M). Neither module had its own LDLIBS
file to remove. The X11 backend retains libxkbcommon.

An isolated link smoke test compiled the production keys and config parsing
sources with their utf8/work dependencies, section garbage collection and only
`-lm`/pthread. It parsed a named config binding and formatted `Ctrl+Left`;
compile/link/run exited `0` (M). Exact command:

```sh
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE \
  -pthread -Isrc -Ivendor/tree-sitter/lib/include -ffunction-sections -fdata-sections \
  build/worker-edit-v4ee/link-smoke.c src/keys/keys.c src/keys/names.c \
  src/config/parser.c src/config/settings.c src/config/theme.c \
  src/utf8/utf8.c src/work/work.c -Wl,--gc-sections -lm \
  -o build/worker-edit-v4ee/link-smoke
build/worker-edit-v4ee/link-smoke
```

Section collection removes unrelated config-to-view entry points, allowing the
link to demonstrate the keys/config parse/format paths without platform code.
Attempting the entire pre-existing config test against the monolithic archive
without backend libraries pulled in editor/platform code and failed on those
unrelated dependencies; the isolated check and object-symbol audit establish
the module boundary directly.

`make -j64 all`: exit `0` (M), gcc version from `gcc --version` is Ubuntu
`13.3.0-6ubuntu2~24.04.1`. Final output includes:

```text
MAKE_ALL_EXIT=0
```

Sanitizer compiler: Ubuntu clang `18.1.3`, from `clang --version`.

```sh
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 make -j64 check
```

Sandboxed exit `2` (M), stopping at `base_test`:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:236: check] Error 1
MAKE_CHECK_EXIT=2
```

The complete command is rerun outside the sandbox, with LeakSanitizer explicitly
on. Makefile display suites retain `tools/with_display_lock.sh` and `DISPLAY=:99`.
Final unrestricted result: pending.

## Measurement context and benches

All measurements here are (M)[AC, desktop Spiral]. BAT0 is absent (tested before
measurement). Existing bench harnesses print `[unknown]` for missing BAT0; this
report supplies the desktop AC stamp required by the worker rules. `uptime`
readings taken around verification:

```text
 08:59:38 up  1:16,  1 user,  load average: 5.38, 1.89, 1.94
[AC, desktop Spiral] BAT0 absent
 09:00:11 up  1:16,  1 user,  load average: 7.51, 2.99, 2.32
```

Required touched-module bench invocations completed with exit `0` (M):

```sh
DISPLAY=:99 build/bench/keys_bench
DISPLAY=:99 build/bench/config_bench
DISPLAY=:99 sh tools/with_display_lock.sh build/bench/editor_bench \
  --track --no-idle --no-p4 --serial-only --keys=64
DISPLAY=:99 sh tools/with_display_lock.sh build/bench/x11_bench
DISPLAY=:99 build/bench/wayland_bench
```

The editor arguments are the existing `bench/editor_bench.args`, not a new sample
policy. Its null/raster/fallback workloads completed with structural checks and
no guarded allocations. GL fell back to raster because EGL/GL/Present was
unavailable on this display. Wayland's benchmark reported its existing explicit
skip: it needs a compositor with presentation feedback. No Wayland timing was
fabricated. Raw logs: `keys-bench.log`, `config-bench.log`, `editor-bench.log`,
`x11-bench.log` under `build/worker-edit-v4ee/`.

Selected tracked outputs (M)[AC, desktop Spiral], with sampling derivations and
load printed by their existing harnesses:

```text
POWER status=unknown [unknown] load1=7.51; keys measurements TRACK only
BENCH keys_lookup TRACK n=1000000 p50=0.000 us p99=0.100 us (M)[unknown] load1=7.51 no limit (no derivation for 1 us)
STREAM (M)[unknown] load1=7.51 matches=436904 prefixes=250596 cancelled=125596 misses=186904 mallocs=0 guard=active gate_mallocs=0(G)
config_bench: parse (M)[unknown] power=unknown load1=7.51 n=77 derivation=one_sample_per_fixture_byte fixture_bytes=77 p50_ns=2300 p99_ns=26599 verdict=TRACK
power before measurement: unknown; one-minute load=4.71
x11 key translate (press, us layout, no compose): n=200000 p50 300 ns p99 400 ns (TRACK, no separate G1 gate) (M)[unknown] indicative on a loaded box; includes timing clock read; allocations on path: 0; load=4.71
TRACK x11 startup main->plat_init done->map requested: n=200 p50=7.243 ms p99=12.656 ms (M)[unknown]; G4a full startup p50<=25 ms p99<=40 ms (G); subset budget PASS (a note); excludes exec, first-frame raster and input acceptance; load=4.71
```

## Found, not fixed

- `src/gl/renderer.inc` still includes XCB headers and owns X11 Present/native
  connection handling. This is the remaining strict acceptance grep conflict;
  the brief explicitly says to report it rather than edit GL.
- `src/raster/raster.c` still owns the XShm/XCB presentation path reserved for
  W1.2. It is already excluded from the acceptance grep.
- Existing bench power detection prints unknown on this battery-free desktop;
  measured AC context is supplied above, without changing harnesses.

## Coordinator decisions

Resolve whether to waive/exclude GL from this bead's acceptance grep or schedule
its platform seam separately. No other design decision is required for the
implemented scope. The pinned ambiguous keysym winners should be reviewed when
updating the vendored header or the oracle library.

## Files touched

The broad include-only edits below are required by the vendored-header scope;
behavioral edits are described above.

- `Makefile`
- `bench/editor_bench.c`
- `bench/editor_large_bench.c`
- `bench/keys_bench.c`
- `bench/prewake/b/prewake.c`
- `bench/prewake/c/prewake.c`
- `bench/prewake/contract.c`
- `bench/wayland_bench.c`
- `docs/decisions/edit-v4ee.md`
- `docs/worker-reports/edit-v4ee-s12.md`
- `fuzz/editor_fuzz.c`
- `fuzz/keys_fuzz.c`
- `fuzz/prewake_fuzz.c`
- `src/config/settings.c`
- `src/editor/LDLIBS`
- `src/editor/display_size.c`
- `src/editor/editor.c`
- `src/editor/fif.c`
- `src/editor/findpanel.c`
- `src/editor/highlight.c`
- `src/editor/input.c`
- `src/editor/large.c`
- `src/editor/menubar.c`
- `src/editor/palettepanel.c`
- `src/editor/runtime.h`
- `src/keys/keys.c`
- `src/keys/keys.h`
- `src/keys/keysym_names.h`
- `src/keys/names.c`
- `src/main.c`
- `src/x11/LDLIBS`
- `src/x11/plat.h`
- `src/x11/x11_posix.c`
- `tests/config_find_test.c`
- `tests/config_test.c`
- `tests/dataloss_test.c`
- `tests/editor_checkpoint_test.c`
- `tests/editor_hot_exit_test.c`
- `tests/editor_large_test.c`
- `tests/editor_p4_test.c`
- `tests/editor_scroll_test.c`
- `tests/editor_test.c`
- `tests/findui_test.c`
- `tests/font_selection_test.c`
- `tests/font_size_test.c`
- `tests/keys_test.c`
- `tests/menu_test.c`
- `tests/palette_test.c`
- `tests/unicode_render_test.c`
- `tests/wayland_test.c`
- `tests/x11_font_size_test.c`
- `tools/keysymgen.py`
- `vendor/README.md`
- `vendor/xkbcommon/xkbcommon-keysyms.h`
