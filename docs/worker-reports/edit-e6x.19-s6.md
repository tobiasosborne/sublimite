Implemented the permitted fixes. Three restricted-file branches remain proposals: §2 local clipboard saturation, §18 wheel deduplication, and §19 XI2 button masks.

Changed: `src/x11/{x11.c,input.c,input.h,STATUS.md}`, `tests/x11_input_test.c`, new `tests/x11_order_test.c`, and `fuzz/x11_input_fuzz.c`. Per-finding decisions and evidence are in [P2.2e.md](/home/tobias/Projects/editor/.wt/edit-e6x.19/docs/decisions/P2.2e.md).

Red evidence:

```text
x11_order_test: FAIL §2 raw burst preserves every edit
x11_order_test: FAIL §2 accepted local completion is never lost
x11_order_test: FAIL §3 preceding input reaches callback first
x11_order_test: FAIL §3 reply polling preserves preceding queued input order
FAIL tests/x11_input_test.c:277: §7 shift release updates repeat
FAIL tests/x11_input_test.c:417: §7 cold key transitions made 3 allocations
x11_order_test: FAIL §8 bounded drain services timers/work and returns under sustained callbacks
FAIL tests/x11_input_test.c:55: §9 us/a: release has no insertion text
FAIL tests/x11_input_test.c:306: §10 rate 1000000001 advances deadline
x11_order_test: FAIL §11 negotiation refusal rejects init
x11_order_test: FAIL §12 delayed keymap reply cannot block input/shutdown
x11_order_test: FAIL §12 delayed device reply cannot block input/shutdown
x11_order_test: FAIL §18 independent legacy notch near smooth timestamp survives
FAIL tests/x11_input_test.c:319: §19 core button 8 held after press
x11_order_test: FAIL §19 XI motion preserves held button 9
x11_order_test: FAIL §26 compose allocation failure rejects init and releases table
x11_order_test: FAIL §29 input allocation failure releases compose table
x11_order_test: FAIL §30 startup records known exec time only
```

Final green runs:

```text
x11_input_test: ok
x11_order_test: all ok
make all: exit 0
check: 30 test binaries passed
test_replay_cli: all passed
make check: exit 0
fuzz: 16 fuzzers built
make fuzz: exit 0
```

Final check: (M)[AC], Not charging, load 3.08; `DISPLAY=:99 EDIT_DISPLAY=:99`, `ASAN_OPTIONS=detect_leaks=0`. The separately invoked restricted-file reproductions remain failing.

Fuzz runs, all without sanitizer errors:

```text
5093368 runs / 121 s (M)[AC] load 5.65 — initial implementation
3567256 runs / 121 s (M)[AC] load 4.25 — cold-allocation fix
4874884 runs / 121 s (M)[AC] load 4.26 — final code
```

Single bench run, TRACK, Full [AC], load 2.16:

```text
key translation: p50 230 ns, p99 661 ns; allocations 1 (M)[AC] load 2.16
startup: p50 7.320 ms, p99 18.259 ms (M)[AC] load 2.16
x11_bench TRACK: exit 1
```

That allocation prompted the cold-state regression and was fixed; the release counting-allocator test passes. The bench was not rerun, so its timings precede that fix.

Open: the three restricted branches require `clip.c`/`xi2.c` changes by their owning beads. Coordinator validation with leaks enabled, quiet-box benchmarking, and existing real-touchpad/manual checks remain. STATUS is current.