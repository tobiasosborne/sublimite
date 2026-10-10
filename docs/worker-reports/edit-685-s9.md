# edit-685 session 9 — P4.I-4 selection LIMIT contract

## Scope and result

Implemented the named finding: Enter/brace selection queries now finish before
selection deletion and before starting an undo group. A LIMIT rejection leaves
buffer bytes, cursor/anchor/preferred column, undo and redo groups/serial/records,
editor history ring, buffer revision and modified status unchanged. A repeated
synchronous key attempt sees that same state; one rejection is recorded for each
input command, without an internal retry loop.

The indent API gains pure `indent_on_enter_after_delete` and
`indent_on_close_brace_after_delete` queries. They omit the selection logically,
preserving successful post-deletion indentation semantics without mutation or
allocation. Existing query signatures and work bounds remain unchanged. Actual
successful selection deletion, dedent and insertion remain one undo group.

Production changes are restricted to indent call sites in `src/editor/input.c`
and the indent module. Tests are in `tests/editor_p4_test.c`,
`tests/indent_test.c`, and `fuzz/indent_fuzz.c`. No new globals or allocator calls.
No capacity/index-repair/IPC-drain code, dependency modules, Makefile, HANDOFF.md,
or worklog files were edited. Git was read-only; bd was never run.

Design choices are recorded in
[docs/decisions/edit-685.md](../decisions/edit-685.md).

## Red first

The null-backend tests were written and run against the inherited editor before
changing production behavior. Both commands deleted the selected byte despite
LIMIT. First-run assertion line numbers below predate subsequent coverage
expansion. Each focused command exited with a test failure (M)[AC].

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_p4_test --indent-limit-enter
editor_p4_test:94: FAIL editor_length(e) == sizeof initial
$ DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_p4_test --indent-limit-brace
editor_p4_test:94: FAIL editor_length(e) == sizeof initial
```

The new preview API was first wired to error-returning stubs. The unit test
compares every selection interval in short fragmented/CRLF fixtures with an
actual deletion on a separate ordinary tree. Its first valid preview failed
before implementing the API (M)[AC]:

```text
$ build/tests/indent_test
tests/indent_test.c:223: assertion failed: indent_on_enter_after_delete(t,lo,hi,actual,sizeof actual,&an)==want
```

## Green and coverage

The same focused commands and the complete release indent suite exit zero
(M)[AC]. Release counting guards are active; sanitizer guards are explicitly
inert. The regression retains populated undo and redo, tests forward LIMIT,
backward LIMIT with a reversed selection, and removal of a newline that joins
onto a long suffix. Each case retries the key synchronously and checks the
original bytes and history again, then proves redo remains usable.

Selections that discard a giant line and leave a short line still succeed;
Enter/brace each undo and redo atomically. Their release allocation guards
count zero allocations (M)[AC]. Preview tests also exercise output capacity,
cleared LIMIT results, mixed tabs/spaces, CRLF joins (including split rejection),
empty/full selections, fragmented pieces, and an inaccessible mapped omission.
The existing protected-page scan-bound tests remain green. The fuzzer compares
projected queries against separately assembled logical bytes and checks the
original tree remains unchanged, including synthetic long lines.

Pasted focused green run:

```text
editor_p4_test: null Enter selection LIMIT scenario=0 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null Enter selection LIMIT scenario=1 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null Enter selection LIMIT scenario=2 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null Enter giant selection shortening succeeds atomically mallocs=0 guard=active
editor_p4_test: null brace selection LIMIT scenario=0 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null brace selection LIMIT scenario=1 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null brace selection LIMIT scenario=2 and synchronous retry preserve bytes/history/cursor passed
editor_p4_test: null brace giant selection shortening succeeds atomically mallocs=0 guard=active
indent_test: pure selection previews match actual deletion including fragmented/CRLF joins passed
review 6: long-line typing/zero-capacity work bounded: ok
indent typing malloc guard: active, allocations=0
indent_test: all cases passed
```

## Validation environment and remaining acceptance

All display-backed commands use DISPLAY=:99 and EDIT_DISPLAY=:99. No command
opens a window on :0. Release is gcc 13; sanitizers/fuzz use clang 18. Build flags
include C11, -Wall -Wextra -Werror -Wshadow -Wconversion. LeakSanitizer cannot run
inside this sandbox: every sanitizer/fuzz run uses ASAN_OPTIONS=detect_leaks=0.
The coordinator must rerun with leak detection enabled.

The first sandbox-only full check stopped at the CLI Xvfb connection preflight,
not an indent assertion. The authorized rerun uses access to the existing :99
socket; no source was changed for that environmental failure. Pasted failure:

```text
== build/san/tests/cli_test
cli_test:61: FAIL c && !xcb_connection_has_error(c)
cli_test:137: FAIL ran == 0
make: *** [Makefile:101: check] Error 1
```

Final-source `make all` exits zero (M)[AC], including gcc 13 strict warnings.
Final-source focused ASan/UBSan Enter, brace and indent tests exit zero (M)[AC].
A complete earlier sanitizer run exits zero (M)[AC]:

```text
check: 60 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

That suite included a prolonged raster-test wait. It completed before an
attempted owned-process cleanup; no test was terminated and no raster source
was changed. A separate terminal-logged retry overlapped its final tests and
stopped in CLI process validation with editor error -103 and:

```text
cli_test:108: FAIL WIFEXITED(status) && WEXITSTATUS(status) == (fail_dump ? 1 : 0)
cli_test:137: FAIL ran == 0
make: *** [Makefile:101: check] Error 1
```

No cause or fix is claimed for that CLI retry failure. The sequential
final-source check passed CLI and all scoped P4 regressions but stopped in the
existing source-watch test before any typing. Its stale-source detection uses
a fixed deadline; that path was not changed by this bead. Pasted failure:

```text
editor_test:724: FAIL editor_get_stats(e).source_stale
editor_test:1255: FAIL review_suite(NULL) == 0
make: *** [Makefile:101: check] Error 1
```

This out-of-scope source-watch failure was not fixed. The final-source full
confirmation run exited zero (M)[AC], using the original environment after the
prior runs had completed. All scoped and existing suites passed. Pasted final
build/check evidence:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 make -j6 all
make: Nothing to be done for 'all'.
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 timeout --foreground 600 make -j6 check
== build/san/tests/x11_xi2_test
x11_xi2_test: ok
check: 60 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

Both commands exited zero (M)[AC]. Missing acceptance: the sandbox cannot
perform leak-enabled checking, so the coordinator must rerun that configuration.
No scoped implementation work remains. Earlier CLI/source-watch failures are
retained above for coordinator triage, without changes to those paths.

Final-source editor and indent fuzz targets each exit zero (M)[AC], no ASan/UBSan
findings, for the requested duration of 60 seconds (G). BAT0 was Not charging
[AC], load1=6.63 (M)[AC] at launch. Pasted completion evidence:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/editor_fuzz -max_total_time=60 -max_len=1024 -timeout=10 -artifact_prefix=build/edit-685-editor-final-
Done 11612 runs in 61 second(s)
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/indent_fuzz -max_total_time=60 -max_len=8192 -timeout=10 -artifact_prefix=build/edit-685-indent-final-
Done 75716 runs in 61 second(s)
```

Run counts and durations above are (M)[AC]. Leak detection remains disabled;
the coordinator must rerun with leaks enabled. `git diff --check` is clean.

## Loaded-box performance evidence

All rows below are measurements (M)[AC], stamped by the bench. Baseline and
candidate were run back to back on the same loaded box. Gate references are
(G). These comparisons do not certify timing gates; no single noisy timing is
used as a pass/fail verdict.

The initial comparison showed an added per-byte reader call in the generated
release code. Its long-line measurements motivated a local reader refactor:
span refill handles mapping and byte consumption is explicitly inlined, using
the same compiler convention already present elsewhere in the repository.
Initial pair, retained for transparency:

```text
VARIANT baseline
POWER status=Not charging [AC] load1=10.55; indent measurements TRACK only
BENCH indent_on_enter viewport_rows=300 TRACK n=20000 p50=0.496 us p99=1.095 us (M)[AC] load1=10.55 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=10.55; indent measurements TRACK only
BENCH indent_bracket_match viewport_rows=300 TRACK n=20000 p50=0.180 us p99=1.972 us (M)[AC] load1=10.55 gate_p99<=20.000 us(G) pass=1
typing allocations=0 (M)[AC] load1=10.55 gate_allocations=0(G) guard=active
POWER status=Not charging [AC] load1=10.55; indent measurements TRACK only
BENCH indent_long_line_enter includes_zero_capacity TRACK n=20000 p50=12.243 us p99=16.741 us (M)[AC] load1=10.55 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=10.55; indent measurements TRACK only
BENCH indent_long_line_brace TRACK n=20000 p50=12.219 us p99=16.776 us (M)[AC] load1=10.55 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=10.55; indent measurements TRACK only
BENCH indent_detect file=log_1g.txt prefix<=64KiB TRACK n=2000 p50=186.896 us p99=195.563 us (M)[AC] load1=10.55 gate_p99<=1000.000 us(G) pass=1
VARIANT baseline exit=0
VARIANT fixed
POWER status=Not charging [AC] load1=10.55; indent measurements TRACK only
BENCH indent_on_enter viewport_rows=300 TRACK n=20000 p50=0.534 us p99=0.913 us (M)[AC] load1=10.55 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=10.55; indent measurements TRACK only
BENCH indent_bracket_match viewport_rows=300 TRACK n=20000 p50=0.176 us p99=1.962 us (M)[AC] load1=10.55 gate_p99<=20.000 us(G) pass=1
typing allocations=0 (M)[AC] load1=10.55 gate_allocations=0(G) guard=active
POWER status=Not charging [AC] load1=10.55; indent measurements TRACK only
BENCH indent_long_line_enter includes_zero_capacity TRACK n=20000 p50=16.629 us p99=39.133 us (M)[AC] load1=10.55 gate_p99<=20.000 us(G) pass=0
POWER status=Not charging [AC] load1=10.55; indent measurements TRACK only
BENCH indent_long_line_brace TRACK n=20000 p50=19.613 us p99=44.112 us (M)[AC] load1=10.55 gate_p99<=20.000 us(G) pass=0
POWER status=Not charging [AC] load1=10.55; indent measurements TRACK only
BENCH indent_detect file=log_1g.txt prefix<=64KiB TRACK n=2000 p50=77.223 us p99=105.298 us (M)[AC] load1=10.55 gate_p99<=1000.000 us(G) pass=1
VARIANT fixed exit=1
```

Final baseline/fixed pair after that refactor:

```text
VARIANT baseline
POWER status=Not charging [AC] load1=9.97; indent measurements TRACK only
BENCH indent_on_enter viewport_rows=300 TRACK n=20000 p50=0.718 us p99=0.822 us (M)[AC] load1=9.97 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=9.97; indent measurements TRACK only
BENCH indent_bracket_match viewport_rows=300 TRACK n=20000 p50=0.282 us p99=2.687 us (M)[AC] load1=9.97 gate_p99<=20.000 us(G) pass=1
typing allocations=0 (M)[AC] load1=9.97 gate_allocations=0(G) guard=active
POWER status=Not charging [AC] load1=9.97; indent measurements TRACK only
BENCH indent_long_line_enter includes_zero_capacity TRACK n=20000 p50=12.403 us p99=15.258 us (M)[AC] load1=9.97 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=9.97; indent measurements TRACK only
BENCH indent_long_line_brace TRACK n=20000 p50=12.398 us p99=14.967 us (M)[AC] load1=9.97 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=9.97; indent measurements TRACK only
BENCH indent_detect file=log_1g.txt prefix<=64KiB TRACK n=2000 p50=147.085 us p99=155.301 us (M)[AC] load1=9.97 gate_p99<=1000.000 us(G) pass=1
VARIANT baseline exit=0
VARIANT fixed-inline
POWER status=Not charging [AC] load1=9.97; indent measurements TRACK only
BENCH indent_on_enter viewport_rows=300 TRACK n=20000 p50=0.800 us p99=0.906 us (M)[AC] load1=9.97 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=9.97; indent measurements TRACK only
BENCH indent_bracket_match viewport_rows=300 TRACK n=20000 p50=0.295 us p99=2.668 us (M)[AC] load1=9.97 gate_p99<=20.000 us(G) pass=1
typing allocations=0 (M)[AC] load1=9.97 gate_allocations=0(G) guard=active
POWER status=Not charging [AC] load1=9.97; indent measurements TRACK only
BENCH indent_long_line_enter includes_zero_capacity TRACK n=20000 p50=6.408 us p99=15.336 us (M)[AC] load1=9.97 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=9.97; indent measurements TRACK only
BENCH indent_long_line_brace TRACK n=20000 p50=13.191 us p99=16.750 us (M)[AC] load1=9.97 gate_p99<=20.000 us(G) pass=1
POWER status=Not charging [AC] load1=9.97; indent measurements TRACK only
BENCH indent_detect file=log_1g.txt prefix<=64KiB TRACK n=2000 p50=65.211 us p99=157.469 us (M)[AC] load1=9.97 gate_p99<=1000.000 us(G) pass=1
VARIANT fixed-inline exit=0
```

Both final variants report active typing guards with zero allocations (M)[AC].
No real-display latency certification or unrelated benchmark matrix was added.
Other inherited editor gaps remain outside this bead. Leak-enabled verification remains required. The earlier CLI/source-watch
failures are recorded for coordinator triage; no cause or fix is claimed here.
Timing certification, if required, remains a coordinator decision.
