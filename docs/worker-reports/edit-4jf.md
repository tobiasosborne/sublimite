# edit-4jf — refwin trace event compatibility

Completed the requested scope. Read CLAUDE.md laws 3, 4 and 9 before implementation. No `git` or `bd` commands were run; `src/trace` and the Makefile were not changed.

## Change

`tools/refwin_pairs.py` now reads `src/trace/trace.h`'s `enum trace_ev` at runtime, resolving implicit increments and explicit decimal/octal/hexadecimal integer literals, including C suffixes and comments. The header path is relative to the script, so invocation from another working directory works. `TRACE_EV_COUNT` is excluded from the valid event set.

Every record still undergoes event-ID, thread and nonzero-timestamp validation. Only named dequeue and Present endpoint events are retained for pairing; composition, startup and subsequently appended unpaired events are discarded. Unknown IDs and gaps remain errors. Existing frame identity and endpoint completeness checks remain in place.

Added `tests/refwin_pairs_test.py` and the `tests/trace_pairs_test.c` launcher. Makefile's existing glob includes the launcher in `all` and `check`, outside its display-lock lane. Coverage includes the CLI startup regression, legacy pipeline records, every currently unpaired event, a hypothetical sparse enum append, rejected enum gaps/sentinel/unknown IDs, wrong threads, zero timestamps and truncated records. The hypothetical header is a temporary copy; no trace source is edited.

## Red

Power status was read from `/sys/class/power_supply/BAT0/status`: `Not charging`; native test preflight reports this as `[AC]`. Results below are execution observations (M)[AC], not performance claims. Protocol IDs and layout constants derive from the task and trace headers. Synthetic fixture timestamps and expected differences are derived in the test comments.

The Makefile-style refwin invocation initially encountered the shared display lock. Subsequent sandbox attempts reached the test and failed to connect to Xvfb `:99`; a direct Unix-socket probe reported `PermissionError: [Errno 1] Operation not permitted`. Used the requested trace-file fallback for the red run, before changing the tool:

```sh
python3 -B tests/refwin_pairs_test.py TraceJoinTest.test_cli_startup_records
```

The test constructs an EDTRACE1 dump containing IDs `10..18`, both with frame zero and with the paired frame, plus valid dequeue/endpoint records. It directly executes `tools/refwin_pairs.py` with injection CSV and output paths. Observed exit status: failure.

```text
test_cli_startup_records ... FAIL
AssertionError: 1 != 0 : refwin_pairs: FAIL /tmp/tmpoepcbfzn/editor.trace: invalid trace record
FAILED (failures=1)
```

## Green

The same startup regression passed after implementation. The complete Python regression suite and its Makefile-discovered sanitizer launcher passed:

```sh
python3 -B tests/refwin_pairs_test.py
DISPLAY=:99 build/san/tests/trace_pairs_test
```

```text
test_all_unpaired_events_are_skipped ... ok
test_cli_startup_records ... ok
test_future_enum_events_and_gaps ... ok
test_legacy_pipeline ... ok
test_malformed_records_still_fail ... ok
OK
```

Built the requested native suites through their Makefile targets:

```sh
make build/san/tests/refwin_test build/san/tests/refwin_timeout_test build/san/tests/trace_test
make build/san/tests/trace_pairs_test build/san/tests/replay_test build/san/tests/startup_test build/san/tests/refproto_test
```

The sandbox also prevented LeakSanitizer's thread inspection. Final native verification ran outside the sandbox with default ASan/UBSan/LeakSanitizer settings and headless Xvfb only. The refwin command uses the same display-lock wrapper as `make check`; inherited lock ownership covers the nested refwin invocation:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 sh tools/with_display_lock.sh sh -c 'build/san/tests/refwin_test && build/san/tests/refwin_timeout_test && build/san/tests/refproto_test'
DISPLAY=:99 sh -c 'build/san/tests/trace_test && build/san/tests/trace_pairs_test && build/san/tests/replay_test && build/san/tests/startup_test'
make -j2 all
```

All commands exited successfully (M)[AC]. Relevant suite output:

```text
refwin_test: PASS 12 editor/reference pairs, complete monotonic CSV, incomplete/wrong-frame runs rejected (Xvfb phase approximate)
refwin_timeout_test: no-input timeout names missing KeyPress; CSV contains only header PASS
refproto_test: zero-clock bootstrap, stable 60/90 Hz, rate change, future/stale/zero clocks PASS
trace_test: PASS
OK
replay_test: PASS
startup_test: startup stamps survive frame-ring wrap and round-trip; reset clears them
```

Counts and clock labels in these excerpts are existing suite outputs, derived from their fixture definitions. The refwin suite's intentional incomplete-run and wrong-frame rejection diagnostics appeared as expected; its overall exit was successful. Reading the actual `build/startup.trace` produced by `startup_test` also succeeded, retaining only pairing events.

Ran the scoped refwin/trace suites rather than the entire `make check` driver. Full command logs are in `build/edit-4jf/`, including `red-pairs.log`, `green-pairs.log`, `green-refwin-native.log`, `green-trace-native.log` and `make-all.log`.

No outstanding work within this bead. Stopping here.
