# edit-63v — session 8 worker report

Scope: `tests/cli_test.c` failure cleanup and window discovery under load.
Read CLAUDE.md laws 1–9, the CLI test, HANDOFF.md, and PLAN.md workflow.
No git or bd commands; no production source or Makefile changes.
Worker documents stay in this report and the bead decision document, following
HANDOFF.md's session-8 instruction that workers leave HANDOFF/worklog alone.

Environment: DISPLAY=:99 throughout; existing Xvfb :99 was available outside
the sandbox through its abstract Unix socket. No real-display connection.
Power: BAT0 reported `Not charging`; AC/online was `1`, hence [AC].
Initial load1 was 0.46 (M)[AC]; forced-timeout verification load1 was 1.14
(M)[AC]. Sandbox socket/ptrace restrictions required approved unsandboxed
X11/IPC and LeakSanitizer runs. Full-suite ASAN_OPTIONS remained
`detect_leaks=0` as requested.

## Red

First added only `CLI_TEST_FORCE_WINDOW_TIMEOUT`, setting the original window
deadline to now, leaving the original early-return assertions and cleanup
unchanged. Built with:

```sh
DISPLAY=:99 make -j8 build/san/tests/cli_test
DISPLAY=:99 CLI_TEST_FORCE_WINDOW_TIMEOUT=1 ASAN_OPTIONS=detect_leaks=1 build/san/tests/cli_test
```

Exit 1; reproduced the reported leak exactly, 29,484 bytes in four allocations
(M)[AC]:

```text
cli_test:52: FAIL window != XCB_WINDOW_NONE
cli_test:80: FAIL session(file, dump, false) == 0
ERROR: LeakSanitizer: detected memory leaks
Direct leak of 21176 byte(s) in 1 object(s)
  xcb_connect_to_fd
  xcb_connect_to_display_with_auth_info
  session tests/cli_test.c:40:27
Indirect leak of 8260 byte(s) in 1 object(s)
Indirect leak of 32 byte(s) in 1 object(s)
Indirect leak of 16 byte(s) in 1 object(s)
SUMMARY: AddressSanitizer: 29484 byte(s) leaked in 4 allocation(s).
```

The initial sandbox attempt could not connect and LeakSanitizer reported its
ptrace limitation; the red evidence above came from the approved run outside
those restrictions. The existing Xvfb was used; attempted startup of another
:99 server did not succeed or replace it.

## Changes

- Assertions now use a single cleanup exit per function. Session cleanup frees
  the XCB title reply, trace records, streams, and connection, then kills/reaps
  an unreaped child. `waitpid` retries EINTR. Main also closes its directory and
  stream and removes partial fixtures after skip/failure.
- Window polling has a monotonic 10–50-second normal budget (E), scaled as
  `10 + 2 * floor(min(load1, 20))` seconds (E). Missing load data falls back to
  10 seconds (E). Poll interval is 10 ms (E).
- Timeout prints `SKIP: window not created within N s (load1=X)` and propagates
  skip to main with exit 0 only for a live, non-stopped child and healthy XCB
  connection. Exited/signaled/stopped children fail. The zero-budget probe
  explicitly labels its diagnostic as forced.
- Removed the alarm that could bypass cleanup; WM_DELETE_WINDOW child waiting
  is also bounded by a deadline. All existing normal-session assertion
  conditions remain, including title identity, dump-error exit status,
  nonempty trace, two journals, journal permissions/size, and fixture removal.
- Retained deterministic timeout injection, plus live-child assertion,
  early-exit, and stopped-child probes. See `docs/decisions/edit-63v.md`.

## Green and gate result

Final release/sanitizer compilation passed with the repository warning flags.
`DISPLAY=:99 make all` exited 0; the final repeat reported nothing to rebuild.

Each final CLI probe ran with `DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1` outside
the ptrace sandbox. A Python subprocess runner asserted expected exit codes,
expected messages, and absence of LeakSanitizer/AddressSanitizer diagnostics:

| Probe environment | Exit | Observed result |
|---|---:|---|
| None | 0 | Both sessions and all existing assertions passed |
| `CLI_TEST_FORCE_WINDOW_TIMEOUT=1` | 0 | Healthy-child SKIP; no leaks |
| `CLI_TEST_FORCE_ASSERT_FAILURE=1` | 1 | Deliberate assertion FAIL; no leaks |
| `CLI_TEST_CHILD_EXIT=1` | 1 | Early exit status 23 rejected; no leaks |
| `CLI_TEST_CHILD_STOP=1` | 1 | Stopped child rejected and killed/reaped; no leaks |

Final representative output:

```text
normal: exit=0, expected=0
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
CLI_TEST_FORCE_WINDOW_TIMEOUT: exit=0, expected=0
cli_test: SKIP: window not created within 0 s (load1=1.14) [forced test timeout]
CLI_TEST_FORCE_ASSERT_FAILURE: exit=1, expected=1
cli_test:94: FAIL !getenv("CLI_TEST_FORCE_ASSERT_FAILURE")
cli_test:137: FAIL ran == 0
CLI_TEST_CHILD_EXIT: exit=1, expected=1
cli_test: editor exited or stopped before window (status=5888)
CLI_TEST_CHILD_STOP: exit=1, expected=1
cli_test: editor exited or stopped before window (status=4991)
cli_test regression probes: PASS (leaks enabled)
```

The requested full gate **does not pass on this tree**:

```sh
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
```

Exit 2. CLI passes, then the unchanged `editor_close_test` fails:

```text
== build/san/tests/cli_test
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
== build/san/tests/editor_close_test
editor_close_test:42: RED editor_length(e) == 1 && e->op_count == 1
editor_close_test:72: RED close_case(false) == 0
make: *** [Makefile:101: check] Error 1
```

Running that untouched binary alone with the same display/ASAN_OPTIONS also
exited 1 with the identical assertion failure. A final make-check repeat
reproduced it. It is independent of the CLI translation unit; no change made
outside bead scope. An optional remaining-suite sweep was stopped and its owned
processes terminated; it is not claimed as full-suite validation. The
coordinator must resolve the inherited editor-close failure before this tree
can meet the full make-check acceptance gate.
