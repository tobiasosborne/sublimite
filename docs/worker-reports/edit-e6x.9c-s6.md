Port complete. Default `src/gl` behavior remains unchanged.

Applied cleanly: `src/gl/{gl.c,gl.h,STATUS.md}`, the patch’s tests, `variants/P2.4b/**`, decision doc, and worker report.

Hand-merged `bench/gl_bench.c`: upload rows and `--upload-only` coexist with main’s standalone `--scroll-track`/`--pace-self-check`. Persistent scrolling reacquires and preserves mapped leases before mutation, then revokes them on submit. Added a regression for the merged callback.

Fresh red runs, both exit 1:

```text
gl_test:162: FAIL strcmp(gl_buffer_mode(&b),upload)==0
gl_test:392: FAIL gl_readback_unit_test()==0

gl_test:193: FAIL g.cells==NULL && !b.active && !s.leased
gl_test:551: FAIL gl_pace_lease_test()==0
```

Green release and ASan/UBSan evidence:

```text
gl_test: paced persistent scroll PASS (merged callback, lease reacquire/revoke, retained rows, ring wrap, 0 allocations; scripted completion)
gl_test: coherent mapped cells readback PASS (direct writes, triple ring wrap, retained strip)
```

Subdata, orphan, and persistent candidates all built and passed llvmpipe readback. Legacy orphan/persistent paths, pacing self-checks, and CLI selection/rejection checks passed.

```text
make all exit=0
check: 45 test binaries passed
test_replay_cli: all passed
make check exit=0
fuzz: 23 fuzzers built
make fuzz exit=0
variant=a unit exit=0
variant=b unit exit=0
variant=c unit exit=0
```

`render_fuzz -runs=20000 -max_len=4096` completed without findings: 20,000 runs, 1 second reported, 1.09 seconds wall time **(M)[AC], load1=6.12**. Corpus unchanged.

Each upload bench was attempted once, back-to-back: **(M)[AC], Not charging, load1=6.04**, TRACK only. Shared GL `.text`: **14,507 bytes (M)[AC], load1=6.04**.

```text
BENCH name=egl_init_15px status=SKIP reason=EGL_or_matching_Present_unsupported result=-10 power=[AC]
upload=subdata bench exit=2 (expected SKIP=2)
upload=orphan bench exit=2 (expected SKIP=2)
upload=persistent bench exit=2 (expected SKIP=2)
```

Every mode’s `--scroll-track` exited 0 and preserved both paced SKIP rows. Persistent example:

```text
TRACK name=A_egl_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=5.95
TRACK name=A_egl_scroll_600_30px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=5.95
```

Open work: coordinator hardware timing/pick and leak-enabled verification; worker checks used `detect_leaks=0`. Persistent partial frames still copy retained cells. No unrelated failures found. Retain `variants/P2.4b/renderer.inc` with the source shim. No protected files changed, no `git`/`bd`, and no `:0` runs.

Updated [decision](/home/tobias/Projects/editor/.wt/edit-e6x.9c/docs/decisions/P2.4b.md) and [worker report](/home/tobias/Projects/editor/.wt/edit-e6x.9c/docs/worker-reports/edit-e6x.9-s6.md).