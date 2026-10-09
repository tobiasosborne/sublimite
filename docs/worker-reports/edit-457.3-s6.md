Implemented the Linux normal-editor defaults as a const table with binary search, caller-owned chord state, and explicit unimplemented action flags.

Files: `src/keys/{keys.h,keys.c,STATUS.md}`, `tests/keys_test.c`, `bench/keys_bench.c`, `fuzz/keys_fuzz.c`, [P4.3.md](/home/tobias/Projects/editor/.wt/edit-457.3/docs/decisions/P4.3.md).

Red run:
```text
keys_test: FAIL ctrl+q expected binding, result=0
tests/keys_test.c:321: assertion failed: 0
```

Green run:
```text
keys_test: 256 independent binding rows passed
keys_test: 20000 lookups mallocs=0 guard=active
keys_test: all passed
```

Build/check results:
```text
make all exit=0
check: 26 test binaries passed
test_replay_cli: all passed
make check exit=0
fuzz: 15 fuzzers built
make fuzz exit=0
```

`make check` passed with Xvfb `:99` socket access and `ASAN_OPTIONS=detect_leaks=0`; the initial sandbox run failed because socket access returned EPERM.

Fuzzing completed without ASan/UBSan findings:

- Final harness: 222,562 runs in 61 seconds (M)[AC], load1=3.35.
- Earlier harness: 296,157 runs in 61 seconds (M)[AC], load1=4.02.

Benchmark, shared-box TRACK only:
```text
BENCH keys_lookup TRACK n=1000000 p50=0.037 us p99=0.104 us (M)[AC] load1=4.02 gate_p99<=1.000 us(G) pass=1
STREAM (M)[AC] load1=4.02 matches=436925 prefixes=250575 cancelled=125575 misses=186925 mallocs=0 guard=active gate_mallocs=0(G)
```

Open limitations: contextual panel/snippet/grammar overrides require a future context resolver. Arbitrary-layout shifted punctuation needs an unshifted input symbol. Executors and config overrides remain later work. The coordinator still needs leak-enabled checks and a quiet-box gate verdict.