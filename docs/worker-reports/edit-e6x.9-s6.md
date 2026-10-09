Port complete; upload experiments and main's pacing features coexist. The src/gl default remains unchanged.

Applied cleanly: src/gl/{gl.c,gl.h,STATUS.md}, tests/gl_test.c, variants/P2.4b/**, docs/decisions/P2.4b.md, and the worker report. Dry-run identified conflicts only in bench/gl_bench.c; its clean hunks were applied before the hand merge.

Hand-merged bench/gl_bench.c: upload rows/--upload-only/EDIT_GL_UPLOAD plus render_pace.h, gl_pace_rig, standalone --scroll-track/--pace-self-check, and both paced sizes. --quick can pair with --upload-only; duplicates and mixed pacing options remain usage errors. Explicit scrolling overrides the upload-only default. Persistent pacing reacquires/preserves cell leases before mutation and revokes them on submit. Added a merged-callback regression to tests/gl_test.c; refreshed STATUS, decision evidence, and this report.

Fresh upload red on :99, exit 1:
```text
gl_test:162: FAIL strcmp(gl_buffer_mode(&b),upload)==0
gl_test:392: FAIL gl_readback_unit_test()==0
```

Fresh pacing red against main's original callback, exit 1:
```text
gl_test:193: FAIL g.cells==NULL && !b.active && !s.leased
gl_test:551: FAIL gl_pace_lease_test()==0
```

Green release and ASan/UBSan evidence:
```text
gl_test: paced persistent scroll PASS (merged callback, lease reacquire/revoke, retained rows, ring wrap, 0 allocations; scripted completion)
gl_test: coherent mapped cells readback PASS (direct writes, triple ring wrap, retained strip)
gl_test: readback unit PASS (4x3, all 256 alpha values, inverse, underline, wide, snapshot), VBO=subdata renderer=llvmpipe (LLVM 20.1.2, 256 bits)
gl_test: readback unit PASS (4x3, all 256 alpha values, inverse, underline, wide, snapshot), VBO=orphan renderer=llvmpipe (LLVM 20.1.2, 256 bits)
gl_test: readback unit PASS (4x3, all 256 alpha values, inverse, underline, wide, snapshot), VBO=persistent renderer=llvmpipe (LLVM 20.1.2, 256 bits)
```

Required runs:
```text
make all exit=0
check: 45 test binaries passed
test_replay_cli: all passed
make check exit=0
fuzz: 23 fuzzers built
make fuzz exit=0
P2.4b: built a
P2.4b: built b
P2.4b: built c
variant=a unit exit=0
variant=b unit exit=0
variant=c unit exit=0
```

Legacy default and EDIT_GL_VBO=persistent readbacks also pass. CLI selection/rejection checks and all pacing self-check cases pass. All display execution used DISPLAY=:99 EDIT_DISPLAY=:99; sandbox socket isolation required approved execution outside the sandbox. ASAN_OPTIONS=detect_leaks=0 was used for sanitizer runs.

Fuzz: render_fuzz -runs=20000 -max_len=4096, exit 0, no findings. Actual completion:
```text
Done 20000 runs in 1 second(s)
render_fuzz wall_s=1.09 (M)
```
Stamp: (M)[AC], Not charging, load1=6.12. No corpus regenerated or modified; native GL is not fuzzed.

Each runtime upload bench was attempted once, back-to-back. Power/load were read before every attempt: all upload attempts (M)[AC], Not charging, load1=6.04, TRACK. All modes measured the same shared GL object: .text=14,507 bytes (M)[AC], load1=6.04. No latency samples or gate verdicts.
```text
BENCH name=egl_init_15px status=SKIP reason=EGL_or_matching_Present_unsupported result=-10 power=[AC]
upload=subdata bench exit=2 (expected SKIP=2)
upload=orphan bench exit=2 (expected SKIP=2)
upload=persistent bench exit=2 (expected SKIP=2)
```

Every mode's --scroll-track exits 0 and preserves both SKIP rows. Persistent example; subdata/orphan use load1=6.04:
```text
TRACK name=A_egl_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=5.95
TRACK name=A_egl_scroll_600_30px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=5.95
```

Open work: coordinator hardware timing/pick and leak-enabled verification. Persistent partial frames still copy retained cells between mapped regions. No unrelated failures found. Retain/force-add variants/P2.4b/renderer.inc with the src/gl shim. No frozen headers, Makefile, CLAUDE.md, HANDOFF.md, or PLAN.md changed; no git/bd commands or :0 runs.
