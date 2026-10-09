# edit-zzj.2 — P3.2 view worker report

## 1. Summary

Implementation and required verification complete. Nothing remains in this bead. No changes outside the
allowed module/test/fuzz/bench/decision files. No git or bd commands were run.

- `src/view/view.h`: one public API, pointer-free tab state, selection record,
  command/resume/error/change contract, bounded checkpoint hook.
- `src/view/view.c`: all requested movements and selections, direct piece edits,
  exact resumable grapheme segmentation, bounded approximate columns, cursor follow.
- `tests/view_test.c`: per-step key-log assertions, Unicode/binary/CRLF/edit joins,
  long-cluster/budget/checkpoint/scroll tests and 10,000-key allocation guard.
- `fuzz/view_fuzz.c`: random logs, independent byte edit model and boundary/work invariants.
- `bench/view_bench.c`: eight command classes on all three required corpora,
  p50/p99 and G1 shares, TRACK time rows and gated zero-allocation rows.
- `docs/decisions/P3.2.md`: semantics, limits, key-log format, alternatives and evidence.
- `src/view/STATUS.md`: this complete worker report.

## Integration / next step

P3.3 should read `view.h` and `docs/decisions/P3.2.md` first. On every command,
inspect the edit tuple even on MORE/error; pass it to layout/undo integration.
Resume pending work before drawing, or cancel explicitly. For each tab serialize
`view_state` fields. Layout's checkpoint adapter is optional and stays outside
this bead. Invalidate it after reported edits; same-command repair suppresses it.
Undo integration is intentionally left to P3.3 (optional in this brief).

## 2. RED run

The key-log suite and public declarations were written before view.c existed.

```text
rm -f build/libedit.a; ar rcs build/libedit.a build/rel/src/base/base.o build/rel/src/base/malloc_guard.o build/rel/src/file/file.o build/rel/src/find/find.o build/rel/src/font/fallback.o build/rel/src/font/font.o build/rel/src/journal/journal.o build/rel/src/layout/layout.o build/rel/src/lineidx/lineidx.o build/rel/src/piece/piece.o build/rel/src/render/render.o build/rel/src/render/render_null.o build/rel/src/scan/scan.o build/rel/src/trace/replay.o build/rel/src/trace/trace.o build/rel/src/trace/trace_load.o build/rel/src/undo/undo.o build/rel/src/utf8/utf8.o build/rel/src/work/work.o build/rel/src/x11/clip.o build/rel/src/x11/input.o build/rel/src/x11/x11.o build/rel/src/x11/xi2.o
gcc -O2 -g -msse2 -pthread build/rel/tests/view_test.o build/libedit.a -lm -ldl -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/tests/view_test
/usr/bin/ld: build/rel/tests/view_test.o: in function `init':
/home/tobias/Projects/editor/.wt/edit-zzj.2/tests/view_test.c:17:(.text+0x1f1): undefined reference to `view_init'
/usr/bin/ld: build/rel/tests/view_test.o: in function `command':
/home/tobias/Projects/editor/.wt/edit-zzj.2/tests/view_test.c:22:(.text+0x294): undefined reference to `view_command'
/usr/bin/ld: /home/tobias/Projects/editor/.wt/edit-zzj.2/tests/view_test.c:27:(.text+0x2de): undefined reference to `view_continue'
collect2: error: ld returned 1 exit status
make: *** [Makefile:78: build/tests/view_test] Error 1
make: Leaving directory '/home/tobias/Projects/editor/.wt/edit-zzj.2'
```

Additional regressions written before their fixes:

```text
view state got 8/8/18446744073709551615 expected 1/1/18446744073709551615
tests/view_test.c:42: assertion failed: 0
tests/view_test.c:24: assertion failed: f->v.scanned<=65536u

```

## 3. GREEN run

Release, final implementation, power before run `Charging` = (M)[AC]:

```text
view_test: 10000 keys mallocs=0 guard=active
view_test: all passed

```

## 4. Verification

Final fuzz, raw status before run `Charging` = (M)[AC]:

```sh
ASAN_OPTIONS=detect_leaks=0 WT/build/fuzz/view_fuzz -max_total_time=310 -max_len=4096 -artifact_prefix=WT/build/ WT/build/view-corpus
```

```text
Done 126590 runs in 311 second(s)
```

Exit 0; the run includes the retained Prepend regression and random UTF-8/binary
key logs. Final `make fuzz`: `fuzz: 13 fuzzers built`.

Final ASan/UBSan check (outside sandbox, isolated Xvfb, `Charging` [AC]):

```sh
ASAN_OPTIONS=detect_leaks=0 xvfb-run -a -s '-screen 0 1280x1024x24 -noreset' make -C WT check
```

```text
x11_input_test: ok
== build/san/tests/x11_live_test
xi2 active, core wheel fallback 1
x11_live_test: ok
== build/san/tests/x11_stall_test
x11_stall_test: key callback reply 0.125 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: direct callback reply 0.103 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: blink callback reply 0.056 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: work callback reply 0.090 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: queued before run 0.025 ms (M)[AC], budget 5 ms (G)[AC]: ok
x11_stall_test: clipboard set return 0.033 ms (M)[AC], budget 5 ms (G)[AC]: ok
== build/san/tests/x11_xi2_test
x11_xi2_test: ok
check: 23 test binaries passed
make: Leaving directory '/home/tobias/Projects/editor/.wt/edit-zzj.2'
```

Exit 0. All 23 test binaries passed, including live X11 tests. Server resets were
disabled to avoid the existing intermittent initialization failure; sandboxed
and default-reset attempts and their failures remain in build logs.

Final module-only release bench, raw status before run `Charging` = (M)[AC]:

```sh
make -C WT build/bench/view_bench
WT/build/bench/view_bench /tmp/edit-corpus 11
```

```text
POWER status=Charging [AC]; measurements (M), concurrent workers: INDICATIVE ONLY
CORPUS /tmp/edit-corpus/log_1g.txt bytes=1073741824 samples/class=11 (M)[AC] loaded-box INDICATIVE
BENCH name=view_log_1g.txt_char_TRACK_M n=11 p50=2503 p99=12050 ci95=[2360,3446] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=char p99=12.050 us target<=50 us(E) G1-share=1.205%/0.603% of 1.0/2.0 ms(G)
BENCH name=view_log_1g.txt_word_TRACK_M n=11 p50=2565 p99=4392 ci95=[2063,3337] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=word p99=4.392 us target<=50 us(E) G1-share=0.439%/0.220% of 1.0/2.0 ms(G)
BENCH name=view_log_1g.txt_vertical_TRACK_M n=11 p50=2223781486 p99=2866598703 ci95=[1283473178,2582465653] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=vertical p99=2866598.703 us target<=50 us(E) G1-share=286659.870%/143329.935% of 1.0/2.0 ms(G)
BENCH name=view_log_1g.txt_page_TRACK_M n=11 p50=1253543372 p99=1824910495 ci95=[615673145,1600817710] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=page p99=1824910.495 us target<=50 us(E) G1-share=182491.050%/91245.525% of 1.0/2.0 ms(G)
BENCH name=view_log_1g.txt_home_end_TRACK_M n=11 p50=5432 p99=7396 ci95=[981,6310] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=home_end p99=7.396 us target<=50 us(E) G1-share=0.740%/0.370% of 1.0/2.0 ms(G)
BENCH name=view_log_1g.txt_document_TRACK_M n=11 p50=2041 p99=7129163164 ci95=[1340,6525906560] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=document p99=7129163.164 us target<=50 us(E) G1-share=712916.316%/356458.158% of 1.0/2.0 ms(G)
BENCH name=view_log_1g.txt_selection_TRACK_M n=11 p50=3079 p99=7498283210 ci95=[2373,5731160070] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=selection p99=7498283.210 us target<=50 us(E) G1-share=749828.321%/374914.160% of 1.0/2.0 ms(G)
BENCH name=view_log_1g.txt_edit_TRACK_M n=11 p50=2710 p99=6023 ci95=[2279,4050] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=edit p99=6.023 us target<=50 us(E) G1-share=0.602%/0.301% of 1.0/2.0 ms(G)
ALLOCATIONS (M)[AC] mallocs=0 gate=0(G) guard=active pass=1
CORPUS /tmp/edit-corpus/oneline_1g.txt bytes=1073741824 samples/class=11 (M)[AC] loaded-box INDICATIVE
BENCH name=view_oneline_1g.txt_char_TRACK_M n=11 p50=3410672793 p99=3974495000 ci95=[3236048058,3766704472] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=char p99=3974495.000 us target<=50 us(E) G1-share=397449.500%/198724.750% of 1.0/2.0 ms(G)
BENCH name=view_oneline_1g.txt_word_TRACK_M n=11 p50=3384203628 p99=3696650440 ci95=[3108122778,3527218572] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=word p99=3696650.440 us target<=50 us(E) G1-share=369665.044%/184832.522% of 1.0/2.0 ms(G)
BENCH name=view_oneline_1g.txt_vertical_TRACK_M n=11 p50=4402438899 p99=8923363260 ci95=[3129024526,7295038478] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=vertical p99=8923363.260 us target<=50 us(E) G1-share=892336.326%/446168.163% of 1.0/2.0 ms(G)
BENCH name=view_oneline_1g.txt_page_TRACK_M n=11 p50=5878665565 p99=9251346438 ci95=[2814290140,8674322454] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=page p99=9251346.438 us target<=50 us(E) G1-share=925134.644%/462567.322% of 1.0/2.0 ms(G)
BENCH name=view_oneline_1g.txt_home_end_TRACK_M n=11 p50=1576627719 p99=2486734448 ci95=[1342797567,2473667336] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=home_end p99=2486734.448 us target<=50 us(E) G1-share=248673.445%/124336.722% of 1.0/2.0 ms(G)
BENCH name=view_oneline_1g.txt_document_TRACK_M n=11 p50=1480029062 p99=2411038429 ci95=[1327858757,2403368009] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=document p99=2411038.429 us target<=50 us(E) G1-share=241103.843%/120551.921% of 1.0/2.0 ms(G)
BENCH name=view_oneline_1g.txt_selection_TRACK_M n=11 p50=2258042668 p99=2998194258 ci95=[1365543296,2941640215] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=selection p99=2998194.258 us target<=50 us(E) G1-share=299819.426%/149909.713% of 1.0/2.0 ms(G)
BENCH name=view_oneline_1g.txt_edit_TRACK_M n=11 p50=1189445148 p99=1672160118 ci95=[1139770744,1401461855] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=edit p99=1672160.118 us target<=50 us(E) G1-share=167216.012%/83608.006% of 1.0/2.0 ms(G)
ALLOCATIONS (M)[AC] mallocs=0 gate=0(G) guard=active pass=1
CORPUS /tmp/edit-corpus/unicode.txt bytes=1048576 samples/class=11 (M)[AC] loaded-box INDICATIVE
BENCH name=view_unicode.txt_char_TRACK_M n=11 p50=1241 p99=4376 ci95=[1035,1892] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=char p99=4.376 us target<=50 us(E) G1-share=0.438%/0.219% of 1.0/2.0 ms(G)
BENCH name=view_unicode.txt_word_TRACK_M n=11 p50=1537 p99=2908 ci95=[955,1889] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=word p99=2.908 us target<=50 us(E) G1-share=0.291%/0.145% of 1.0/2.0 ms(G)
BENCH name=view_unicode.txt_vertical_TRACK_M n=11 p50=1130203 p99=1226128 ci95=[1124658,1165020] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=vertical p99=1226.128 us target<=50 us(E) G1-share=122.613%/61.306% of 1.0/2.0 ms(G)
BENCH name=view_unicode.txt_page_TRACK_M n=11 p50=1129458 p99=1139364 ci95=[1124763,1138238] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=page p99=1139.364 us target<=50 us(E) G1-share=113.936%/56.968% of 1.0/2.0 ms(G)
BENCH name=view_unicode.txt_home_end_TRACK_M n=11 p50=1795 p99=8363 ci95=[886,5730] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=home_end p99=8.363 us target<=50 us(E) G1-share=0.836%/0.418% of 1.0/2.0 ms(G)
BENCH name=view_unicode.txt_document_TRACK_M n=11 p50=810 p99=7126332 ci95=[625,7014191] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=document p99=7126.332 us target<=50 us(E) G1-share=712.633%/356.317% of 1.0/2.0 ms(G)
BENCH name=view_unicode.txt_selection_TRACK_M n=11 p50=3076 p99=7039080 ci95=[1094,6998156] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=selection p99=7039.080 us target<=50 us(E) G1-share=703.908%/351.954% of 1.0/2.0 ms(G)
BENCH name=view_unicode.txt_edit_TRACK_M n=11 p50=1958 p99=3977 ci95=[1501,2896] gate_p50=0 gate_p99=0 pass=1 power=[AC]
TRACK class=edit p99=3.977 us target<=50 us(E) G1-share=0.398%/0.199% of 1.0/2.0 ms(G)
ALLOCATIONS (M)[AC] mallocs=0 gate=0(G) guard=active pass=1

```

Exit 0. Harness p50/p99 values are nanoseconds. All 24 time rows are TRACK;
`pass=1` on these rows means valid samples, not that the 50 us estimate is met.
All three allocation rows are gated and report 0. Eleven samples/class means
p99 is the sample maximum; this is a small indicative run, not a tail-latency
release verdict. Coordinator should use the default 31 or a larger sample count
with the synthesized kernel on a quiet machine.

## 5. Numbers against gates

- Allocation guard: 10,000 keys, 0 malloc/calloc/realloc calls (M)[AC], gate 0 (G).
- View segmentation/column scan: at most 65,536 byte-work units per command/resume
  (P), checked after every unit-test/fuzz command. Opaque frozen piece work excluded.
- Time rows: TRACK, provisional p99 <= 50 us (E); no view time gate exists in §0.2.
  Each row reports its share of the G1 1.0/2.0 ms budgets (G), excluding render.
  Eleven samples/class in this indicative run (benchmark default is 31).

## 6. Decisions

See `docs/decisions/P3.2.md`. Highlights: one selection in an extensible record;
cell-based preferred column reset to unset by horizontal commands/edits; exact
cluster boundaries even across windows; MORE for unbounded clusters/runs;
approximate bounded column fallback; stored checkpoint seed callback; forward
boundary repair after cluster-joining edits; insert-first replacement with an
explicit successful-prefix report if the following delete fails.

## 7. Out of scope findings / Tobias

- P1.3 reference piece line queries scan the whole document. They dominate
  multi-gigabyte document/page/vertical/selection benchmarks and the single-line
  corpus. Frozen APIs were used; kernel/layout/undo were not modified.
- Frozen piece API has no multi-operation transaction/reservation. Replacement
  may report a successful insertion prefix if deletion fails; callers must inspect
  the change on errors. Same limitation is documented in P1.5.
- Sandboxed Xvfb cannot initialize the existing live X11 stall test. Running
  outside the sandbox succeeds, but the full suite also exposed its known
  intermittent initialization failure. A no-reset isolated Xvfb retry passed;
  final full check uses that setting. No X11 source was changed.
- LeakSanitizer is disabled (`ASAN_OPTIONS=detect_leaks=0`) because the sandbox
  runner cannot support its ptrace check. ASan/UBSan remain active; the coordinator
  should rerun with leak checking on a suitable runner.
- The final measurement runs start with `Charging` [AC]; earlier development
  runs were [bat]. All benchmark results are loaded-box INDICATIVE ONLY. No timing
  miss was chased and no result is claimed as an end-to-end typing verdict.

## 8. Makefile changes

None. Existing globs discover all added C files; no extra link flags are needed.
