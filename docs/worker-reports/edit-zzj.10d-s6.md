Fixed the regression. Preferred-column discovery cleared soft-row affinity before re-entering visual-row lookup. `view.c` now resolves the origin using saved `before_state.visual_end`.

Changed [view.c](src/view/view.c), [view_test.c](tests/view_test.c), [STATUS.md](src/view/STATUS.md), [P3.2b.md](docs/decisions/P3.2b.md), [P4.1.md](docs/decisions/P4.1.md), and [worklog](docs/worklog/2026-10-09.md). Tests cover anchored Shift+Up/Down, reversal, preferred columns, continuations, cancellation and retry. `tests/wrap_test.c` expectations are unchanged.

Red, before the fix — (M)[AC], Not charging, load1=5.47:

```text
tests/view_test.c:690: assertion failed: f.v.state.selection.cursor==8 && f.v.state.selection.anchor==5
FAIL wrap_test:396 view_command(&v,VIEW_DOWN,true,NULL,0,&ch)==0 && v.state.selection.cursor==8 && v.state.selection.anchor==5
```

Green — (M)[AC], Not charging, load1=1.69; DISPLAY/EDIT_DISPLAY=:99, ASan/UBSan with `detect_leaks=0`:

```text
wrap_selection: anchored Shift+Up/Down preserves soft-row affinity and preferred column
wrap_port: long visual-column continuation, anchored soft-end motion and cancellation passed
view_test: all passed
wrap_test: all passed
make check: exit 0
check: 45 test binaries passed
test_replay_cli: all passed
```

Build results — (M)[AC], Not charging, load1=2.10:

```text
make all: exit 0
make fuzz: exit 0
fuzz: 23 fuzzers built
```

View fuzz — requested 120 seconds (G); (M)[AC], Not charging, load1=1.69, exit zero:

```text
Done 663 runs in 135 second(s)
```

Single TRACK benchmark, existing wrap-off coverage, exit zero:

```text
BENCH view_log_1g.txt_vertical_bulk1_command mode=TRACK n=5 p50=2008 p99=3034 ns (M)[AC] load1=1.69 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
```

Open limits: existing uncached oversized-cluster layout queries remain approximate. Leak-enabled verification remains with the coordinator. No new unresolved failure from this fix-up.