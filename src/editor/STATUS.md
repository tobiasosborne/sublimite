# P3.3 editor loop — edit-zzj.3

Implemented: public editor API and native/injected event loop, CPU backend
selection in `src/main.c`, fixed reservations, piece/undo mutations, view
movement and repair, dirty layout slices, queue depth one/coalescing, deferred
resize, trace endpoints, mutation journal staging/pumping, negative stopped-loop
errors, blink timeout/unfocus policy. No GL or dependency module changes.

Decision and integration limitations: `docs/decisions/zzj.3.md`.

Verification:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_test
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
make fuzz
ASAN_OPTIONS=detect_leaks=0 build/fuzz/editor_fuzz -max_total_time=300 -max_len=1024 build/editor-corpus
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_bench --track
```

Read BAT0 status and load1 before measurements. Xvfb socket access requires
running outside this sandbox; all live runs used `:99`. LeakSanitizer must be
rerun by the coordinator with leaks enabled. Release tests perform real malloc
counting; ASan reports the guard as inert.

RED before implementation (M)[AC], load1 2.82:

```text
undefined reference to `editor_get_stats'
undefined reference to `editor_step'
collect2: error: ld returned 1 exit status
make: *** [Makefile:87: build/tests/editor_test] Error 1
```

Fuzz-discovered scrolled-undo RED, focused test written before its fix:

```text
editor_test:36: FAIL rc == EDITOR_OK || rc == EDITOR_MORE
editor_test:214: FAIL press(e, key('z', PLAT_MOD_CTRL, NULL)) == 0
editor_test:222: FAIL scrolled_undo() == 0
```

GREEN includes the scrolled-undo regression, byte/journal model, real release
guard over 10000 keys (P) with each backend, idle timeout/unfocus, delayed backend
queue depth/coalescing, native XKB translation/undo/redo/WM close, and an injected
piece allocator exhaustion that must stop with a negative error.

G1 (M)[AC], load1 3.39, shared-box TRACK, 10000 measured keys (P) per row:

| Backend | Drain to submit p50/p99 | Drain to T4 p50/p99 | Allocations |
|---|---:|---:|---:|
| null | 0.076/0.121 ms | 0.077/0.122 ms | 0 |
| raster | 0.227/0.405 ms | 0.355/0.987 ms | 0 |

G1 limits: 1.0/2.0 ms (G). Both rows type at published line 8053057 (P, floor
of 0.9 times fixture lines), byte 966366840 (M)[AC], of the existing 1 GiB
`/tmp/edit-corpus/log_1g.txt`. Every sampled key has its own containing frame.
The alternating insert/backspace workload is declared in benchmark output.
The coordinator owns the quiet verdict.

Corrected G11 (M)[AC], shared-box TRACK:

```text
null: load1=3.40 process CPU p50/p99=72962/129718 ns; UI polls=20; idle=0 unfocused=0
raster: load1=3.57 process CPU p50/p99=649600/1017413 ns; UI polls=100; idle=0 unfocused=0
```

Limits: process CPU 100000/200000 ns (G), wakeups <=2/s while blinking and zero
after 10 seconds or unfocus (G). Null is within the limits; raster misses CPU
and UI wakeups, before counting its additional worker polls. This acceptance
criterion remains open. No repeated G1 runs were made to seek a quiet box.

Remaining: raster G11; hard slice preemption for giant view/undo commands (needs
dependency API work); automatic post-edit index rebuilding; saved-file/journal
transaction integration and recovery UI beyond M0. The CLI preserves a unique
adjacent journal and reports its path; Ctrl+S is intentionally omitted while the
save transaction dependency is in flight. Home/End are omitted after finding
the view EOF/End bug recorded in the decision. No out-of-scope module was fixed.

The burst allocation guard sums every mutation/layout/submit segment, pausing
only around platform/present/completion IO through `editor_config.on_io`.
This prevents XCB packets in a later turn from contaminating the process-wide
guard, per the frozen P2.0 library exemption. A late pre-fix release run at
load1 12.79 (M)[AC] exposed that measurement error. Final release GREEN
(M)[AC], BAT0=Full, load1 5.71:

```text
editor_test: scrolled undo/redo viewport repair passed
editor_test: mutation error is negative and stops the loop passed
editor_test: null script and journal byte model passed
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: raster script and journal byte model passed
editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: idle blink timeout and unfocused timer disarm passed
editor_test: queue depth one and coalesced edits passed
editor_test: native X11 translation, editor loop and WM close passed
editor_test: all passed
```

Build: `make all` exit 0 (gcc, strict warnings); `make fuzz` exit 0:

```text
fuzz: 15 fuzzers built
```

ASan/UBSan's editor suite passes with the counting guard explicitly ASan-inert.
Final full checks (M)[AC], BAT0=Full, load1 5.71, `detect_leaks=0`:

```text
make: Nothing to be done for 'all'.
check: 26 test binaries passed
test_replay_cli: all passed
fuzz: 15 fuzzers built
```

All three required make targets returned exit 0. `make bench` was not run over
unrelated modules; the editor's G1 and corrected G11 rows are recorded above.

Successful fuzz evidence (M)[AC], after the scrolled-undo fix, BAT0=Charging,
load1 6.16:

```text
#12554 DONE cov: 6266 ft: 37504 corp: 722/257Kb lim: 512 exec/s: 41 rss: 296Mb
Done 12554 runs in 301 second(s)
```

Final IO-boundary implementation, (M)[AC], BAT0=Full, load1 5.71:

```text
#4270 DONE cov: 6295 ft: 37902 corp: 675/235Kb lim: 530 exec/s: 70 rss: 290Mb
Done 4270 runs in 61 second(s)
```

Fuzz uses a full-copy byte/history model with Unicode one-shot segmentation,
independent of view's resumable scanner and returned mutation ranges. It checks
bytes and selection after each key/event. Cases include malformed bytes,
combining/ZWJ sequences, newline joins, Shift movement/replacement, vertical
movement, undo/redo invalidation, focus, resize, expose and ignored releases.
No findings in the successful runs. The original failed Unicode seeds exposed
the out-of-scope End issue and the fixed scrolled-undo integration bug; no
dependency source was changed.

Actual CLI smoke on `:99`: native `abc`, Backspace, Enter, `x`, WM close; exit 0,
then a separate journal replay verifies `ab\nx` exactly. Its scratch file and
journal were created only under `/tmp`.
