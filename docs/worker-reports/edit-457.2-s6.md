Implemented the standalone module. Enter preserves indentation without adding an extra level. Bracket matching balances the selected type and does not parse quotes/comments. No other modules were edited.

Files: `src/indent/indent.c`, [indent.h](/home/tobias/Projects/editor/.wt/edit-457.2/src/indent/indent.h), `tests/indent_test.c`, `bench/indent_bench.c`, `fuzz/indent_fuzz.c`, [P4.2.md](/home/tobias/Projects/editor/.wt/edit-457.2/docs/decisions/P4.2.md), [STATUS.md](/home/tobias/Projects/editor/.wt/edit-457.2/src/indent/STATUS.md).

Red run against stubs:

```text
tests/indent_test.c:23: assertion failed: indent_on_enter(t,cursor,out,sizeof out,&n)==INDENT_OK
```

Green run:

```text
indent typing malloc guard: active, allocations=0
indent_test: all cases passed
make all exit=0
make check exit=0
check: 29 test binaries passed
test_replay_cli: all passed
fuzz: 16 fuzzers built
```

Fuzz: `Done 449498 runs in 121 second(s)` — (M)[AC], load1=9.67; no crashes, oracle failures, or sanitizer errors.

Single benchmark run, TRACK only:

```text
indent_on_enter: p50=0.396 us p99=0.666 us (M)[AC] load1=12.17; gate<=20 us(G), PASS
indent_bracket_match: p50=0.140 us p99=2.179 us (M)[AC] load1=12.17; gate<=20 us(G), PASS
indent_detect log_1g.txt: p50=86.433 us p99=219.518 us (M)[AC] load1=12.17; gate<=1000 us(G), PASS
typing allocations=0 (M)[AC] load1=12.17; guard=active
```

No open module problems. Integration and painting remain deferred. Existing X11 clip/live/stall tests skipped because private Xvfb could not bind sockets. Checks used `detect_leaks=0`; leak-enabled verification and quiet-box gate verdicts remain with the coordinator.