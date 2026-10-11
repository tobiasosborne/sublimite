# edit-v0o.2 worker report — partial, source acquisition blocked

This bead is not complete. Only the highlight mailbox/plain-text readiness
contract and refresh-budget helper are implemented. Tree-sitter is not
vendored; there is no parser worker, grammar, incremental tree editing, or
highlighting integration. No parser-busy typing or tab-switch result is claimed.

## Changed files

- `src/hl/hl.h`, `src/hl/hl.c`: refresh-derived slice-budget contract, pointer-free
  mailbox encoding/decoding, generation/range/viewport readiness checks.
- `tests/hl_test.c`: compile-first red test, stale/plain fallback, complete range
  coverage, unknown phase rejection, and allocation guard.
- `fuzz/hl_fuzz.c`: mailbox decoding/round-trip invariants; no parser fuzzing.
- `src/hl/STATUS.md`, `docs/decisions/edit-v0o.2.md`: partial status and design.
- `HANDOFF.md`, `docs/worklog/2026-10-11.md`, this report: worker handoff evidence.

The Makefile, editor headers/paint, and worker pool remain unchanged. The helper
module is statically included by the existing source glob. No guessed refresh
rate or memory limit is added. Budget derivation (P): one frame times the
caller-supplied refresh interval equals that interval in nanoseconds. Unknown
refresh is refused. No parser exists yet to enforce the slice deadline.

## Red run

Command: `make build/san/tests/hl_test`, before adding `src/hl/hl.h` or its
implementation. (M)[AC] The following is the captured compiler/make output; source
positions, language-version labels and error counts originate in that command.

```text
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -include tests/display_guard.h -MMD -MP -c tests/hl_test.c -o build/san/tests/hl_test.o
tests/hl_test.c:3:10: fatal error: 'hl/hl.h' file not found
    3 | #include "hl/hl.h"
      |          ^~~~~~~~~
1 error generated.
make: *** [Makefile:58: build/san/tests/hl_test.o] Error 1
```

## Green run

Command:

```sh
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g tests/hl_test.c src/hl/hl.c src/base/malloc_guard.c -o build/hl-evidence/hl_test_san
ASAN_OPTIONS=detect_leaks=0 build/hl-evidence/hl_test_san
```

(M)[AC] Captured output:

```text
hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard
```

The direct GCC release run also passed, with the real malloc guard active.
Its allocation assertion is the typing-path law: no allocation is permitted.
The Clang sanitizer guard is inert by the repository's existing design.
Leak detection is disabled by the established sandbox workflow in HANDOFF;
the coordinator must rerun with leaks enabled.

The separately built ASan/UBSan protocol fuzzer passed its seeded corpus.
`-runs=0` is libFuzzer's corpus-initialization-only control, not an invented
mutation count or duration. This does not establish parser correctness.

## Repository checks

- `make all`: passed with GCC release and the required warning flags. The
  complete initial compile is in `build/hl-evidence/make-all-initial.log`;
  the final build is in `build/hl-evidence/make-all.log`.
- `ASAN_OPTIONS=detect_leaks=0 EDIT_DISPLAY_LOCK_TIMEOUT=0 make check`: failed
  at the CLI suite's shared display lock. The lock setting selects immediate,
  nonblocking acquisition; zero is the no-wait control, derived from that
  locking behavior. No arbitrary wait duration was introduced. A prior check
  was interrupted while waiting for the same occupied lock.
- Every suite in `tests/*_test.c` was accounted for. Suites after make's early
  stop were invoked individually with the same sanitizer environment and
  display guard. Already completed make-check results were reused. Per-suite
  process exits and raw power status are in
  `build/hl-evidence/suites/results.json`; output is beside it.
- Headless suites pass except `ipc_test`, whose Unix socket creation is denied
  by the sandbox. `file_kill_test` also fails, but its failing sections open X.
  No all-green repository check is claimed.
- `sh tools/test_replay_cli.sh build`: passed. The display-lock shell contract
  requires the reference window/display and remains for the coordinator's run.
- The Makefile-built highlight test passes in both sanitizer and release
  configurations. The release configuration runs the real allocation guard.

Power evidence: `BAT0/status` was read before validation and each suite and is
recorded as Charging/Not charging; `AC/online` reports the adapter connected
(the driver boolean is recorded in `build/hl-evidence/ac-online.txt`). Command
outputs and test statuses here are (M)[AC], derived directly from command
results and the per-suite JSON. No latency or throughput measurement is made.

The socket probe confirms the display restriction (M)[AC], reproduced exactly:

```text
X socket probe: PermissionError(1, 'Operation not permitted')
```

Suites blocked or failing for display access, plus the separate socket failure:

| Suite | Observed reason |
|---|---|
| `cli_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `editor_close_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `editor_large_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `editor_p4_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `editor_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `file_kill_test` | X-dependent `view_init` setup and its child checks fail; see outside-scope finding below. |
| `gl_gate_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `gl_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `ipc_test` | Unix socket initialization denied (`Operation not permitted`). |
| `raster_test` | X setup/connection fails in the sandbox. |
| `refproto_test` | X setup/connection fails in the sandbox. |
| `refwin_test` | X setup/connection fails in the sandbox. |
| `refwin_timeout_test` | X setup/connection fails in the sandbox. |
| `x11_alignment_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `x11_clip_review_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `x11_clip_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `x11_display_guard_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `x11_identity_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `x11_input_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `x11_live_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `x11_order_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `x11_stall_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |
| `x11_xi2_test` | Shared display lock occupied; nonblocking acquisition could not start this suite. |

Passing suites (M)[AC], derived from successful process exits in the JSON:

`base_test`, `file_test`, `find_bench_test`, `find_bestof_test`, `find_p1r_test`, `find_simd_filter_verify_test`, `find_test`, `find_timeout_test`, `findui_test`, `font_test`, `harness_test`, `hl_test`, `indent_test`, `journal_kill_test`, `journal_test`, `keys_test`, `layout_test`, `lineidx_test`, `minimap_test`, `piece_bounds_test`, `piece_mem_test`, `piece_mt_test`, `piece_test`, `prewake_test`, `render_test`, `replay_test`, `runtime_test`, `savectl_reload_test`, `savectl_test`, `scan_test`, `scroll_test`, `tabs_test`, `trace_test`, `undo_test`, `unicode_render_test`, `utf8_tables_test`, `utf8_test`, `view_test`, `work_foreground_test`, `work_test`, `wrap_test`.

The coordinator must rerun the full check outside this sandbox on its assigned
Xvfb display, with normal shared-lock acquisition and leaks enabled. Typing and
tab-switch acceptance with a busy parser remains unmeasured because no parser
worker exists.

## What remains in this bead

Continuation needs a faithful upstream source archive staged in this
worktree. Shell download failed with `curl: (6) Could not resolve host:
codeload.github.com` (M: the observed curl DNS error status). Available web
retrieval returned cache misses for required headers and transformed some
source text, including collapsing lines and removing angle-bracket includes.
No transformed source has been substituted for an upstream vendor tree.
The attempted upstream tag is recorded in the decision document as a release
identifier, not a performance quantity.

The requested runtime, transitive headers and licence, static Makefile build,
worker arena allocator, viewport-first parsing, refresh-bounded resumable
slices, snapshot ownership/cancellation, `ts_tree_edit`, minimal test grammar,
real parser unit/fuzz tests, and parser-busy editor benchmark remain unfinished.
Editor painting must remain plain text until a matching viewport parse lands.
The coordinator's display rerun and next Astra drive remain necessary.

## Outside scope

The Makefile's display-test filter omits `file_kill_test`, although that test
includes `bench/file_bench.c` and calls its `view_init`, which calls `plat_init`
and maps a window. Its failed setup/child assertions therefore require X.
The display classification was reported, not changed. The observed IPC socket
restriction is also outside this helper implementation; its captured output is
(M)[AC], with values copied from the test and kernel error reporting:

```text
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
```

No unrelated source was changed. No `bd` or `git` command was run. Source and
report changes are confined to this worktree; test fixtures used the existing
harness's temporary storage.

## Continuation — official runtime supplied and implementation finished

This section supersedes the acquisition block and unfinished implementation
list above. The coordinator supplied the official `v0.27.1` release archive.
The requested runtime/module code is implemented; display-dependent G1/G3,
full repository and leak acceptance still require coordinator validation.
No bead closure, visible highlighting or comparison-drive result is claimed.

### Provenance and build

`sha256sum /tmp/ts-dl/tree-sitter-v0.27.1.tar.gz` returned:

```text
982cd3d4d9eb7be18c243240a622423fd2e4ecb4bec2cd98832c2f3fa1f0f333
```

Hash and release tag are source identifiers. The decision document records
both. Extraction selected only the root `LICENSE`, `lib/src/`, and
`lib/include/tree_sitter/`, retaining upstream layout under
`vendor/tree-sitter/`. Every selected file was compared byte-for-byte against
its archive member; the manifest also rejects missing/extra files
(`build/hl-evidence/vendor-manifest.log`). No upstream source was rewritten,
no fixture directory was extracted, and no system shared runtime is used.

The Makefile builds the `lib/src/lib.c` amalgamation into each project static
archive, using the project's C11 release/sanitizer/fuzzer flags. A strict
compile first demonstrated upstream conversion diagnostics, including
`-Werror=sign-conversion` in UTF decoding and narrowing conversions in node
code (`build/hl-evidence/vendor-strict.log`). Only that vendored object's
`-Wconversion` and `-Wshadow` are relaxed. `-Wall -Wextra -Werror` remain on
it; every project source/test/fixture/fuzzer retains all requested warnings.

### Implemented behavior and changed files

- `src/hl/hl.h`, `src/hl/parse.c`: persistent worker-owned parser and completed
  trees; allocation-free request initialization; borrowed immutable piece
  snapshots and ordered edit arrays; generation-controlled incremental reuse
  with `ts_tree_copy`/`ts_tree_edit`; refresh-bounded cooperative slices and
  `work_continue`; cancellation/reset; read/backing-fault failure; pending
  mailbox publication with viewport-before-document ordering.
- Existing `src/hl/hl.c` mailbox encoding, readiness and refresh helper remain
  intact. Results still contain only byte ranges and publication phase,
  with snapshot generation in the worker transport. No tree crosses the
  mailbox. Plain text is the readiness fallback. Editor paint is unchanged.
- `tests/hl_fixture.h`: a handwritten Unicode-unit grammar; no upstream grammar
  was copied. It is used only in tests, fuzzers and the benchmark.
- `tests/hl_parse_test.c`: real viewport/document parsing, pending-result retry,
  original/source lifetime, snapshot isolation, insertion/deletion chains,
  intact prior tree, and the release malloc guard around request setup.
- `tests/hl_worker_test.c`: deterministic slice expiry/resumption, cancelled
  suspended parse, fresh restart, invalid edit failure, actual worker-pool
  publication ordering, empty input and mapped-file truncation failure.
- `fuzz/hl_edit_fuzz.c`: replacement edits plus viewport selection, before/after
  snapshots that survive source destruction, and differential comparison of
  every node's symbol, byte range and points against a fresh parse. The
  existing `fuzz/hl_fuzz.c` mailbox fuzzer remains.
- `bench/hl_bench.c`: existing editor G1/G3 endpoints under continuous parsing
  on a dedicated bulk lease; explicit supplied refresh interval; no paint
  integration. See the allocation-counter limitation below.
- Decision, module status, worklog and HANDOFF record the continuation.

Budget derivation (P): a frame is one refresh interval, so a supplied interval
`R_ns` yields `budget_ns = 1 frame * R_ns/frame = R_ns`. Unknown refresh is
refused. CLOCK_MONOTONIC elapsed time includes setup; parsing stops at the next
upstream progress callback after expiry. Callback granularity, token/scanner
work, tree operations, reads and scheduling can overshoot. The upstream
callback operation constant comes from its source, not an invented time cap.
The logical clock in tests advances one tick per check and uses a budget of
one tick to force resumptions; it does not measure physical display timing.

Snapshots and edit arrays must survive physical lease acknowledgement,
including cancellation. Cancelling a queued job can bypass the worker, so the
caller discards retained request continuation state before worker reuse or
destruction. Worker parsing/lifecycle use upstream allocation; typing-side
setup and mailbox/readiness calls allocate nothing. No global allocator
replacement or worker heap operation is introduced on input-to-submit.
The page-sized input buffer follows the measured Linux page size
(`getconf PAGESIZE`, `build/hl-evidence/page-size.txt`); it is storage, not a
source-size/performance bound. Source sizes above the uint32 tree-sitter byte
ABI are refused and retain plain text.

### Red and green evidence

The real-parser test was added before `parse.c`. The initial Makefile test
link failed with undefined references to `hl_worker_init`, `hl_request_init`,
`hl_parse_slice`, and publication/lifecycle functions. Captured red output is
in `build/hl-evidence/red.log`. The handwritten grammar was then corrected
and the real incremental test passed; no upstream parser source was patched.

Final release and ASan/UBSan builds both passed all highlight suites (M)[AC]:

```text
hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard
hl_parse_test: PASS viewport-first, snapshot lifetime, incremental edit, typing guard
hl_worker_test: PASS slice resume, cancellation, restart, failure, pool ordering, empty/faulted parse
```

Commands/build evidence:

```sh
make -j2 all build/san/tests/hl_worker_test build/san/tests/hl_parse_test build/san/tests/hl_test build/fuzz/hl_edit_fuzz build/fuzz/hl_fuzz
build/tests/hl_test
build/tests/hl_parse_test
build/tests/hl_worker_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/hl_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/hl_parse_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/hl_worker_test
```

`build/hl-evidence/final-build.log` contains the successful final Makefile
build. Earlier build/red/strict logs remain alongside it. Release executes
the repository's real allocation interposition; ASan uses its inert guard.

The final ASan/UBSan edit fuzzer completed its seed/mutation smoke run with no
model mismatch or sanitizer report (M)[AC]:

```sh
ASAN_OPTIONS=detect_leaks=0 build/fuzz/hl_edit_fuzz -runs=256 -artifact_prefix=build/hl-evidence/ build/hl-evidence/edit-final-corpus
```

Derivation of the smoke exploration count: uint8 selector cardinality is
`2^8 = 256`; this selects a smoke-run budget, with no claim of exhaustive
selector coverage. Multiline/UTF-8/invalid/NUL/empty/cache-boundary seeds are
explicit fixtures. The successful final log is
`build/hl-evidence/edit-fuzz-verified.log`. Longer exploratory runs were
interrupted and are retained as `edit-fuzz.log` and `edit-fuzz-final.log`;
those are not completed campaigns. The mailbox seed-corpus check passed with
libFuzzer's `-runs=0` initialization-only control
(`build/hl-evidence/mailbox-fuzz-final.log`).

### Repository and environment validation

`make all` passed, including the new benchmark, tests and release runtime.
The final `ASAN_OPTIONS=detect_leaks=0 EDIT_DISPLAY_LOCK_TIMEOUT=0 make -j2
check` rebuilt sanitizer test binaries and stopped at the occupied CLI display
lock (`build/hl-evidence/make-check-final.log`). Zero selects nonblocking lock
acquisition, not an application timing budget.

All suites were separately accounted for after the earlier check stopped;
per-suite output, power stamps and exit statuses are in
`build/hl-evidence/suites-continuation/results.json`. The final mapped-fault
addition was then checked with the final highlight suites above. Nonzero
suite exits group as follows (M)[AC]:

| Suites | Observed limitation |
|---|---|
| `cli_test`, `editor_close_test`, `editor_large_test`, `editor_p4_test`, `editor_test` | Occupied shared display lock; nonblocking acquisition refused. |
| `file_kill_test` | X-dependent view setup/children failed, as in the prior report. |
| `raster_test`, `refproto_test`, `refwin_test`, `refwin_timeout_test`, `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`, `x11_stall_test` | Display connection or fixture initialization failed in the restricted environment. |
| `ipc_test` | Unix socket setup denied: `errno=1 (Operation not permitted)`. |

Every other suite exited successfully in that accounting run. Some graphics
suites exercise optional/headless branches; their successful exit does not
claim a live display drive. `sh tools/test_replay_cli.sh build` passed
(`build/hl-evidence/replay-continuation.log`).

The leak-enabled highlight probe failed with LeakSanitizer's environment error
(M)[AC], rather than a reported leak:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

The complete captured output is `build/hl-evidence/leak-probe.log`. The
coordinator must rerun leak-enabled tests outside the restricted environment.
Power before/through validation was Charging with AC online; raw power/load
readings are in `build/hl-evidence/power-continuation.txt`. No latency result
is derived from these test runs.

### Busy-parser G1/G3 and remaining acceptance

The endpoint harness builds and, with no refresh input, reports SKIP because
it cannot invent a rate. The attempted locked display run was blocked before
benchmark execution (`build/hl-evidence/busy-bench.log`). Its supplied
`11111111 ns` setup-probe value was (P) `floor(10^9 ns/s / 90 Hz)` from the
historical panel profile; it is not asserted to be the current display's
measured interval, and no timing row ran. The coordinator should substitute
the actual measured interval:

```sh
DISPLAY=:99 sh tools/with_display_lock.sh build/bench/hl_bench --refresh-ns=INTERVAL
```

This harness evaluates worker CPU contention using a dedicated pool. It does
not wire highlighting into paint. The existing allocator guard counts the
entire process, so its G1/G3 allocation totals include the allocating parser
worker; those totals cannot establish typing-thread allocation behavior.
The separate release highlight typing guards establish the module's
allocation-free UI API. Evaluate timing rows against an idle baseline and
use thread-scoped allocation evidence if measuring the integrated editor.
No G1/G3 regression verdict or highlighting-on acceptance is claimed here.

The coordinator still owns unrestricted full-check/leak/display validation,
the comparison drive, and any bead status change. Production grammars, queries,
theme scopes and paint wiring are outside this runtime request. No `git` or
`bd` command was run; all source/report edits remain in this worktree.
