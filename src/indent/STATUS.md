# P4.2 status — edit-457.2

Standalone module complete. Editor-loop and layout integration are deferred by
the coordinator's dependency change; no editor, main, layout, frozen header,
Makefile or other module was edited. No git or bd commands were run.

## Delivered

- `indent.h`, `indent.c`: pure queries with explicit return codes, byte offsets,
  caller-owned output storage, no hidden state and no lifecycle resources.
- Enter preserves leading SP/TAB bytes, with LF/CRLF preservation. Optional
  extra indentation after an opening delimiter is deliberately omitted.
- Brace dedent returns a pre-insertion replacement edit including `}`, so the
  caller can apply deletion and insertion in the same undo group.
- Bracket matching and trailing-whitespace queries read only their viewport
  windows. Quotes/comments are not parsed. Matching balances the selected type.
- Style detection examines at most the first 64 KiB through bytes or a snapshot;
  tab/space votes and GCD of space widths are documented heuristics.
- Unit tests, guarded typing tests, release corpus benchmark and libFuzzer
  byte-buffer oracles. Protected pages verify window/prefix access bounds;
  fragmented pieces and cross-block scans exercise the frozen piece API.
- `docs/decisions/P4.2.md`: design choices, Dependency change and exact
  Integration contract, including UI/worker ownership and undo grouping.

## Red / green evidence

The full unit test was first run against error-returning stubs:

```
tests/indent_test.c:23: assertion failed: indent_on_enter(t,cursor,out,sizeof out,&n)==INDENT_OK
```

Implemented release run:

```
indent typing malloc guard: active, allocations=0
indent_test: all cases passed
```

Final clang ASan/UBSan run:

```
indent typing malloc guard: ASan inactive, allocations=0
indent_test: all cases passed
check: 29 test binaries passed
test_replay_cli: all passed
```

Release `make all`: exit 0. `make check`: exit 0 with
`ASAN_OPTIONS=detect_leaks=0`. `make fuzz`: exit 0, `fuzz: 16 fuzzers built`.
An initial overlapping sanitizer build had a stale object from before the
public edit struct was shortened; the final rebuild and full check above
resolved it. The release allocation guard is active; sanitizer interposition
is disabled by the existing base allocator guard.

## Fuzz evidence

Before the run: Charging [AC], load1=9.67. One completed run with the final
module, ASan/UBSan, raw and structured bytes and independent output oracles:

```
build/fuzz/indent_fuzz -max_total_time=120 -max_len=8192 -timeout=10
Done 449498 runs in 121 second(s)
```

449498 executions / 121 seconds (M)[AC] load1=9.67; no crash, assertion failure
or sanitizer error. LeakSanitizer was disabled for this sandbox.

## Benchmark evidence — TRACK only

One run, no retries for box load. Before measurement:
`BAT0/status=Full` [AC], `load1=12.17`. Existing corpus only; the large log is
mapped, never copied or regenerated. Snapshot creation and fixture setup are
outside timed regions. The benchmark exits nonzero on a gate miss.

```
BENCH indent_on_enter viewport_rows=300 TRACK n=20000 p50=0.396 us p99=0.666 us (M)[AC] load1=12.17 gate_p99<=20.000 us(G) pass=1
BENCH indent_bracket_match viewport_rows=300 TRACK n=20000 p50=0.140 us p99=2.179 us (M)[AC] load1=12.17 gate_p99<=20.000 us(G) pass=1
typing allocations=0 (M)[AC] load1=12.17 gate_allocations=0(G) guard=active
BENCH indent_detect file=log_1g.txt prefix<=64KiB TRACK n=2000 p50=86.433 us p99=219.518 us (M)[AC] load1=12.17 gate_p99<=1000.000 us(G) pass=1
```

## How to verify

```
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/indent_test
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/indent_fuzz -max_total_time=120 -max_len=8192 -timeout=10
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/indent_bench
```

## Remaining work / verification limits

No missing standalone-module functionality. Integration and painting belong
to the follow-up beads. Quotes/comments can produce lexical bracket false
positives; detection is heuristic; Enter and brace checks scan the current
line, so pathological single lines can exceed ordinary source-code latency.

Quiet-box gate verdicts and LeakSanitizer with leaks enabled remain with the
coordinator. The full check driver passed but existing `x11_clip_test`,
`x11_live_test`, and `x11_stall_test` skipped because their private Xvfb servers
could not bind a socket in the sandbox. This is outside the indent scope;
no X11 files or display configuration were changed. All runs used :99.
