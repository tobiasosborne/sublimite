# GL — P2.4b cell upload experiment (edit-e6x.9)

The EGL backend selected in P2.4 remains the default. Unset EDIT_GL_UPLOAD keeps
its expanded-instance renderer and EDIT_GL_VBO behavior unchanged. Opt-in
EDIT_GL_UPLOAD=subdata|orphan|persistent selects the raw-cell experiments.
See docs/decisions/P2.4b.md for design, ownership, evidence and pick commands.

Done:

- src/gl/gl.c is a selection shim; the shared implementation lives in
  variants/P2.4b/renderer.inc until the coordinator picks/promotes a candidate.
- Controlled cell-array/subdata and cell-array/orphan candidates, plus direct
  coherent read/write mapped-cell storage with three fenced regions.
- Shared raw-cell shader and glyph metadata texture buffer; no cell-to-instance
  expansion in opt-in modes. All GL calls remain after submit or in init.
- GL-specific acquire/submit lease; successful submit revokes grid.cells.
  Ordinary caller-owned CPU grids retain the frozen copy-on-submit guarantee.
- Cached, nonblocking region-fence checks; write-after-read exclusion; retry,
  validation failure and shutdown handling. Frozen adapter queue depth remains one.
- One bench/gl_bench.c, runtime selection, allocation checks, synthetic layout
  timing, G3 to T5 and G1 strip to T4; experimental rows always TRACK.
- Ported onto pacing-aware main: standalone --scroll-track/--pace-self-check
  and both paced atlas sizes remain; --upload-only can combine with --quick.
  Explicit scroll selection works in every upload mode. Persistent pacing
  reacquires/preserves a lease before shifting cells, then revokes it at submit.
- The merged pacing callback is tested with scripted completions and real
  cell-ring/adapter submission: retained rows, ring wrap, lease ownership and
  zero counted input-to-submit allocations. Native timing is not simulated.
- Candidate directories a/b/c include shared source with their own init default;
  common build.sh and identical bench.sh/test.sh interfaces, no benchmark fork.
- All candidates pass exact llvmpipe pixel readback on :99; coherent mapped
  direct writes/retained strips cross multiple ring wraps. Scripted unit tests
  cover timeout, failed fence, slot reuse, lease revocation, retry, fallback copy,
  and zero allocations. Legacy paths and frozen grid/adapter/trace tests remain.

Verification from the worktree root:

    env DISPLAY=:99 EDIT_DISPLAY=:99 make all
    env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
    env DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
    sh variants/P2.4b/build.sh
    for variant in a b c; do sh variants/P2.4b/$variant/test.sh; done
    env DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/gl_bench --pace-self-check
    env DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_GL_UPLOAD=persistent build/bench/gl_bench --scroll-track

The port's GCC make all and candidate builds/tests pass. Runtime readback
covers all upload modes plus legacy orphan/persistent on llvmpipe; pacing
self-check and CLI selection/rejection checks pass. Full clang ASan/UBSan
make check passes: 45 test binaries and the replay CLI contract. make fuzz
builds 23 fuzzers.
The existing render fuzzer completed 20,000 runs without findings: wall time
1.09 s (M)[AC], load1=6.12. Worker leak checking is disabled; coordinator must
re-run with LeakSanitizer enabled. Full-project sanitizer result is recorded
in docs/decisions/P2.4b.md and the port worker report.

Missing/coordinator work:

- :99 does not supply matching EGL PIXMAP Present: native lifecycle/latency
  benches explicitly SKIP. Each runtime upload bench was attempted once; the
  shared GL .text is 14,507 bytes (M)[AC], load1=6.04, TRACK. Latency rows remain
  empty. Both standalone paced sizes SKIP in every mode. No worker opened :0 or opted out of the
  display guard. Announced coordinator-only real-display command is in P2.4b.md.
- No variant is picked or promoted. variants/ is ignored: force-add the candidate
  source, shared renderer.inc, build/test/bench wrappers and READMEs when collecting the experiment.
- Partial mapped layout currently copies retained cells between regions. A
  backend-neutral lease/cancel API and retained-row validation/storage proposal
  are documented for a future bead; render.h remains frozen and untouched.
- The synthetic layout bench excludes document mutation/input ingress/minimap;
  it cannot establish the editor's end-to-end G1 verdict.
