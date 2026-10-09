Implemented worker-only search, per-keystroke cancellation, streamed highlights, wrap-around, grouped replacement, and bottom-panel rendering.

Changed files:

- `src/findui/{findui.h,findui.c,worker.c,paint.c,private.h,STATUS.md}`
- `tests/findui_test.c`
- `fuzz/findui_fuzz.c`
- `bench/findui_bench.c`
- [P4.6.md](/home/tobias/Projects/editor/.wt/edit-457.6/docs/decisions/P4.6.md), including the Integration contract.

Red runs:

```text
tests/findui_test.c:16: assertion failed: findui_init(&panel, &config) == FINDUI_OK
tests/findui_test.c:289: assertion failed: findui_highlights(&f.panel, 4, 4, ranges, 600, &count) == FINDUI_OK && count == 0
```

Green runs:

```text
findui_test: all passed
make all: PASS, exit 0
check: 44 test binaries passed
test_replay_cli: all passed
make check: PASS, exit 0
fuzz: 23 fuzzers built
make fuzz: PASS, exit 0
```

Targeted tests passed under gcc and clang ASan/UBSan. Full check used host Xvfb `:99`; sandbox display access failed initially. LeakSanitizer was disabled as instructed.

Fuzz evidence:

```text
(M)[AC] load1=5.89: Done 27580 runs in 61 second(s)
(M)[AC] load1=9.03: Done 7825 runs in 61 second(s)
```

Single corpus benchmark, TRACK:

```text
power=Not charging [AC] load1=10.71
logical_ack (M)[AC] n=64 p50_ms=0.002246 p99_ms=0.009399
(G)1/5ms comparison=within verdict=TRACK
calling_thread_scans=(M)0 expected=(G)0
```

Open integration/limits: editor wiring and proposed accessors remain with their owning beads. Oversized replace-all returns `LIMIT`; allocation failures preserve one undoable prefix pending an atomic undo transaction API. Existing native EGL lifecycle test skips under Xvfb. Coordinator owns gate verdicts and the leaks-enabled rerun.