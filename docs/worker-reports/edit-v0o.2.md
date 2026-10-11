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
