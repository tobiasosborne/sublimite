# edit-5o0 session 9 worker report

This bead is **incomplete**. Implemented: sections 7, 8, 9, 11, 34. Partially implemented: sections 15 and 16. Explicit failing acceptance diagnostics remain for sections 10, 12, and 38. No binding gate or ownership contract was relaxed.

Read CLAUDE.md, the supplied bead, named P2-1 sections, and raster STATUS.md. Worked in finding order. Product changes are confined to raster/render; the G11 benchmark hunk is local and leaves sample counts unchanged. No new globals, Git mutations, `bd`, HANDOFF.md edits, or docs/worklog edits. Native commands use DISPLAY=:99 and EDIT_DISPLAY=:99 exclusively. Design choices: [edit-5o0 decision](../decisions/edit-5o0.md).

All test counts and measured values pasted below are (M)[AC]; BAT0 reports `Not charging`. The shared host is loaded. No single timing is a gate verdict. LeakSanitizer is disabled with `ASAN_OPTIONS=detect_leaks=0`; coordinator must rerun with leaks enabled.

## Per finding

- **7 done:** submit never sleeps or cancels jobs. Authenticated quiescence permits snapshot reuse; fixed retirement storage retains physical leases through delayed return. Exhaustion returns BUSY before changing the snapshot. Shutdown joins both current and retired jobs. Original `fence_outlives_test` stays green with stronger authoritative-retirement assertions.
- **8 done:** common raster event routing rejects raw platform device/present signals. Only backend-owned authenticated work/native observations deliver retirement and diagnostics. The regression preserves server pixmap ownership until authoritative observation.
- **9 done:** initialize/resize clears the entire native pixmap, covering fractional-cell margins. Raster runs the shared native resize contract. The initial fixture incorrectly compared opaque alpha with RGB; corrected readback normalization, reran the old implementation RED, then restored the clear fix GREEN.
- **10 unresolved gate decision:** ASan allocation census covers default production editor startup and forced failed-EGL-to-raster startup. The owned peak lower bound already fails G10. Heap census excludes mmap arenas/stacks/transient peaks; native pixmap payload is derived, not a server allocator measurement. Default policy is in excluded `editor/open.c`; no memory acceptance claimed.
- **11 done admission, larger-grid preparation missing:** reject above glyph/page/cell limits before scanning, including zero damage. Full admitted-cell validation remains. Larger grids are refused; resumable/versioned validation is not implemented.
- **12 unresolved:** explicit active-frame-arrival probe remains RED. Queue-depth-one blocks acceptance until T6. Safe superseding/coalescing and a real refresh-phase fixture are missing; no G1 contract revision.
- **15 partial:** deadline, missing-ack tests, failure latch, recovery refusal, and remaining-time API implemented. Editor damage-blocked wait must call that API; `editor/editor.c` is excluded from this worker's edit set. Without integration an eventless final wait can still delay failure observation indefinitely.
- **16 partial:** CPU accounting retains later completion and timeout CPU. Process-wide context switches reveal worker sleeps and are labelled TRACK. Exact all-thread wakeup tracing remains missing; qualifying G11 returns unmeasured. Parallel worker edit-9jo owns sample-count enlargement, unchanged here.
- **34 done:** real-backend ownership operation stream, deterministic transport, independent physical/native ownership assertions, mandatory delayed-return schedule, cancellation, saturation, foreign events, partial/missing native acknowledgements, failed initialization, shutdown/replacement, and inline expiry. Existing kernel differential/cancellation fuzzing retained.
- **38 unresolved/outside source edit set:** explicit real-implementation mapped-lease resize probe remains RED. Requires GL resize refusal and allocation-free lease cancellation in excluded `src/gl/renderer.inc`/`gl.h`.

## Pasted red and green evidence

Commands use `build/tests/raster_test --review <case>` unless noted. Exit 1 is expected for explicit unresolved gate diagnostics; they are deliberately outside the passing correctness suite.

### Section 7

RED, before fix:
```text
raster_test:811: FAIL rc==RENDER_OK && review_rollback_waits==0
P2-1 section 7: submit=0 UI_sleeps=990
```
GREEN:
```text
P2-1 section 7: submit=0 UI_sleeps=0
```
Additional assertions verify retained handles, shutdown reclamation, full retirement storage BUSY, and reclamation after physical return.

### Section 8

RED:
```text
raster_test:826: FAIL rc==RENDER_ERR_UNSUPPORTED && b.active && !b.complete_seen
P2-1 section 8: raw completion=0 active=0
```
GREEN:
```text
P2-1 section 8: raw completion=-10 active=1
```

### Section 9

Corrected native test against uncleared implementation RED:
```text
native resize step=2 pixel=6,0 got=00244668 expected=00000000
render_test:465: FAIL pixels[(size_t)y*width+x]==expected
```
GREEN:
```text
render native resize: PASS (grow/shrink, changed cells, fractional margins, native window pixels)
```

### Section 10 — still RED, no green claim

`ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review P2-1-10-gate`:
```text
raster_test:884: FAIL !miss
P2-1 section 10: default-raster private=3296201 SHM=51840000 (M)[AC] native_pixmap_payload=51840000 (E) owned_peak_lower_bound=106976201 gate=87000000 (G) RED
P2-1 section 10: failed-EGL-to-raster private=3305834 SHM=51840000 (M)[AC] native_pixmap_payload=51840000 (E) owned_peak_lower_bound=106985834 gate=87000000 (G) RED
```
The final diagnostic labels `private` as `heap_allocations` to make the census limitation explicit. Combined lower bounds above are (E), containing measured heap/SHM plus derived native payload. Both startup paths were observed back to back; no peak-memory pass claimed.

### Section 11

RED:
```text
raster_test:956: FAIL page_rc==RENDER_ERR_CAPACITY && cell_rc==RENDER_ERR_CAPACITY && review_submit_calls==0
P2-1 section 11: excess pages=0
P2-1 section 11: excess cells=0
```
GREEN:
```text
P2-1 section 11: excess pages=-3
P2-1 section 11: excess cells=-3
```
Maximum admitted table, back-to-back TRACK before/after:
```text
TYPING_BUDGET_TRACK cells=65536 glyphs=4096 pages=64 samples=256 p50=193439 ns p99=415358 ns allocations=0 guard=1 (M)[AC]
TYPING_BUDGET_TRACK cells=65536 glyphs=4096 pages=64 samples=256 p50=167092 ns p99=263045 ns allocations=0 guard=1 (M)[AC]
```
Load1 after pair: 5.91 (M)[AC]. Validation/snapshot/enqueue only, deterministic paused scheduler; no real-display/G1/G3 acceptance. The unchanged full scan is now admission-bounded, not accelerated.

### Section 12 — still RED, no green claim

`--review P2-1-12-gate`:
```text
raster_test:972: FAIL rc==RENDER_OK
P2-1 section 12: edited frame arrival while T6 pending submit=-8 (BUSY=-8)
```
Structural probe, not a real refresh-phase measurement.

### Section 15

RED:
```text
raster_test:1317: FAIL rc==RENDER_ERR_DEVICE && st.failed && b.active && seen.t6_count==0
P2-1 section 15: missing=fence expiry=0
```
GREEN, independent missing acknowledgements and latched failure/retry checks:
```text
P2-1 section 15: missing=fence expiry=-11
P2-1 section 15: missing=pixmap expiry=-11
P2-1 section 15: missing=idle expiry=-11
```
Timeout rounding/clamping is asserted. Production wait integration remains missing.

### Section 16

`build/bench/editor_bench --self-check-g11` RED:
```text
editor_bench:248 failed: cpu.n==1 && values[0]==600 && !window.pending
P2-1 section 16: delayed completion samples=1 total=100
```
GREEN:
```text
P2-1 section 16: delayed completion samples=1 total=600
G11 accounting: timeout CPU retained; process switches=5 UI=1 (M)[AC] worker sleeps visible
G11 accounting: delayed completion CPU retained through retirement
```
Scripted CPU units above establish accounting, not actual CPU timing. Context-switch count is measured; it is not an exact wakeup verdict.

A native TRACK-row run found a terminal accounting case RED:
```text
BACKEND requested=raster actual=cpu-raster init_error=0
editor_bench:337 failed: !window.pending
```
The correction carries the final pending window through the benchmark's existing quiet-observation turn, then drains ready raster acknowledgements without an extra wake/timer. Completion/quiet-turn CPU is included conservatively in the final sample. No new scope was added after the cutoff; this corrects the existing G11 change. Final native row GREEN (functional TRACK run, exit 0):
```text
BENCH name=editor_raster_G11_process_cpu n=19 required_n=10000 p50=185146 p99=710034 ci95_p50=[155360,212296] ci95_p99=[217765,710034] gate_p50=100000 gate_p99=200000 dropped=0 (M)[AC] power=Not charging load1=6.70 verdict=TRACK
G11_schedule_TRACK raster process_context_switches=65 (M)[AC] wakeup trace unavailable; qualifying wakeup gate unmeasured
G11 raster (M)[AC] load1=6.70 blinks=19 poll_returns=20 wakeups/s=2.006 idle=0 unfocused=0 (G)<=2/s,0,0 mode=TRACK structural=OK
G11 raster structural worker_jobs=0 inline_frames=19
```
Command: `DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_BACKEND=raster build/bench/editor_bench --track --idle-only`. It runs null then raster back to back; null's CPU p50/p99 were 93964/111610 ns (M)[AC], load1=6.63. These measurements demonstrate row completion/accounting, not a gate pass or a controlled performance improvement. Raster CPU values exceed the nominal 100000/200000 ns (G) limits and the distribution has only nineteen samples (M)[AC]. Context-switch scope includes the quiet retirement observation and overcounts preemption. Exact wakeups remain unmeasured.

The final G11 correction was limited to the existing finding after the scope cutoff. `make all` exited 0 afterward, and its dedicated self-check passed under Clang ASan/UBSan with the required warning flags. The previously successful full `make check` consumed unchanged product/test code; the final correction changes only the benchmark.

### Section 34

New ownership fuzzer compiled against `git show HEAD:src/raster/raster.c` saved to `build/raster-before-s9.c`, with `-DRASTER_FUZZ_OLD`, RED on its mandatory first schedule:
```text
fuzz/raster_fuzz.c:183: assertion failed: t->shutdown || t->phase[h.slot]!=2
ERROR: libFuzzer: deadly signal
SUMMARY: libFuzzer: deadly signal
```
This reproduces cancellation/join of a published but physically live lease on the next submit. The product fix preceded the expanded fuzzer because findings were handled in requested order. The baseline replay proves the new stream exercises the old defect.

GREEN smoke against current backend:
```text
#100 DONE
Done 100 runs in 0 second(s)
```
Final ownership + kernel fuzz run GREEN (M)[AC], start load1=5.56:
```text
Done 63327 runs in 61 second(s)
```
Command: `ASAN_OPTIONS=detect_leaks=0 build/fuzz/raster_fuzz -max_total_time=60 -artifact_prefix=build/`. Exit 0. The empty-input crash artifact from the old backend also replays cleanly against the current backend; replay is a fixed-input test, not a second fuzz run.

### Section 38 — still RED, no green claim

`build/tests/render_test --review P2-1-38-gate`:
```text
render_test:525: FAIL rc==RENDER_ERR_BUSY && grid.cells==slots && state.dims.cols==2
P2-1 section 38: resize during mapped lease=0 (BUSY=-8) leased=1
```

## Validation and remaining acceptance

Release/native focused run GREEN:
```text
raster pixels: PASS inline caret, full-window scalar comparison and zero worker jobs
raster pixels: PASS full + 10 individual partial comparisons (disconnected/boundaries/unchanged rows)
raster fence: PASS (raw completion rejected, queued fence job joined before backend release)
render native resize: PASS (grow/shrink, changed cells, fractional margins, native window pixels)
raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)
```

Final `make all` exits 0 (GCC 13.3.0 release), with the required C11 `-Wall -Wextra -Werror -Wshadow -Wconversion` flags. Clang is 18.1.3. `git diff --check` passes. No corpus regeneration or performance gate exception.

First full `make check` exited 2 at `refwin_test`, after raster sanitizer conformance passed. That unchanged test exited before its preflight success message; isolated rerun passed the full editor/reference pair test. Cause is not established; shared-display contention is an inference, not a diagnosed fault. No out-of-scope test/tool/platform source was changed. Final release raster allocation conformance exits 0:
```text
raster law2: windows=10000 allocations=0 guard=1 scope=input->submit
render no-malloc: 10000 frames, 0 allocations
raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)
```
The G11 self-check also passes under Clang ASan/UBSan with required warnings. Final full `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check` exits 0:
```text
check: 60 test binaries passed
test_replay_cli: all passed
```
The first-attempt refwin failure did not recur. Expected negative-control diagnostic lines inside passing suites are not suite failures.

Raw evidence logs are under `build/edit-5o0-evidence/` (ignored build artifacts); relevant output is pasted here so this report stands alone.

Outstanding: G10 memory policy/census completion; G1 superseding and real refresh fixture; editor wait-deadline integration; authenticated all-thread G11 wakeups and qualifying sample distribution; GL lease refusal/cancel API; coordinator LeakSanitizer rerun. These prevent bead acceptance even if build/correctness/fuzz validation succeeds.
