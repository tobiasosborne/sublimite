Implemented 600-frame refresh-paced scrolling TRACK rows with MSC miss counts, longest stall, stage p50/p99, and power/load stamps. G3z remains the gate.

Changed: `bench/raster_bench.c`, `bench/gl_bench.c`, new `bench/render_pace.h`, and [docs/decisions/e6x.16.md](/home/tobias/Projects/editor/.wt/edit-e6x.16/docs/decisions/e6x.16.md). No backend source edits.

Red run, both benches’ `--pace-self-check`, exit 1:

```text
SELF-CHECK render_pace=Xvfb_SKIP FAIL
SELF-CHECK render_pace=MSC_and_stall FAIL
SELF-CHECK render_pace=600_p99 PASS
```

Green run, both benches under GCC and Clang ASan/UBSan, exit 0:

```text
SELF-CHECK render_pace=Xvfb_SKIP PASS
SELF-CHECK render_pace=MSC_and_stall PASS
SELF-CHECK render_pace=600_p99 PASS
SELF-CHECK render_pace=TRACK_output PASS
SELF-CHECK render_pace=MSC_unavailable PASS
SELF-CHECK render_pace=600_scrolls_no_gate PASS
SELF-CHECK render_pace=error_is_TRACK PASS
```

Required verification:

```text
make all: exit 0
check: 43 test binaries passed
test_replay_cli: all passed
make check: exit 0 — Clang ASan/UBSan, detect_leaks=0
fuzz: 22 fuzzers built
make fuzz: exit 0
Done 10287 runs in 31 second(s)
```

Raster fuzz: no findings; requested 30-second budget, completed 31 seconds `(M)[AC] power_status="Not charging" load1=10.47`.

Standalone bench rows, both commands exit 0:

```text
TRACK name=A_raster_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=11.29
TRACK name=A_raster_scroll_600_30px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=11.29
TRACK name=B_raster_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=11.29
TRACK name=B_raster_scroll_600_30px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=11.29
TRACK name=A_egl_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=11.29
TRACK name=A_egl_scroll_600_30px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=11.29
```

Open: coordinator’s real-display cadence/stage measurements and leak-enabled verification remain. Future diagnostic hooks are proposed in the decision doc. Module STATUS files stayed untouched under the explicit `src/raster`/`src/gl` edit ban. No `:0` run, git, bd, or corpus regeneration.