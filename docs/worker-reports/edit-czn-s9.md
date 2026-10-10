# edit-czn session 9 worker report

Status: the reported deep-file prefix replay is reproduced, traced, and fixed locally in the editor. Focused red/green regressions cover an incomplete index and viewport-boundary newline deletion. The final-source sanitizer suite passed, the editor fuzz target ran clean for the required duration, and the full existing-row campaign completed in TRACK mode. The forced release rebuild and shorter final-source row confirmations also exited zero after the host clock correction. The loaded-box measurements do not certify G1 acceptance.

Scope is edit-czn only. Read CLAUDE.md, the supplied bead, the related editor review finding 10 and lineidx partial-query review, and module status. Git was used only for read-only diff/show; no bd, commits, HANDOFF.md, worklog, Makefile, or other-module implementation changes. All supplied display environments are :99. Corpus was reused without regeneration.

## Finding: deep-file G1 typing keeps layout busy

Original driver, unmodified implementation, command:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/editor_bench --track --no-idle --no-p4 --keys=100
```

Red (M)[AC]:

```text
STAMP (M)[AC] BAT0=Charging load1=0.79 TRACK shared box
BACKEND requested=null actual=null init_error=0
POSITION null lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
editor_bench:74 failed: bench_now_ns() < deadline
editor_bench:176 failed: settle(e) == 0
```

The requested position is floor(0.9 times the fixture's line count): target line 8,053,057 and byte 966,366,840 (M)[AC] in `/tmp/edit-corpus/log_1g.txt`. The first reproduction failed during its warmup, so it produced a deadline failure rather than the measured key row.

A second pre-fix run added only diagnostics and quiescent trace dumping. `tools/tracedump` on `build/edit-czn-evidence/build-edit-czn-red.trace` printed:

```text
G1 T4-T0     n=7 p50=859.150 ms p99=1021.060 ms
G3 T5-T0     n=7 p50=859.150 ms p99=1021.060 ms
T6-T0        n=7 p50=859.150 ms p99=1021.060 ms
```

These retained warmup frames and timings are (M)[AC], BAT0 Not charging, load1 1.58. Phase arithmetic from that same ring:

```text
T2-T1 n=7 p50_ns=859000795 p99_ns=1020772970 (M)[AC]
T3-T2 n=7 p50_ns=232435 p99_ns=247147 (M)[AC]
T4-T3 n=10 p50_ns=35107 p99_ns=41238 (M)[AC]
```

The deadline diagnostic was `view_busy=1 layout_busy=1 view_query_byte=966366840 layout_row=0 phase=0 mutations=8`. The long interval is **editor `repair` -> empty `view_command(TYPE)` -> `view_restore`/`normalize` -> `line_query`**. Normalize invalidates the nonzero certified anchors, so every repair scans the file prefix in cooperative short reads. This consumes many turns before T2; layout remains pending behind view. The trace disproves synchronous row layout as the primary source of the reproduced seconds-long delay. The old unconditional exact row query is also replaced so an unresolved row cannot demand an exact whole-file line start.

A focused test was written and run red before production changes. It creates a small synthetic large buffer, positions deep in it, replaces the index with an unbuilt index, and requires active line queries to start at the visible certified anchor. Red (M)[AC]:

```text
partial line-start: query_byte=433920 below viewport_byte=7549696
editor_test:798: FAIL !view_busy(&e->v) || !e->v.query_kind || e->v.query_pos >= start
```

The final test expands this to character insertion, Enter, and backspace at the beginning, the deep position, and near EOF. It checks frame submission, viewport/next-row starts, and that the index remains incomplete. A boundary extension was written red before its fix (M)[AC]:

```text
partial joined line-start: query_byte=1017088 below anchor_byte=7549568
editor_test:827: FAIL !view_busy(&e->v) || !e->v.query_kind || e->v.query_pos >= join - 128u
```

It deletes the newline immediately before the deep viewport and requires the joined line's preceding anchor, without replaying byte zero. Green:

```text
editor_test: partial index deep typing reuses viewport line starts passed
review 7: multiple long clipped lines preserve subsequent rows in first frame and edit/undo passed
```

Implementation: reuse current certified viewport anchors in the fixed view cache; retain a bounded certified preceding anchor through a line-join layout reset; reuse layout row starts; use capped tail scans or the existing partial lineidx query for unresolved clipped rows. No new globals, no typing-path malloc, and no lineidx changes. The decision is [edit-czn.md](../decisions/edit-czn.md).

## Back-to-back same-driver comparison

An isolated baseline library under `build/edit-czn-baseline` replaces only editor.c/input.c with HEAD versions obtained through read-only `git show`. Both variants use the diagnostic benchmark driver. The two commands ran consecutively on the loaded AC box, with no quiet-window wait. G1 reference is 1/2 ms p50/p99 (G). Red then green, exact pasted output below; trace lines carrying only `(M)` inherit the adjacent [AC] stamp in this earlier driver revision.

```text
STAMP (M)[AC] BAT0=Charging load1=16.94 TRACK shared box
BACKEND requested=null actual=null init_error=0
POSITION null lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
SETTLE deadline: view_busy=1 layout_busy=1 view_query_byte=653401088 layout_row=0 phase=0 mutations=1 slice_max_ns=16043798
TRACE null T2-T1 n=0 p50_ns=0 p99_ns=0 (M)
TRACE null T3-T2 n=0 p50_ns=0 p99_ns=0 (M)
TRACE null T4-T3 n=4 p50_ns=96434 p99_ns=106348 (M)
STAMP (M)[AC] BAT0=Charging load1=18.31 TRACK shared box
BACKEND requested=null actual=null init_error=0
POSITION null lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_null_ingress_submit_return_TRACK n=100 p50=384591 p99=6593891 ci95=[378374,393295] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_null_ingress_T4_present_TRACK n=100 p50=385043 p99=6595424 ci95=[378820,393823] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 null (M)[AC] load1=18.31 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=76637 mode=TRACK
TRACE null T2-T1 n=116 p50_ns=56732 p99_ns=137912 (M)
TRACE null T3-T2 n=116 p50_ns=248542 p99_ns=6465546 (M)
TRACE null T4-T3 n=120 p50_ns=76160 p99_ns=89155 (M)
STAMP (M)[AC] BAT0=Charging load1=19.33 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
POSITION raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_raster_ingress_submit_return_G1 n=100 p50=722321 p99=5937193 ci95=[705524,830822] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_raster_ingress_T4_present_G1 n=100 p50=5041064 p99=14424748 ci95=[4540417,6227278] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 raster (M)[AC] load1=19.33 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=67422 mode=TRACK
TRACE raster T2-T1 n=117 p50_ns=163797 p99_ns=221250 (M)
TRACE raster T3-T2 n=116 p50_ns=347017 p99_ns=579125 (M)
TRACE raster T4-T3 n=222 p50_ns=4603592 p99_ns=13395523 (M)
STAMP (M)[AC] BAT0=Charging load1=20.42 TRACK shared box
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
BACKEND requested=gl actual=cpu-raster init_error=-10
POSITION gl_fallback_raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_gl_fallback_raster_ingress_submit_return_G1 n=100 p50=663444 p99=6698234 ci95=[621939,694000] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_gl_fallback_raster_ingress_T4_present_G1 n=100 p50=4602441 p99=13654606 ci95=[3913358,6045387] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 gl_fallback_raster (M)[AC] load1=20.42 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=70270 mode=TRACK
TRACE gl_fallback_raster T2-T1 n=117 p50_ns=147640 p99_ns=952540 (M)
TRACE gl_fallback_raster T3-T2 n=116 p50_ns=335066 p99_ns=525833 (M)
TRACE gl_fallback_raster T4-T3 n=232 p50_ns=4086569 p99_ns=13127132 (M)
baseline_exit=1 fixed_exit=0
```

The baseline missed its settle deadline before T2 on the first mutation. The fixed driver completed every backend row and observed zero typing allocations (M)[AC]. Scheduler/display tails still exceed G1 in this loaded comparison. TRACK exit zero means the workload completed; it is not a gate pass. Xvfb could not initialize native EGL, so its GL row accurately reports raster fallback.

The final diagnostic driver was also compared consecutively after the forced rebuild. The baseline again failed its settle deadline; the fixed variant completed all typing rows. Pasted final red/green (M)[AC]:

```text
STAMP (M)[AC] BAT0=Not charging load1=32.94 TRACK shared box
BACKEND requested=null actual=null init_error=0
POSITION null lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
SETTLE deadline: view_busy=1 layout_busy=1 view_query_byte=647821568 layout_row=0 phase=0 mutations=4 slice_max_ns=10107863
TRACE null T2-T1 n=3 p50_ns=3825174784 p99_ns=3972728308 (M)[AC]
TRACE null T3-T2 n=3 p50_ns=336919 p99_ns=647231 (M)[AC]
TRACE null T4-T3 n=6 p50_ns=50078 p99_ns=107432 (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=33.58 TRACK shared box
BACKEND requested=null actual=null init_error=0
POSITION null lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_null_ingress_submit_return_TRACK n=100 p50=514368 p99=11052425 ci95=[481263,528129] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_null_ingress_T4_present_TRACK n=100 p50=515244 p99=11053782 ci95=[482211,528981] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 null (M)[AC] load1=33.58 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=3029245 mode=TRACK
TRACE null T2-T1 n=116 p50_ns=92339 p99_ns=3873403 (M)[AC]
TRACE null T3-T2 n=116 p50_ns=332081 p99_ns=10891875 (M)[AC]
TRACE null T4-T3 n=121 p50_ns=91279 p99_ns=119435 (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=33.62 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
POSITION raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_raster_ingress_submit_return_G1 n=100 p50=1699380 p99=7807651 ci95=[1110677,2700167] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_raster_ingress_T4_present_G1 n=100 p50=5053185 p99=19710129 ci95=[4275672,5923212] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 raster (M)[AC] load1=33.62 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=2466104 mode=TRACK
TRACE raster T2-T1 n=117 p50_ns=170960 p99_ns=874928 (M)[AC]
TRACE raster T3-T2 n=116 p50_ns=410848 p99_ns=3491366 (M)[AC]
TRACE raster T4-T3 n=187 p50_ns=5197724 p99_ns=19134573 (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=34.68 TRACK shared box
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
BACKEND requested=gl actual=cpu-raster init_error=-10
POSITION gl_fallback_raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_gl_fallback_raster_ingress_submit_return_G1 n=100 p50=1053024 p99=7485820 ci95=[925897,1248301] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_gl_fallback_raster_ingress_T4_present_G1 n=100 p50=4476339 p99=13245961 ci95=[3761055,5448244] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 gl_fallback_raster (M)[AC] load1=34.68 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=1235672 mode=TRACK
TRACE gl_fallback_raster T2-T1 n=117 p50_ns=171616 p99_ns=2965687 (M)[AC]
TRACE gl_fallback_raster T3-T2 n=116 p50_ns=396455 p99_ns=2039046 (M)[AC]
TRACE gl_fallback_raster T4-T3 n=188 p50_ns=4177154 p99_ns=15983339 (M)[AC]
baseline_exit=1 fixed_exit=0
```

This final red trace directly attributes the delay to T2-T1, with T3-T2 much smaller. The cap/anchor refinement therefore preserves the original causal improvement. The fixed native-backend tails still do not establish G1 gate acceptance.

## Forced incomplete-index G1 rows

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_TRACE_DUMP=build/edit-czn-evidence/build-edit-czn-partial-green.trace build/bench/editor_bench --track --partial-index --no-idle --no-p4 --keys=1000
```

The allocating setup replaces the index after positioning and before warmup/measurement. It forces incomplete metadata independently of worker scheduling. Pasted output (M)[AC]:

```text
STAMP (M)[AC] BAT0=Charging load1=22.95 TRACK shared box
BACKEND requested=null actual=null init_error=0
POSITION null lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
INDEX null deliberately_unbuilt before warmup; viewport anchor retained
BENCH name=editor_null_ingress_submit_return_TRACK n=1000 p50=982244 p99=3628945 ci95=[973628,989933] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_null_ingress_T4_present_TRACK n=1000 p50=983356 p99=3630360 ci95=[974457,991251] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 null (M)[AC] load1=22.95 keys=1000 allocations=0 mutations=1016 journal=1016 slice_max_ns=3031006 mode=TRACK
TRACE null T2-T1 n=1016 p50_ns=98118 p99_ns=2651054 (M)[AC]
TRACE null T3-T2 n=1016 p50_ns=635364 p99_ns=3395701 (M)[AC]
TRACE null T4-T3 n=1020 p50_ns=85848 p99_ns=276287 (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=23.20 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
POSITION raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
INDEX raster deliberately_unbuilt before warmup; viewport anchor retained
BENCH name=editor_raster_ingress_submit_return_G1 n=1000 p50=684002 p99=5943534 ci95=[658051,707244] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_raster_ingress_T4_present_G1 n=1000 p50=1882762 p99=13099670 ci95=[1576154,2049974] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 raster (M)[AC] load1=23.20 keys=1000 allocations=0 mutations=1016 journal=1016 slice_max_ns=5993863 mode=TRACK
TRACE raster T2-T1 n=1017 p50_ns=136764 p99_ns=1981370 (M)[AC]
TRACE raster T3-T2 n=1016 p50_ns=308282 p99_ns=1907056 (M)[AC]
TRACE raster T4-T3 n=1802 p50_ns=1451138 p99_ns=12285222 (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=20.43 TRACK shared box
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
BACKEND requested=gl actual=cpu-raster init_error=-10
POSITION gl_fallback_raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
INDEX gl_fallback_raster deliberately_unbuilt before warmup; viewport anchor retained
BENCH name=editor_gl_fallback_raster_ingress_submit_return_G1 n=1000 p50=340440 p99=529384 ci95=[336861,343832] gate_p50=1000000 gate_p99=2000000 pass=1 power=[AC]
BENCH name=editor_gl_fallback_raster_ingress_T4_present_G1 n=1000 p50=798291 p99=2102991 ci95=[792290,806502] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 gl_fallback_raster (M)[AC] load1=20.43 keys=1000 allocations=0 mutations=1016 journal=1016 slice_max_ns=121467 mode=TRACK
TRACE gl_fallback_raster T2-T1 n=1017 p50_ns=85334 p99_ns=160799 (M)[AC]
TRACE gl_fallback_raster T3-T2 n=1016 p50_ns=178162 p99_ns=255692 (M)[AC]
TRACE gl_fallback_raster T4-T3 n=2015 p50_ns=546646 p99_ns=1928280 (M)[AC]
```

These rows completed, with no allocation-guard calls recorded on typing. Loaded-box percentile misses remain visible. They establish elimination of the reproduced prefix-repair stall, not universal native-display G1 certification.

## Final verification and existing rows

Raw logs and trace dumps were moved into the ignored `build/edit-czn-evidence/` directory after execution. Commands below use those relocated output paths for reproduction. The pasted results are unchanged.

Final-source command (M)[AC], exit zero:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 all check build/fuzz/editor_fuzz
```

Pasted green:

```text
editor_test: partial index deep typing reuses viewport line starts passed
editor_test: all passed
check: 59 test binaries passed
test_replay_cli: all passed
```

Final editor fuzz command and pasted green, exit zero (M)[AC]:

```sh
ASAN_OPTIONS=detect_leaks=0 build/fuzz/editor_fuzz -max_total_time=60 -max_len=1024 -artifact_prefix=build/edit-czn-final-fuzz-
```

```text
#12821	DONE   cov: 12408 ft: 34622 corp: 398/3123b lim: 14 exec/s: 210 rss: 370Mb
Done 12821 runs in 61 second(s)
```

The duration was 61 seconds (M)[AC] against 60 seconds (G). An earlier clean run executed 4,289 cases in 61 seconds (M)[AC]. Both used ASan/UBSan with leaks disabled.

The initial fixed implementation also completed the full existing matrix, with 10,000 measured keys per typing backend (M)[AC], exit zero in TRACK mode. This campaign started before the boundary regression extension; the shorter final-source matrix separately confirms that extension. Pasted full-row output:

```text
STAMP (M)[AC] BAT0=Not charging load1=22.95 TRACK shared box
BACKEND requested=null actual=null init_error=0
POSITION null lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_null_ingress_submit_return_TRACK n=10000 p50=463945 p99=6819991 ci95=[462518,465354] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_null_ingress_T4_present_TRACK n=10000 p50=464710 p99=6821186 ci95=[463185,465985] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 null (M)[AC] load1=22.95 keys=10000 allocations=0 mutations=10016 journal=10016 slice_max_ns=10033575 mode=TRACK
TRACE null T2-T1 n=9362 p50_ns=107286 p99_ns=3049221 (M)[AC]
TRACE null T3-T2 n=9362 p50_ns=274504 p99_ns=6486733 (M)[AC]
TRACE null T4-T3 n=9362 p50_ns=82163 p99_ns=279877 (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=23.50 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
POSITION raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_raster_ingress_submit_return_G1 n=10000 p50=398430 p99=3211349 ci95=[396451,400331] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_raster_ingress_T4_present_G1 n=10000 p50=872161 p99=5056341 ci95=[867722,877338] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 raster (M)[AC] load1=23.50 keys=10000 allocations=0 mutations=10016 journal=10016 slice_max_ns=2948493 mode=TRACK
TRACE raster T2-T1 n=6060 p50_ns=164305 p99_ns=318790 (M)[AC]
TRACE raster T3-T2 n=6060 p50_ns=173259 p99_ns=455522 (M)[AC]
TRACE raster T4-T3 n=11839 p50_ns=535821 p99_ns=2647358 (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=4.68 TRACK shared box
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
BACKEND requested=gl actual=cpu-raster init_error=-10
POSITION gl_fallback_raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_gl_fallback_raster_ingress_submit_return_G1 n=10000 p50=514857 p99=4433170 ci95=[508742,519706] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_gl_fallback_raster_ingress_T4_present_G1 n=10000 p50=1363208 p99=5994077 ci95=[1351820,1375017] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 gl_fallback_raster (M)[AC] load1=4.68 keys=10000 allocations=0 mutations=10016 journal=10016 slice_max_ns=5258020 mode=TRACK
TRACE gl_fallback_raster T2-T1 n=6506 p50_ns=181982 p99_ns=1111169 (M)[AC]
TRACE gl_fallback_raster T3-T2 n=6506 p50_ns=227782 p99_ns=2612552 (M)[AC]
TRACE gl_fallback_raster T4-T3 n=11504 p50_ns=991678 p99_ns=5304034 (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=15.89 TRACK shared box
BACKEND requested=null actual=null init_error=0
BENCH name=editor_null_G11_process_cpu n=19 p50=179316 p99=218837 ci95=[176323,181928] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 null (M)[AC] load1=15.89 blinks=19 poll_returns=20 wakeups/s=1.997 idle=0 unfocused=0 (G)<=2/s,0,0 mode=TRACK
STAMP (M)[AC] BAT0=Charging load1=15.96 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
BENCH name=editor_raster_G11_process_cpu n=19 p50=526458 p99=745875 ci95=[435725,647675] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 raster (M)[AC] load1=15.96 blinks=19 poll_returns=86 wakeups/s=8.600 idle=0 unfocused=0 (G)<=2/s,0,0 mode=TRACK
STAMP (M)[AC] BAT0=Charging load1=13.90 TRACK shared box
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
BACKEND requested=gl actual=cpu-raster init_error=-10
BENCH name=editor_gl_fallback_raster_G11_process_cpu n=19 p50=506619 p99=666883 ci95=[375883,606832] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 gl_fallback_raster (M)[AC] load1=13.90 blinks=19 poll_returns=85 wakeups/s=8.487 idle=0 unfocused=0 (G)<=2/s,0,0 mode=TRACK
STAMP (M)[AC] BAT0=Not charging load1=12.38 TRACK shared box
BENCH name=editor_null_100tabs_ingress_T5_G3 n=200 p50=271880 p99=298983 ci95=[270640,272817] gate_p50=5000000 gate_p99=5560000 pass=1 power=[AC]
BENCH name=editor_null_100tabs_minimap_inside_frame n=200 p50=6255 p99=9114 ci95=[6247,6264] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
G3 null (M)[AC] load1=12.38 tabs=100 A=2880x1800 switches=200 allocations=0 minimap_fills=201 stale=0 mode=TRACK
STAMP (M)[AC] BAT0=Not charging load1=12.38 TRACK shared box
BENCH name=editor_raster_100tabs_ingress_T5_G3 n=200 p50=6830504 p99=15013107 ci95=[6496148,7075587] gate_p50=5000000 gate_p99=5560000 pass=0 power=[AC]
BENCH name=editor_raster_100tabs_minimap_inside_frame n=200 p50=8270 p99=13411 ci95=[8184,8355] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
G3 raster (M)[AC] load1=12.38 tabs=100 A=2880x1800 switches=200 allocations=0 minimap_fills=202 stale=0 mode=TRACK
STAMP (M)[AC] BAT0=Not charging load1=12.19 TRACK shared box
BENCH name=editor_second_invocation_exec_open_ACK_exit n=64 p50=3493167 p99=4412351 ci95=[3316686,3545436] gate_p50=0 gate_p99=10000000 pass=1 power=[AC]
IPC (M)[AC] load1=12.19 invocations=64 opened_tabs=64 includes=exec,parse,open,ACK,exit,reap mode=TRACK
```

The main G1 reference is 1/2 ms p50/p99 (G). All full typing rows observed zero typing allocations (M)[AC]. The raster/GPU-fallback tails, G11 CPU/wake rows, and raster tab-switch row show existing loaded-box misses; no renderer or idle-policy change is made. IPC and minimap rows completed. TRACK success only means completion/correctness checks succeeded.

The host clock moved backward during final verification. Source/object timestamps show that the final production implementation had already compiled and passed the sanitizer/fuzz checks before that correction. A forced `make -B -j4 all` is used to ensure the late benchmark diagnostic cleanup also compiles, independent of timestamps. Timing samples and benchmark deadlines use CLOCK_MONOTONIC, so the wall-clock correction is not a latency measurement.

Final build confirmation after the host clock correction, exit zero (M)[AC]:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make -B -j4 all
```

All release objects and binaries were rebuilt with gcc and the strict warning flags. Make itself emitted a clock-skew warning because existing input timestamps are ahead of the corrected clock. Forcing every target prevents that warning from concealing an omitted compile; there were no C compiler warnings/errors.

Final-source matrix confirmation, exit zero in TRACK mode (M)[AC]:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_TRACE_DUMP=build/edit-czn-evidence/build-edit-czn-final-matrix.trace build/bench/editor_bench --track --keys=100
```

```text
STAMP (M)[AC] BAT0=Charging load1=18.38 TRACK shared box
BACKEND requested=null actual=null init_error=0
POSITION null lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_null_ingress_submit_return_TRACK n=100 p50=372872 p99=3800712 ci95=[368472,385026] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_null_ingress_T4_present_TRACK n=100 p50=373321 p99=3802441 ci95=[369003,385477] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 null (M)[AC] load1=18.38 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=3032320 mode=TRACK
TRACE null T2-T1 n=116 p50_ns=59618 p99_ns=166172 (M)[AC]
TRACE null T3-T2 n=116 p50_ns=239249 p99_ns=3660992 (M)[AC]
TRACE null T4-T3 n=119 p50_ns=74783 p99_ns=92356 (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=18.38 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
POSITION raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_raster_ingress_submit_return_G1 n=100 p50=1097947 p99=6688752 ci95=[761530,2698988] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_raster_ingress_T4_present_G1 n=100 p50=6441676 p99=15690008 ci95=[5240500,8308292] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 raster (M)[AC] load1=18.38 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=409583 mode=TRACK
TRACE raster T2-T1 n=117 p50_ns=164063 p99_ns=414385 (M)[AC]
TRACE raster T3-T2 n=116 p50_ns=371940 p99_ns=1153903 (M)[AC]
TRACE raster T4-T3 n=216 p50_ns=6313565 p99_ns=15200826 (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=20.31 TRACK shared box
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
BACKEND requested=gl actual=cpu-raster init_error=-10
POSITION gl_fallback_raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
BENCH name=editor_gl_fallback_raster_ingress_submit_return_G1 n=100 p50=394052 p99=2729617 ci95=[385136,408567] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_gl_fallback_raster_ingress_T4_present_G1 n=100 p50=2397925 p99=8438740 ci95=[2162465,2526665] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 gl_fallback_raster (M)[AC] load1=20.31 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=73465 mode=TRACK
TRACE gl_fallback_raster T2-T1 n=117 p50_ns=97514 p99_ns=329960 (M)[AC]
TRACE gl_fallback_raster T3-T2 n=116 p50_ns=189753 p99_ns=274408 (M)[AC]
TRACE gl_fallback_raster T4-T3 n=235 p50_ns=2014198 p99_ns=6323192 (M)[AC]
STAMP (M)[AC] BAT0=Not charging load1=22.02 TRACK shared box
BACKEND requested=null actual=null init_error=0
BENCH name=editor_null_G11_process_cpu n=19 p50=99501 p99=109104 ci95=[96982,103735] gate_p50=100000 gate_p99=200000 pass=1 power=[AC]
G11 null (M)[AC] load1=22.02 blinks=19 poll_returns=20 wakeups/s=1.997 idle=0 unfocused=0 (G)<=2/s,0,0 mode=TRACK
STAMP (M)[AC] BAT0=Not charging load1=25.70 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
BENCH name=editor_raster_G11_process_cpu n=19 p50=800522 p99=1033925 ci95=[694315,941145] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 raster (M)[AC] load1=25.70 blinks=19 poll_returns=85 wakeups/s=8.471 idle=0 unfocused=0 (G)<=2/s,0,0 mode=TRACK
STAMP (M)[AC] BAT0=Not charging load1=26.98 TRACK shared box
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
BACKEND requested=gl actual=cpu-raster init_error=-10
BENCH name=editor_gl_fallback_raster_G11_process_cpu n=19 p50=1172453 p99=1747333 ci95=[1039774,1408620] gate_p50=100000 gate_p99=200000 pass=0 power=[AC]
G11 gl_fallback_raster (M)[AC] load1=26.98 blinks=19 poll_returns=94 wakeups/s=9.380 idle=0 unfocused=0 (G)<=2/s,0,0 mode=TRACK
STAMP (M)[AC] BAT0=Charging load1=27.56 TRACK shared box
BENCH name=editor_null_100tabs_ingress_T5_G3 n=200 p50=854014 p99=11084890 ci95=[845381,868981] gate_p50=5000000 gate_p99=5560000 pass=0 power=[AC]
BENCH name=editor_null_100tabs_minimap_inside_frame n=200 p50=16693 p99=26356 ci95=[16605,16780] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
G3 null (M)[AC] load1=27.56 tabs=100 A=2880x1800 switches=200 allocations=0 minimap_fills=201 stale=0 mode=TRACK
STAMP (M)[AC] BAT0=Charging load1=27.56 TRACK shared box
BENCH name=editor_raster_100tabs_ingress_T5_G3 n=200 p50=36095388 p99=58466276 ci95=[34833193,37835681] gate_p50=5000000 gate_p99=5560000 pass=0 power=[AC]
BENCH name=editor_raster_100tabs_minimap_inside_frame n=200 p50=17452 p99=32322 ci95=[17317,17556] gate_p50=0 gate_p99=500000 pass=1 power=[AC]
G3 raster (M)[AC] load1=27.56 tabs=100 A=2880x1800 switches=200 allocations=0 minimap_fills=201 stale=0 mode=TRACK
STAMP (M)[AC] BAT0=Charging load1=27.89 TRACK shared box
BENCH name=editor_second_invocation_exec_open_ACK_exit n=64 p50=18840700 p99=36941611 ci95=[17735891,20746027] gate_p50=0 gate_p99=10000000 pass=0 power=[AC]
IPC (M)[AC] load1=27.89 invocations=64 opened_tabs=64 includes=exec,parse,open,ACK,exit,reap mode=TRACK
```

Final-source forced-partial confirmation, exit zero in TRACK mode (M)[AC]:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_TRACE_DUMP=build/edit-czn-evidence/build-edit-czn-final-partial.trace build/bench/editor_bench --track --partial-index --no-idle --no-p4 --keys=100
```

```text
STAMP (M)[AC] BAT0=Charging load1=19.47 TRACK shared box
BACKEND requested=null actual=null init_error=0
POSITION null lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
INDEX null deliberately_unbuilt before warmup; viewport anchor retained
BENCH name=editor_null_ingress_submit_return_TRACK n=100 p50=497594 p99=4067835 ci95=[488197,510258] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_null_ingress_T4_present_TRACK n=100 p50=498130 p99=4068985 ci95=[488698,511149] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 null (M)[AC] load1=19.47 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=4044567 mode=TRACK
TRACE null T2-T1 n=116 p50_ns=88589 p99_ns=3621826 (M)[AC]
TRACE null T3-T2 n=116 p50_ns=321873 p99_ns=3633194 (M)[AC]
TRACE null T4-T3 n=118 p50_ns=85607 p99_ns=111295 (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=19.47 TRACK shared box
BACKEND requested=raster actual=cpu-raster init_error=0
POSITION raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
INDEX raster deliberately_unbuilt before warmup; viewport anchor retained
BENCH name=editor_raster_ingress_submit_return_G1 n=100 p50=785741 p99=6594510 ci95=[724198,1215979] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_raster_ingress_T4_present_G1 n=100 p50=3172559 p99=14221235 ci95=[2860990,3979627] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 raster (M)[AC] load1=19.47 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=7742462 mode=TRACK
TRACE raster T2-T1 n=117 p50_ns=165349 p99_ns=6076172 (M)[AC]
TRACE raster T3-T2 n=116 p50_ns=369779 p99_ns=3127950 (M)[AC]
TRACE raster T4-T3 n=167 p50_ns=2275496 p99_ns=13685355 (M)[AC]
STAMP (M)[AC] BAT0=Charging load1=21.33 TRACK shared box
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
BACKEND requested=gl actual=cpu-raster init_error=-10
POSITION gl_fallback_raster lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
INDEX gl_fallback_raster deliberately_unbuilt before warmup; viewport anchor retained
BENCH name=editor_gl_fallback_raster_ingress_submit_return_G1 n=100 p50=411472 p99=3633602 ci95=[400899,416803] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
BENCH name=editor_gl_fallback_raster_ingress_T4_present_G1 n=100 p50=2510728 p99=7470737 ci95=[2435612,2608648] gate_p50=1000000 gate_p99=2000000 pass=0 power=[AC]
G1 gl_fallback_raster (M)[AC] load1=21.33 keys=100 allocations=0 mutations=116 journal=116 slice_max_ns=201159 mode=TRACK
TRACE gl_fallback_raster T2-T1 n=117 p50_ns=104747 p99_ns=4016969 (M)[AC]
TRACE gl_fallback_raster T3-T2 n=116 p50_ns=196730 p99_ns=385671 (M)[AC]
TRACE gl_fallback_raster T4-T3 n=207 p50_ns=2173187 p99_ns=8770533 (M)[AC]
```

These confirmations ran on the loaded box rather than waiting for quieter measurements. Their reported misses remain in the record. No single timing is treated as a gate verdict.

LeakSanitizer is disabled with `ASAN_OPTIONS=detect_leaks=0` as required in the sandbox. The coordinator must rerun with leaks enabled. Compiler versions are gcc 13.3 and clang 18.1 (M)[AC]; the build uses C11, -Wall -Wextra -Werror -Wshadow -Wconversion.

## Missing / outside scope

G1 gate certification is still open: shared-box p99 tails miss 2 ms (G), and Xvfb EGL uses raster fallback. The pre-existing G1/G11 geometry getter mismatch is unchanged and its printed A geometry is not an assertion of physical pixels; the P4 tab row asserts its own actual geometry. Native-display behavior and cold page-fault latency are not established.

Index repair/restart policy (edit-zzj.16), index metadata costs (edit-zzj.13 finding 10), whole-file open/setup, giant selection/history/column queries, and renderer/G11 misses are outside scope and unchanged. If a preceding real line start lies beyond the backward cap, view retains its cooperative fallback rather than importing an unproved anchor. Thus this change does not prove arbitrary giant-line view/column work meets G1. The editor's layout row-start path itself performs no synchronous whole-file scan. No other finding was fixed.
