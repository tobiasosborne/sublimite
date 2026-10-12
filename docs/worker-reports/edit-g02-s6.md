# edit-g02 worker report — s6

Implemented the find-panel muscle-memory bead and stopped. No bd or git commands
were run. No protected handoff, worklog, plan, rules or product files were edited.

## Files touched

- `src/editor/findpanel.c`: Return/keypad Enter advance past the initial query
  preview; Shift reverses navigation; an uncached ordinal completion selects its
  result without advancing again. Configured `replace_next` uses the existing
  replacement path while the replacement panel is visible.
- `src/keys/keys.c`: mark the default Ctrl+Shift+H action implemented.
- `tests/editor_test.c`: first-Return, keypad, reverse/wrap, pending search,
  unmatched query, Escape and uncached previous-match regressions; update the
  prior panel contract for the corrected first-Return behavior.
- `tests/config_find_test.c`: default/normalized and remapped replace-next,
  default-shortcut removal, panel persistence, Ctrl+Enter, Ctrl+Alt+Enter and undo.
- `tests/keys_test.c`: independent expected capability for Ctrl+Shift+H.
- `docs/decisions/edit-g02.md`: local design choices and fixture derivations.
- `docs/worker-reports/edit-g02-s6.md`: this report.

No input or scroll-controller files changed. Existing prepaid storage, mutation,
undo, journal and layout machinery are reused. No new allocation path or budget.

## Evidence conventions

(P) Fixture byte offsets and sizes are string lengths; the uncached fixture is
match-cache capacity plus the literal query byte length. Geometry reuses existing
minimap/tab/panel dimensions, as derived next to the tests and in the decision.
Zero denotes success, empty state or no allocations; direction magnitude and C
terminator subtraction follow the existing API/string contracts.

(M)[AC] All pasted runs below were observed with BAT0 `Not charging` (the tree's
AC classification). Source line numbers are assertion locations; error/action
values are API/enum values, not budgets. Counts in existing suite output come
from its independent binding array and existing lookup stream. No fixture counts
or deadlines were invented. Compiler versions observed through `--version` were
gcc 13.3.0 and clang 18.1.3 (M)[AC]. The Makefile applied the required C11 and
Wall/Wextra/Werror/Wshadow/Wconversion flags, with ASan/UBSan in sanitizer builds.

## Red run

Tests were written before implementation. Build:
`make build/san/tests/editor_test build/san/tests/config_find_test build/san/tests/keys_test`.
The failing runs used `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99`.

```text
$ build/san/tests/editor_test --g02-find
editor_test:3656: FAIL editor_view(e).selection.anchor==sizeof "cat "-1u && editor_view(e).selection.cursor==sizeof "cat cat"-1u
exit=1
$ build/san/tests/config_find_test --g02-default
config_find_test: FAIL !memcmp(bytes,"fox cat cat",sizeof bytes)
exit=1
$ build/san/tests/config_find_test --g02-remapped
config_find_test: FAIL !memcmp(bytes,"fox cat cat",sizeof bytes)
exit=1
$ build/san/tests/keys_test
keys_test: FAIL ctrl+shift+h expected action=66 flags=0 value=0; got action=66
tests/keys_test.c:306: assertion failed: 0
exit=-6
```

The uncached Shift+keypad Enter regression was then added before its continuation
fix; build `make build/san/tests/editor_test`, followed by the same environment:

```text
$ build/san/tests/editor_test --g02-previous
editor_test:3696: FAIL editor_view(e).selection.anchor==sizeof text-(sizeof "a"-1u) && editor_view(e).selection.cursor==sizeof text
exit=1
```

(P) The negative key-table exit is its existing assertion failure; the ordinary
failing exits are the tests' error-return convention. (M)[AC] Captured logs:
`build/edit-g02-red.log`, `build/edit-g02-red-previous.log` and their build logs.

## Green run

Final targeted clang ASan/UBSan runs, with `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99`:

```text
$ build/san/tests/editor_test --g02-find
edit-g02 Return/keypad/previous/wrap/pending/empty/Escape passed
exit=0
$ build/san/tests/editor_test --g02-previous
edit-g02 uncached Shift+keypad Enter selects previous exactly once passed
exit=0
$ build/san/tests/config_find_test
config_find_test: find-next override with panel open passed
config_find_test: find-next override with panel closed passed
config_find_test: open/replace/previous/toggle overrides and local editing passed
config_find_test: default replace-next and retained Enter shortcuts passed
config_find_test: remapped replace-next and retained Enter shortcuts passed
exit=0
$ build/san/tests/keys_test
keys_test: 259 independent binding rows passed
keys_test: 20000 lookups mallocs=0 guard=ASan-inert
keys_test: all passed
exit=0
$ build/san/tests/editor_test --73j-find
edit-73j find/replace, disjoint undo/redo and journal recovery passed
edit-73j replacement Escape/error prefixes, undo and recovery passed
edit-73j distant wrapped/clipped find results are visible passed
exit=0
```

Final gcc release runs (allocation interposition active):

```text
$ build/tests/editor_test --g02-find
edit-g02 Return/keypad/previous/wrap/pending/empty/Escape passed
exit=0
$ build/tests/editor_test --g02-previous
edit-g02 uncached Shift+keypad Enter selects previous exactly once passed
exit=0
$ build/tests/config_find_test
config_find_test: find-next override with panel open passed
config_find_test: find-next override with panel closed passed
config_find_test: open/replace/previous/toggle overrides and local editing passed
config_find_test: default replace-next and retained Enter shortcuts passed
config_find_test: remapped replace-next and retained Enter shortcuts passed
exit=0
$ build/tests/keys_test
keys_test: 259 independent binding rows passed
keys_test: 20000 lookups mallocs=0 guard=active
keys_test: all passed
exit=0
$ build/tests/editor_test --73j-find
edit-73j find/replace, disjoint undo/redo and journal recovery passed
edit-73j replacement Escape/error prefixes, undo and recovery passed
edit-73j distant wrapped/clipped find results are visible passed
exit=0
```

(M)[AC] Logs: `build/edit-g02-green-targeted.log` and
`build/edit-g02-green-release.log`. The existing keys stream reports no mallocs;
the editor ingress/submit counter and config-find guarded actions also pass.

## Wider verification

- `make -j"$(nproc)" all`: PASS, final release tree. Parallelism derives from
  available processors reported by nproc (M)[AC], not a new worker budget.
  Log: `build/edit-g02-make-all-final.log`.
- `ASAN_OPTIONS=detect_leaks=0 make -j"$(nproc)" check`: NOT GREEN. The display
  classified CLI suite cannot create its IPC socket in this sandbox.
  Log: `build/edit-g02-make-check.log`.
- Non-display Makefile filtering was applied through a command-line `TEST_SAN`
  override, without editing the Makefile. Every non-display binary was attempted;
  the only actual binary failures are the IPC and journal crash cases detailed
  below, plus the unclassified display portions of file_kill_test. Remaining
  binaries pass. Expected failure diagnostics inside the timeout/harness suites
  still end in passing suite exits. Logs: `build/edit-g02-check-headless.log`,
  `build/edit-g02-check-headless-final.log`, `build/edit-g02-check-remaining.log`,
  `build/edit-g02-check-tail.log`.
- `FILE_KILL_CANCEL_ONLY=1 build/san/tests/file_kill_test`: its headless
  cancellation checks PASS, also recorded in the filtered run.
- `build/san/tests/editor_test --headless`: PASS, including the bead's regressions
  and existing typing-allocation/latency checks. Log:
  `build/edit-g02-editor-headless.log`.
- Headless editor close, hot exit, session and scrolling suites: PASS. Log:
  `build/edit-g02-editor-other-headless.log`.
- `build/san/tests/editor_p4_test --headless`: PASS with approved local Unix-socket
  access after the sandbox denied IPC setup. Log: `build/edit-g02-p4-headless.log`.
- `sh tools/test_replay_cli.sh build`: PASS in `build/edit-g02-check-tail.log`.
- The display-lock shell contract waited behind the shared display lane; its own
  run was interrupted and left deferred for the coordinator. No other runner was
  interrupted and no shared lock was removed.

LeakSanitizer's exit scan fails under the sandbox tracing setup with
`LeakSanitizer does not work under ptrace`. Leak detection was disabled for these
sandbox sanitizer runs; ASan and UBSan remained enabled. Leak verification remains
for the coordinator's normal environment.

## Benchmarks

Existing keys benchmark PASS. (M)[AC] Its existing event stream measures
monotonic lookup elapsed times and converts nanoseconds to microseconds; no new
sample count or limit was chosen. Output in `build/edit-g02-keys-bench.log`:

```text
POWER status=Not charging [AC] load1=27.84; keys measurements TRACK only
BENCH keys_lookup TRACK n=1000000 p50=0.069 us p99=0.155 us (M)[AC] load1=27.84 no limit (no derivation for 1 us)
STREAM (M)[AC] load1=27.84 matches=436904 prefixes=250596 cancelled=125596 misses=186904 mallocs=0 guard=active gate_mallocs=0(G)
```

`build/bench/editor_bench --font-baseline='DejaVu Sans Mono'`: PASS. This existing
entry point exercises only null rendering for typing and tab switching. It uses
the existing sample counts/viewport/font fixtures; elapsed clocks and order
statistics produce its timings (M)[AC]. Log: `build/edit-g02-editor-bench.log`.
Typing and tab switching both reported no allocations. This is a tracked
measurement, not a new performance requirement.

The typing row missed its inherited tracked targets under the recorded load.
No before-change benchmark was taken, so this run cannot attribute that miss
to this bead. (M)[AC] Nanoseconds below are the existing benchmark's elapsed
clock/order-statistic output; target values come from its existing perf row:

```text
BENCH name=editor_null_serial_injection_T4_G1 n=10000 required_n=10000 p50=1158741 p99=2249506 ci95_p50=[1157745,1160098] ci95_p99=[2240057,2261497] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=5.43 verdict=TRACK
```

## Found outside scope; not fixed

`journal_kill_test` fails crash replay. Observed (M)[AC]:

```text
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
```

The IPC suite first hits the sandbox's Unix-socket restriction. With approved
socket access, its launcher state and file-change notification assertions fail:
`main_launcher_primary`, `main_launcher_isolated`, `astra_many_open` and
`astra_many_open_local`. All remaining named IPC cases pass with socket access.
Logs: `build/edit-g02-ipc-unsandboxed.log`, `build/edit-g02-ipc-remaining.log`.

To establish independence from this bead, a copy of the sanitizer library had
only its findpanel and keys objects replaced by objects compiled from the exact
pre-edit source copies. Unchanged journal/IPC test objects were relinked against
that library. The same journal failure and all the named IPC failures reproduced.
No production source was reverted or edited during this check. Logs:
`build/edit-g02-baseline-build.log`, `build/edit-g02-baseline-journal.log`,
`build/edit-g02-baseline-ipc.log`.

`file_kill_test` is absent from the Makefile's display classification, but its
viewport and queued probes call `view_init`, which opens X and a CPU renderer.
Those portions fail in the sandbox; its available headless cancellation mode
passes. Classification was not changed in this bead.

The cited `docs/reviews/astra-vs-sublime-3.md` is absent from this worktree.
The supplied brief's behavior was used; no missing review was reconstructed.

## What remains

The local bead implementation and red/green regressions are complete. Full
`make check` is **not** green: the unrelated journal/IPC failures above need
coordinator triage. Display suites fail inside this sandbox for lack of the
X socket; the coordinator must rerun them on Xvfb :99, along with the deferred
lock contract and leak verification.

Observed display diagnostic (M)[AC]; :99 is the coordinator's supplied display
identifier, and the error value is the X connection result:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/refwin_test
refwin_test: measured period arithmetic and 90/60 Hz pair rejection PASS
diagnostic-test: window=0x00000000 connect DISPLAY=:99: connection error=1
exit=1
```

The reference test's pair-rate numbers above come from its existing measured
period arithmetic/protocol fixture (P/M), not a new rate target. Its earlier
invalid-pair diagnostics are retained in `build/edit-g02-display.log`.
