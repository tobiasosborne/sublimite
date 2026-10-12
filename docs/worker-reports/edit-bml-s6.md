# edit-bml worker report

Implemented destination-style clipboard normalization. File-backed buffers use
the file detector's dominant ending; in-memory buffers cache the same detector's
result at creation so cutting all text retains the style. LF and CRLF inputs
normalize to that ending; lone CR, UTF-8 and invalid bytes remain unchanged.
Receive staging and the existing arena reservation account for CRLF expansion.
Small and worker-sliced paste share the same normalizer, including PRIMARY.

Files touched:

- `src/editor/clip.c`: capture destination ending, normalize LF/CRLF, reserve
  expanded staging and preserve its capacity through completion/cleanup.
- `src/editor/private.h`: cached detected ending for in-memory buffers.
- `src/editor/buffers.c`: detect the initial in-memory prefix before typing.
- `tests/editor_test.c`: current-line copy/cut/paste, undo/redo, byte-exact save,
  mixed-ending destination cases, empty-buffer retention, and bulk expansion
  across a worker-slice boundary; update old expectations that asserted the bug.
- `docs/decisions/edit-bml.md`: design and derivations.
- `docs/worker-reports/edit-bml-s6.md`: this report.

All captured runs below are (M)[AC]: `/sys/class/power_supply/BAT0/status`
reported `Not charging`, mapped to AC by HANDOFF's power convention. Exit
statuses and assertion source coordinates are observed run results. Fixture
lengths and offsets derive from the literal byte sequences (P); work bounds
reuse existing module constants. Compiler versions and display identifiers
are the supplied toolchain and coordinator destination, not invented quantities.

Red, before changing production code:

```text
$ make CC_RELEASE=gcc-13 CC_SAN=clang-18 build/san/tests/editor_test
[build passed]
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --clipboard-crlf
editor_test:74: FAIL editor_length(e) == n
editor_test:3281: FAIL expect(e, pasted, sizeof pasted - 1u) == 0
exit: 1
```

Supplementary red: paste-time detection lost the in-memory style after cutting
the sole CRLF line. The failing test preceded the cached-style change:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --clipboard-crlf
editor_test:74: FAIL editor_length(e) == n
editor_test:3347: FAIL expect(e, original, sizeof original - 1u) == 0
edit-bml: CRLF current-line copy/cut/paste, undo/redo, byte-exact save passed
edit-bml: small mixed paste follows CRLF/LF, no-ending fallback, majority/tie passed
exit: 1
```

Green release build and regressions:

```text
$ make CC_RELEASE=gcc-13 CC_SAN=clang-18 all
exit: 0
$ build/tests/editor_test --clipboard-crlf
edit-bml: CRLF current-line copy/cut/paste, undo/redo, byte-exact save passed
edit-bml: small mixed paste follows CRLF/LF, no-ending fallback, majority/tie passed
edit-bml: in-memory CRLF style survives cutting all text passed
edit-bml: bulk LF expansion, split CRLF, lone CR/invalid UTF-8, undo/redo passed
exit: 0
$ build/tests/editor_test --clipboard
clipboard malloc guard: 0 allocations (active); expected zero by law
clipboard: null round trip, UTF-8/CRLF, replacement, cut/undo, alternate keys, PRIMARY passed
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
clipboard middle click: deep horizontal position exact
clipboard PRIMARY: queued selection/collapse/middle paste waits for latest capture, mallocs=0 passed
clipboard middle click: no active tab preserves placeholder
exit: 0
```

Green ASan/UBSan regression run, after the final production changes:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --clipboard-crlf
edit-bml: CRLF current-line copy/cut/paste, undo/redo, byte-exact save passed
edit-bml: small mixed paste follows CRLF/LF, no-ending fallback, majority/tie passed
edit-bml: in-memory CRLF style survives cutting all text passed
edit-bml: bulk LF expansion, split CRLF, lone CR/invalid UTF-8, undo/redo passed
exit: 0
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --clipboard
clipboard malloc guard: 0 allocations (ASan-inert); expected zero by law
clipboard: null round trip, UTF-8/CRLF, replacement, cut/undo, alternate keys, PRIMARY passed
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
clipboard middle click: deep horizontal position exact
clipboard PRIMARY: queued selection/collapse/middle paste waits for latest capture, mallocs=0 passed
clipboard middle click: no active tab preserves placeholder
exit: 0
```

Sanitizer suites ran serially, with leak detection disabled for the environment
reason below. Relevant results:

| Invocation | Result |
|---|---|
| `editor_test --clipboard-crlf` | Pass |
| `editor_test --clipboard` | Pass |
| `editor_session_test` | Pass |
| `editor_p4_test --73j-null` | Pass: indentation/CRLF undo, queued groups, tabs, wrap, minimap, index recovery |
| `unicode_render_test` | Pass |
| `journal_test` | Pass |
| `editor_close_test --headless`, `editor_hot_exit_test`, `editor_scroll_test` | Pass |
| `editor_test --headless` | Fails in the unrelated find-panel fault-injection test, before clipboard regressions |
| `editor_p4_test --headless`, `editor_large_test`, `ipc_test` | Local Unix socket initialization denied by sandbox |
| `file_kill_test` | Its viewport and queued viewport helpers require X; initialization fails |
| `journal_kill_test` | Unrelated replay failure, reproduced in an isolated rerun |

The serial driver also ran all remaining module/pure suites, including the
pure GL and X11 tests and the mocked Wayland suites. They passed. Complete
per-invocation exit statuses are in `build/edit-bml-s6/suite-status.log`; full
output is in `build/edit-bml-s6/serial.log`. The reproducible serial driver is
`build/edit-bml-s6/run-serial.py`. These are verification artifacts in this
worktree; the required red and green evidence is pasted here independently.

Full `make check` built all sanitizer binaries, passed `base_test`, then stopped
at the occupied display lane. The initial attempt was interrupted while waiting
for that lane; the final attempt used nonblocking lock admission (P: zero wait,
not a timing budget):

```text
$ EDIT_DISPLAY_LOCK_TIMEOUT=0 ASAN_OPTIONS=detect_leaks=0 make CC_RELEASE=gcc-13 CC_SAN=clang-18 check
base_test: PASS
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test: display lock timeout: display :99 (held by unknown)
make: *** [Makefile:131: check] Error 1
exit: 2
```

The editor benchmark was attempted through `tools/with_display_lock.sh` with
the existing `--track` arguments and the same nonblocking admission. It also
stopped at the occupied display lock. No benchmark measurement is claimed.

Environment limitations:

- The sandbox cannot reach the X socket. `xdpyinfo -display :99` reports
  `unable to open display ":99"`. Display verification remains for the
  coordinator on Xvfb at the supplied `:99` destination. The shared display
  lock was also occupied, so full check and the benchmark could not enter it.
- A direct local Unix socket bind probe in a temporary directory under `build`
  returns `AF_UNIX bind failed: [Errno 1] Operation not permitted`. This explains
  the non-display IPC initialization failures; those suites need a coordinator
  rerun where local socket binding is allowed.
- LeakSanitizer fails at exit even on a passing regression with
  `LeakSanitizer does not work under ptrace (strace, gdb, etc)`. Runs use
  `ASAN_OPTIONS=detect_leaks=0`; ASan and UBSan remain enabled. Leak detection
  needs a coordinator run outside this sandbox.

Outside-scope findings, reported without fixes:

- `editor_test --headless` fails at `tests/editor_test.c:3856`, asserting
  `!e->find_replace_active && e->find_error==FINDUI_ERR_UNDO`. The isolated
  `editor_test --73j-find` rerun fails at the same assertion. This test injects
  a piece allocator failure into find replacement; it does not paste text.
  The find-panel sources were left unchanged for the worker handling that area.
- `journal_kill_test` fails identically on an isolated rerun:
  `trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0` (M)[AC],
  observed progress/replay fields from the test. Journal/session/recovery sources
  were left unchanged for their assigned workers.
- The file kill-test viewport failure is at `tests/file_kill_test.c:198`;
  `bench/file_bench.c:304` calls `plat_init` in that helper. This is display
  coverage despite the suite's filename and its omission from the Makefile's
  display-test classification. No Makefile or file-module changes were made.

What remains: coordinator display/benchmark and leak-enabled verification,
IPC suites outside the socket restriction, and triage of the unrelated
find-panel and journal kill-test failures. Full non-display verification is
therefore **not wholly green**; the edit-bml regression and applicable passing
subsets are green. The bead implementation is complete, with no additional
source work requested or performed. No `bd` or `git` commands were run, and
the protected coordinator documents were not edited.
