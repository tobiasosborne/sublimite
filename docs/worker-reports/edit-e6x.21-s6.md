Implemented the scoped fixes. Changed `src/x11/x11.c`, the bench, clipboard skip logic, live tests, new `tests/x11_test_main.c.in`, and fuzzer ops. Updated [P2.2g.md](/home/tobias/Projects/editor/.wt/edit-e6x.21/docs/decisions/P2.2g.md) and `src/x11/STATUS.md`.

Red → green evidence:

```text
§21 clipboard sequences: FAIL unreachable (clip_init calls=0)
§21 clipboard sequences: ok (clip_init calls=8)
XI2 already resolved by P2.2f: populated decodes=32, nonzero scroll=2

§22 window protocol error returned 0
§22 startup errors: ok

§23 FAIL 100 ms subset exceeds G4a: exit 0, want 1
§23 bench self-check: ok

§24 init-failure regression: FAIL: initialization failure exited 0
§24 init-failure regression: ok (exit 1)

§25 coverage regression: FAIL map no-op accepted
§25 mutation check: ok (rejected)
§25 window/Present/idle: ok
```

Verification:

```text
make all: exit 0
check: 35 test binaries passed
test_replay_cli: all passed
make check: exit 0
fuzz: 20 fuzzers built
make fuzz: exit 0
```

Final fuzz runs, both clean under ASan/UBSan with leak detection disabled:

```text
Live: 9364 runs in 121 seconds     (M)[AC], Not charging, load 7.20
Pure: 2363697 runs in 121 seconds  (M)[AC], Not charging, load 6.49
```

An earlier run caught a harness deadline-reset error; fixed and replayed successfully.

Single TRACK bench, Not charging `[AC]`, load `7.20`:

```text
Translation: p50 341 ns, p99 621 ns; allocations 0 (M)
Startup subset: p50 8.282 ms, p99 8.934 ms; budget PASS (M)
x11_bench TRACK: exit 0
```

Open problems: full warm/cold G4a fixture needs editor/raster work outside scope. A separate `CurrentTime` ownership-race probe still fails completion/data association; the `clip.c` proposal is recorded, implementation untouched. Coordinator must rerun leak detection. All X runs used private Xvfb or `:99`.