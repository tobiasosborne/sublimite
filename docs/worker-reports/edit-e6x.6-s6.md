Implemented the CPU reference window, phase injector, CSV protocol/joiner, and headless paired test. Updated `src/raster/STATUS.md` and the worklog; design and evidence are in [P2.6.md](/home/tobias/Projects/editor/.wt/edit-e6x.6/docs/decisions/P2.6.md).

Files: `tools/refwin.c`, `tools/keyinject.c`, `tools/refwin_protocol.h`, `tools/refwin_pairs.py`, `tests/refwin_test.c`, and the three documentation files.

Red run:
```text
refwin_test: tools not implemented
refwin_test: FAIL headless CSV completeness/monotonicity
```

Green run, release, ASan/UBSan, and XSendEvent fallback:
```text
refwin_test: PASS 12 editor/reference pairs, complete monotonic CSV, incomplete/wrong-frame runs rejected (Xvfb phase approximate)
```

Validation:
```text
make all: exit 0
check: 44 test binaries passed
test_replay_cli: all passed
fuzz: 22 fuzzers built
Done 675558 runs in 11 second(s)
```

Fuzz: (M)[AC], BAT0 `Not charging`, load1=5.50; no findings. Final T6 normalization also passed targeted release/sanitizer/fallback tests.

Single historical TRACK run, (M)[AC], load1=14.43:
```text
T4: p50=60562 ns    p99=9129291 ns
T5: p50=-170043 ns  p99=12904568 ns
T6: p50=292621 ns   p99=16256348 ns
```

That run preceded final T6 observer normalization; no bench rerun. All new-tool runs used `:99`. Xvfb phase locking is approximate, with no real vblank. G2c awaits the coordinator’s real-display run. Leak-on verification remains outstanding; optional EGL was omitted. Missing xcb-xtest development files are handled through runtime `dlopen`.