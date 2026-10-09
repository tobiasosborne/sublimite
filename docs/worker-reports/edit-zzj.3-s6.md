Implemented the M0 loop, CLI, tests, bench and fuzzer. **Raster G11 remains unmet**, so the bead is not fully accepted.

Files created: `src/editor/{editor.h,editor.c,input.c,open.c,private.h,STATUS.md}`, `src/main.c`, `tests/editor_test.c`, `bench/editor_bench.{c,args}`, `fuzz/editor_fuzz.c`, and [decision notes](/home/tobias/Projects/editor/.wt/edit-zzj.3/docs/decisions/zzj.3.md). No dependency modules, frozen headers or Makefile changes.

RED, before implementation:

```text
undefined reference to `editor_step'
collect2: error: ld returned 1 exit status
```

Focused scrolled-undo RED, before its fix:

```text
editor_test:36: FAIL rc == EDITOR_OK || rc == EDITOR_MORE
editor_test:214: FAIL press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0
```

Final GREEN — (M)[AC], BAT0=Full, load1=5.71:

```text
editor_test: scrolled undo/redo viewport repair passed
editor_test: mutation error is negative and stops the loop passed
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: queue depth one and coalesced edits passed
editor_test: native X11 translation, editor loop and WM close passed
editor_test: all passed
```

The guard covers mutation/layout/submit segments and excludes platform/present/completion I/O per P2.0.

Required targets returned exit 0:

```text
make: Nothing to be done for 'all'.
check: 26 test binaries passed
test_replay_cli: all passed
fuzz: 15 fuzzers built
```

ASan/UBSan used `ASAN_OPTIONS=detect_leaks=0`; coordinator leak verification remains.

Successful fuzz evidence:

```text
(M)[AC] Charging, load1=6.16
Done 12554 runs in 301 second(s)
(M)[AC] Full, load1=5.71 — final implementation
Done 4270 runs in 61 second(s)
```

G1: 10,000 keys per backend at published line 8,053,057 in the existing corpus. Alternating insert/Backspace; all numbers **TRACK**, (M)[AC], load1=3.39:

| Backend | Submit-return p50/p99 | T4 p50/p99 | Allocations |
|---|---:|---:|---:|
| null | 0.076/0.121 ms | 0.077/0.122 ms | 0 |
| raster | 0.227/0.405 ms | 0.355/0.987 ms | 0 |

G1 limits are 1.0/2.0 ms (G). The coordinator owns the quiet verdict.

Corrected G11 rows, TRACK:

```text
(M)[AC] load1=3.40 null: CPU p50/p99=72962/129718 ns
UI poll_returns=20; wakeups/s=1.998; idle=0; unfocused=0

(M)[AC] load1=3.57 raster: CPU p50/p99=649600/1017413 ns
UI poll_returns=100; wakeups/s=9.984; idle=0; unfocused=0
```

Open problems:

- Raster exceeds G11’s 100,000/200,000 ns CPU and ≤2/s wakeup limits (G). Backend completion/damage work is needed; its worker polls add further wakeups.
- Existing view/undo APIs cannot guarantee the hard slice bound for pathological commands.
- View End on an empty trailing line moves backward across LF. Reported without modifying view; Home/End are omitted.
- Ctrl+S is omitted pending journal transaction integration. The CLI preserves an adjacent session journal; native CLI typing, close and journal replay passed on `:99`.

Full evidence and verification commands are in [STATUS.md](/home/tobias/Projects/editor/.wt/edit-zzj.3/src/editor/STATUS.md).