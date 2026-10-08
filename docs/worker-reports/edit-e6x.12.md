P2.2c completed. The loop drains XCB before sleeping and after callbacks. Clipboard ownership checks and property reads now complete asynchronously. Remaining keymap and XI2 waits are documented.

`make bench` passed; `x11_bench` reported **0 allocations (M)[AC]**. Fuzzing passed.

Files changed:

- [x11.c](/home/tobias/Projects/editor/src/x11/x11.c), [clip.c](/home/tobias/Projects/editor/src/x11/clip.c), [clip.h](/home/tobias/Projects/editor/src/x11/clip.h), [plat.h](/home/tobias/Projects/editor/src/x11/plat.h)
- [x11_stall_test.c](/home/tobias/Projects/editor/tests/x11_stall_test.c), [x11_live_test.c](/home/tobias/Projects/editor/tests/x11_live_test.c)
- [P2.2c.md](/home/tobias/Projects/editor/docs/decisions/P2.2c.md)

Red run output:

```text
x11_stall_test: FAIL callback reply stranded events: keys=0 close=0; bounded run timed out or exceeded 5 ms (G)[AC]
```

Green run output:

```text
x11_stall_test: key callback reply 0.074 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: direct callback reply 0.066 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: blink callback reply 0.087 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: work callback reply 0.152 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: queued before run 0.071 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: clipboard set return 0.039 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_live_test: ok
x11_input_test: ok
x11_xi2_test: ok
```

`make check` tail:

```text
== build/san/tests/x11_xi2_test
x11_xi2_test: ok
check: 14 test binaries passed
```