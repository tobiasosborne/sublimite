# edit-e6x.16 — refresh-paced scrolling TRACK contract

Tobias observed mostly smooth scrolling with occasional small jumps in session
5. The raster and picked EGL benches now have a separate diagnostic workload:
one displayed baseline followed by exactly 600 full-grid scroll frames. Each
frame shifts the existing cells up one cell row, fills the exposed row, marks
the whole grid dirty, submits, presents, and waits for its matching completion.
The baseline establishes the first MSC and completion timestamp; its timings
are excluded from the stage samples. `--quick` does not shorten this workload.

## Pacing and measurements

- Raster uses the existing Present pixmap request: COPY, without ASYNC, target
  MSC zero (the next eligible refresh). No timer, target-A/B nominal Hz, or
  NotifyMSC acknowledgement substitutes for a displayed pixmap completion.
- EGL initializes a dedicated context with `EDIT_GL_SWAP_INTERVAL=1`, then uses
  the existing independently verified matching PIXMAP Present completions.
  The normal bench restores its prior interval setting after this workload.
- Queue depth remains one, as supported by the backends. Rendering the next
  frame begins after the previous frame completes. Render work, scheduling,
  mailbox latency, and native presentation all contribute to cadence misses.
- Consecutive increasing displayed MSCs contribute `current - previous - 1`
  missed refreshes. Duplicate/regressing IDs are reported as invalid and do
  not lower the previous high-water ID. Missing MSCs break the interval chain;
  `msc_intervals` records coverage and `missed_refreshes=unavailable` denotes
  no comparable pair. Raster also reports Present SKIP/wrong-kind completions
  as dropped. GL's current public diagnostics omit mode: its dropped count is
  explicitly `unavailable`.
- `longest_stall_ms` is the maximum full interval between matching completion
  observation timestamps, CLOCK_MONOTONIC, including the normal refresh
  interval. It is not excess time above an assumed refresh period. Dispatch
  delay can affect this number; MSC is the refresh-miss source. X UST is never
  mixed with CLOCK_MONOTONIC.
- Every stage retains individual samples and prints nearest-rank p50 and p99
  in ns. Raster includes grid mutation/damage, submit, ingress-to-T5, each
  strip, each worker queue, ready/present/upload stages, T5-to-completion, and
  completion intervals. EGL includes mutation/damage, submit, native
  present/swap, swap-return-to-T5, T5-to-completion, ingress-to-T5, and intervals.
  EGL's native stage includes deferred atlas/VBO work and swap waiting; finer
  GPU draw/upload separation is not observable through the current interface.
- `bench/render_pace.h` owns sampling, accounting, stamps and formatting.
  Storage allocation and battery/load reads precede input-to-submit. Both
  callbacks guard the actual scrolling mutation through submit for allocations.
  Reporting occurs after collection, outside the typing path.

Every new line begins `TRACK`, with `verdict=TRACK`, `(M)` plus power tag,
raw normalized power status, and the pre-run one-minute `/proc/loadavg` stamp.
Misses, stalls, invalid IDs and callback errors never set a gate verdict or
change the bench's gate result. Callback errors emit `status=ERROR`, retain the
partial sample count, and end the workload. Ordinary execution puts the new
workload last on a rig, so an error cannot poison subsequent old gate samples.
The independent G3z acceptance contract remains unchanged. No new TRACK row
prints a `pass` field or gate thresholds.

## Admission and commands

`DISPLAY=:99` (including screen suffixes/host prefixes) unconditionally emits
`status=SKIP reason=Xvfb_:99_has_no_real_vblank`, before any window opens in the
standalone mode. No environment override can turn this into measured data.
An absent display or unavailable native backend also prints an explicit SKIP.
The ordinary GL G3z path now skips on this known synthetic fixture as well.

The coordinator can run `build/bench/raster_bench --scroll-track --target A`
and `build/bench/gl_bench --scroll-track` on its separately announced real
display fixture. These run only the diagnostic workload, at both atlas sizes;
raster additionally supports the B-resolution X11 proxy. The existing default
bench matrices append this workload too. The worker ran solely with
`DISPLAY=:99 EDIT_DISPLAY=:99`, never enabled real-display access, and never
opened `:0`. A different display number alone cannot attest hardware vblank;
fixture selection belongs to the coordinator, not an automatic G3z claim.

No backend hook is needed for this TRACK row: `raster_last_present`,
`raster_frame_metrics`, `gl_displayed_msc`, and existing T5/T6 hooks suffice.
For future verified G3z acceptance, propose backend diagnostics for hardware
vblank source/rate and effective swap interval, plus GL matching completion
kind/mode/UST. This would distinguish skipped swaps and server presentation
intervals from dispatch stalls. No `src/raster`, `src/gl`, frozen header,
Makefile, or shared handoff/status files were edited. This explicit scope ban
also excludes their STATUS.md files; the bench status and verification live
here for the coordinator to carry into those files.

## Red / green and verification status

The tests were written first with unimplemented admission/accounting helpers.
Both release benches returned 1 for `--pace-self-check`:

```text
SELF-CHECK render_pace=Xvfb_SKIP FAIL
SELF-CHECK render_pace=MSC_and_stall FAIL
SELF-CHECK render_pace=600_p99 PASS
```

After implementation, GCC release and explicitly compiled Clang ASan/UBSan
benches both returned 0, with the following output for each backend:

```text
SELF-CHECK render_pace=Xvfb_SKIP PASS
SELF-CHECK render_pace=MSC_and_stall PASS
SELF-CHECK render_pace=600_p99 PASS
SELF-CHECK render_pace=TRACK_output PASS
SELF-CHECK render_pace=MSC_unavailable PASS
SELF-CHECK render_pace=600_scrolls_no_gate PASS
SELF-CHECK render_pace=error_is_TRACK PASS
```

These fixtures check skipped MSCs, duplicate/regressing IDs, an isolated long
stall, p99, unavailable MSC, machine-readable TRACK/SKIP stamps, exactly 600
scroll callbacks plus baseline, baseline exclusion, and partial error output.
The synthetic runner deliberately misses 1200 refreshes and still emits TRACK.
The original raster `--self-check` sections also all pass.

Full verification results:

```text
make all: exit 0 (GCC release, C11, all required warnings as errors)
check: 43 test binaries passed
test_replay_cli: all passed
make check: exit 0 (Clang ASan/UBSan)
fuzz: 22 fuzzers built
make fuzz: exit 0
Done 10287 runs in 31 second(s)
stat::number_of_executed_units: 10287
```

The existing raster fuzzer ran once with a 30-second requested budget,
`-max_len=4096 -rss_limit_mb=512 -print_final_stats=1`, completed in 31 seconds
without a finding, `(M)[AC] power_status="Not charging" load1=10.47` at start.
No corpus was regenerated; no new production parser or GL fuzzer was added.
Leak checking was disabled (`ASAN_OPTIONS=detect_leaks=0`); the coordinator reruns with leaks.
The sandbox cannot see the host's Xvfb socket; the required suite uses host
access to `:99` only. No replacement X server was started.

One standalone invocation of each new bench entry point produced six SKIP
rows, all `(M)[AC] power_status="Not charging" load1=11.29`. Representative
lines (all other size/target rows have the same reason/stamp):

```text
TRACK name=A_raster_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=11.29
TRACK name=A_egl_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=11.29
```

Real-display cadence, native stage percentiles and hardware stall numbers
remain unmeasured by this worker. These SKIPs are not a G3z verdict.

To verify the new contract without a real display:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/raster_bench --pace-self-check
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/gl_bench --pace-self-check
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/raster_bench --self-check
```

`make check` does not normally build sanitized bench binaries. This session
also compiled each bench explicitly with Clang, the required warning flags,
`-fsanitize=address,undefined -fno-omit-frame-pointer`, the display guard,
`build/san/libedit.a` and the same libraries as its release link, then ran
`--pace-self-check` with leak detection disabled. Both passed.

## Real-display run (2026-10-09 16:52–16:53, batch 2, Tobias's go 16:10)
`EDIT_ALLOW_REAL_DISPLAY=1 DISPLAY=:0`, power "Not charging" [AC], 1-min load 14.7–15.3 (about 10 workers running), panel refresh measured 16.67 ms (60 Hz). 600 refresh-paced frames each, TRACK:

| Backend / atlas | missed refreshes / 600 | longest stall | notes |
|---|---|---|---|
| EGL 15 px | 1 | 31.2 ms | submit p50/p99 1.24/1.58 ms; ingress→T5 4.94/18.9 ms |
| EGL 30 px | 3 | 37.2 ms | submit 0.47/0.64 ms |
| raster 15 px | 316 | 66.6 ms | strips p50 ≈ 5.5–6.2 ms each under load; ingress→T5 16.1/36.3 ms |
| raster 30 px | 75 | 50.1 ms | |

Reading (policy: notes, not beads): under load the 4-worker CPU raster cannot hold 60 Hz scrolling at 15 px; EGL nearly can (1–3 misses). This supports switching the editor's single backend call site to EGL (follow-up).
