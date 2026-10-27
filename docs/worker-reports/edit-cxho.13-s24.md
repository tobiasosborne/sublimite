# edit-cxho.13 — worker report

Work stayed in `/home/tobias/Projects/sublimite/.wt/edit-cxho.13-s24`.
No `bd`, `git`, installed build/profile changes, other-worktree access,
delegation, or additional review pass. Numeric outputs below are command
observations (M)[AC], with `BAT0: Not charging`; fixture offsets/counts derive
from test strings (P). Exit codes use the process success/error convention (P).

## Files touched

- `src/editor/findpanel.c`: selection prefill, regex quoting, slurp handlers,
  whole-word find-under and worker-backed closed-panel navigation.
- `src/editor/private.h`: pending document-boundary seek state, independent of
  the saved incremental-preview origin.
- `src/keys/keys.c`: implement Slurp Find/Replace and Find Under/Previous rows.
- `tests/editor_test.c`: input-injection coverage for prefill/count/preview,
  retained selected query, multiline fallback, hidden slurp, replacement,
  caret words, next/previous/wrap, queued navigation and regex metacharacters;
  adapt the existing replacement fixture to Ctrl+H's new preview.
- `tests/keys_test.c`: expected flags for the implemented commands;
  Alt+F3 remains unimplemented.
- `docs/decisions/edit-cxho.13.md`: implementation choices and derivations.
- `docs/worker-reports/edit-cxho.13-s24.md`: this report.

## Red run, before implementation

Built the sanitizer targets with:

```sh
make -j4 build/san/tests/editor_test build/san/tests/keys_test build/san/tests/findui_test
```

Build exit: `0`. Each focused editor invocation used:

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 \
  sh tools/with_display_lock.sh build/san/tests/editor_test --cxho13-prefill
```

The runner also invoked `--cxho13-slurp`, `--cxho13-under`, and
`--cxho13-regex`, removing its temporary runtime directory on exit.
Actual assertion output:

```text
== editor_test --cxho13-prefill
editor_test:4493: FAIL state.query_length == strlen(query)
editor_test:4690: FAIL find_focus_query(e, query) == 0
exit=1
== editor_test --cxho13-slurp
editor_test:4493: FAIL state.query_length == strlen(query)
editor_test:4732: FAIL find_focus_query(e, query) == 0
exit=1
== editor_test --cxho13-under
editor_test:4493: FAIL state.query_length == strlen(query)
editor_test:4766: FAIL find_focus_query(e, query) == 0
exit=1
== editor_test --cxho13-regex
editor_test:4493: FAIL state.query_length == strlen(query)
editor_test:4802: FAIL find_focus_query(e, escaped) == 0
exit=1
keys_test: FAIL ctrl+F3 expected action=72 flags=0 value=0; got action=72
tests/keys_test.c:390: assertion failed: 0
keys_test exit=134
```

Those sandboxed editor red runs also printed LeakSanitizer's ptrace error.
Subsequent sanitizer execution used the approved outside-sandbox command path,
retaining `ASAN_OPTIONS=detect_leaks=1`; no sanitizer was disabled.
The absent pathname socket initially suggested an unavailable display, but
`DISPLAY=:99 xdpyinfo` outside the sandbox succeeded through the abstract
socket (exit `0`). No other display was selected by this worker.

The first broader editor run also exposed the expected old Ctrl+H assumption:

```text
editor_test:4906: FAIL !memcmp(bytes,"cat dog fox",sizeof text-1)
editor_test:7442: FAIL findpanel_contract()==0
editor_test exit=1
```

Selection prefill now previews from the reopened panel's saved EOF cursor,
wrapping to the first `cat`. The fixture now asserts that preview and advances
to its original replacement target before the existing undo/recovery checks.

## Green run and verification

```sh
make -j4 all
# exit 0, gcc 13 with the repository's C11/Werror/shadow/conversion flags
make -j4 build/san/tests/editor_test build/san/tests/keys_test build/san/tests/findui_test
# exit 0, clang 18 ASan/UBSan with the same warning flags
ASAN_OPTIONS=detect_leaks=1 build/san/tests/keys_test
# exit 0
ASAN_OPTIONS=detect_leaks=1 build/san/tests/findui_test
# exit 0
```

```text
keys_test: 2572 generated names and 2442 canonical values checked both directions against libxkbcommon
keys_test: 259 independent binding rows passed
keys_test: 20000 lookups mallocs=0 guard=ASan-inert
keys_test: all passed
findui_test: bottom-row rendering, damage validation and typing allocation guard passed
findui_test: all passed
```

The focused sanitizer runs used the same display-lock command as red, outside
ptrace with leak detection enabled. Each exited `0`:

```text
edit-cxho.13 prefill: selection/count/origin, retained selected query, Replace, multiline fallback passed
edit-cxho.13 slurp: hidden query/F3/previous, replacement, caret word, unchanged selection passed
edit-cxho.13 find-under: next/previous whole words, wrap, explicit substring, queued F3 passed
edit-cxho.13 regex: escaped prefill/slurp/find-under and literal replacement passed
```

Release allocator verification used each selector with
`XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test --cxho13-<selector>`.
All exited `0`, including their allocation assertions with the active release
guard (zero allocations (P), the binding typing-path rule).

The full check invoked the editor suite as:

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 \
  sh tools/with_display_lock.sh build/san/tests/editor_test
```

The editor process exited `0` (the `set -e` full-check loop proceeded to the
next suite). Its output included:

```text
edit-cxho.12 incremental: mapped=0 wrapped=0 preview/scroll/origin/empty/next/previous/no-match/Escape mallocs=0 passed
edit-cxho.12 incremental: mapped=0 wrapped=1 preview/scroll/origin/empty/next/previous/no-match/Escape mallocs=0 passed
edit-cxho.12 incremental: mapped=1 wrapped=0 preview/scroll/origin/empty/next/previous/no-match/Escape mallocs=0 passed
edit-cxho.13 prefill: selection/count/origin, retained selected query, Replace, multiline fallback passed
edit-cxho.13 slurp: hidden query/F3/previous, replacement, caret word, unchanged selection passed
edit-cxho.13 find-under: next/previous whole words, wrap, explicit substring, queued F3 passed
edit-cxho.13 regex: escaped prefill/slurp/find-under and literal replacement passed
edit-cxho.3 focus: document/query typing, panel caret, Tab, F3/previous, Escape, Return, Ctrl+F/H passed; field mallocs=0 document mallocs=0 guard=ASan-inert
edit-73j find/replace, disjoint undo/redo and journal recovery passed
editor_test: native X11 translation, editor loop and WM close passed
editor_test: all passed
```

Full check completed with exit `0`:

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY=:99 \
  EDIT_REFUSE_REAL_DISPLAY=1 ASAN_OPTIONS=detect_leaks=1 make -j4 check
```

Actual summary (M)[AC], with the binary count produced by Makefile's test glob:

```text
check: 112 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
make -j4 check exit=0
```

The display-lock contract intentionally prints timeout and nested-make failure
messages while verifying refusal/exit propagation; its final result was PASS.
All required named sanitizer suites passed with leak detection enabled. All
display suites inherited the assigned `:99`, temporary runtime directory and
the Makefile's `tools/with_display_lock.sh` runner. Temporary runtime
directories were removed by the invocation traps.

Power/load and `df -h /` around the full check (M)[AC]:

```text
Before:
Not charging
13:09:39 up 1 day, 15:12, 1 user, load average: 2.56, 3.27, 3.39
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  163G  1.6T  10% /
After:
Not charging
13:28:14 up 1 day, 15:31, 1 user, load average: 0.52, 1.03, 1.83
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  165G  1.6T  10% /
```

The new tests and optional benchmark use small text fixtures. The full suite
also exercised its existing sparse large-file fixtures and their cleanup.
Existing shared corpus inputs were retained.

The optional tracked editor benchmark used the existing default sample count
from `bench/editor_bench.c` and the small `foo bar foo\nfoo\n` fixture:

```sh
XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_REFUSE_REAL_DISPLAY=1 \
  sh tools/with_display_lock.sh build/bench/editor_bench \
  --track --no-idle --no-p4 --serial-only --file=build/cxho13-bench.txt
```

It completed the null and raster rows. The requested GL backend fell back to
raster on this Xvfb; that last row was stopped to release the display lock for
required verification. Command exit: `143` (M)[AC], SIGTERM; the following are
completed rows, not a claim that the whole benchmark completed. Its fixture,
runtime directory and interrupted journal were removed.

Power/load stamps adjacent to the timing observations (M)[AC]; raw benchmark
latencies are in ns, as defined by its harness:

```text
cat /sys/class/power_supply/BAT0/status: Not charging
uptime before command: 13:13:11 up 1 day, 15:16, 1 user, load average: 1.92, 2.87, 3.23
STAMP (M)[AC] BAT0=Not charging load1=1.14 shared box
BENCH name=editor_null_serial_injection_T4_G1 n=10000 required_n=10000 p50=337203 p99=593343 ci95_p50=[336941,337430] ci95_p99=[581720,613465] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=1.14 verdict=TRACK
G1 null workload=serial keys=10000 completed=10000 arrivals_active=0 coalesced=0 foreign_input=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=TRACK (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=1.12 shared box
BENCH name=editor_raster_serial_injection_T4_G1 n=10000 required_n=10000 p50=14731960 p99=15484843 ci95_p50=[14717532,14747153] ci95_p99=[15459491,15551825] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=1.12 verdict=TRACK
G1 raster workload=serial keys=10000 completed=10000 arrivals_active=10000 coalesced=0 foreign_input=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=TRACK (M)[AC]
cat /sys/class/power_supply/BAT0/status: Not charging
uptime after command: 13:22:25 up 1 day, 15:25, 1 user, load average: 0.87, 1.23, 2.20
```

The extra `--73j-find` retest initially timed out acquiring the display lock
while the benchmark held it (exit `1`). It was retried after the benchmark was
stopped. This was a lock acquisition failure, with no test assertion executed.
The retry used the same sanitizer/display-lock command with `--73j-find`,
exited `0`, and printed:

```text
edit-73j find/replace, disjoint undo/redo and journal recovery passed
edit-73j replacement Escape/error prefixes, undo and recovery passed
edit-73j distant wrapped/clipped find results are visible passed
editor_test --73j-find exit=0
```

## Remaining scope and findings

Alt+F3 remains marked `KEYS_ARG_UNIMPLEMENTED`: the view has a single
cursor/anchor pair. Multiple selections and find-all stay with edit-cxho.14.
Word slurping reuses `view_select_word` and its existing bounded local-line
resolver; a word requiring more than that prepaid fast path returns its error
without changing the field or document. No scan bound or allocation budget
was added.

Outside scope: the tracked raster row above misses its existing G1 target on
the shared Xvfb. The full editor sanitizer latency regression passed. There
was no before/after benchmark comparison and no rendering change in this bead;
this measurement does not establish a regression. The requested-GL fallback
benchmark row remains incomplete.
