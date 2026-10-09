# edit-e6x.4 / EGL — Session 5 handoff

Read docs/decisions/P2.4-egl.md first for RED/GREEN evidence, exact benchmark
outputs, law 2 scope, and continuation decisions. Then gl.c, gl_test.c and
bench/gl_bench.c. Existing renderer/driver/headers/link flags were kept.

Done: EGL/XCB dynamically loaded on the init worker; GL 3.3 core; both runtime
VBO modes and explicit storage fallback; cached atlas; exact integer glyph/bg
blend; retained framebuffer and instanced scissored strips; queue one; post-swap
fence with a bounded zero-timeout poll; matching PIXMAP Present completion through
plat callback and private verification; original-frame T5/T6 src/trace hooks.

Session 5 resolves law 2: submit now performs only CPU copies/comparisons into
init-reserved storage and coherent mapped memory. Atlas uploads, context binding
and orphan VBO native calls are deferred to present. Submit makes no GL/X calls,
therefore no blocking X round-trip. Release regression counts 0 allocations in
10,000 changing-atlas frames for each mode; real-window benchmarks also assert
0 allocations from frame begin/damage through submit on every frame. Production
allocator hook and frozen files are unchanged; native present/event allocation
is outside the settled input->submit scope.

Verification: release orphan/persistent/forced-fallback tests pass on Xvfb.
ASan/UBSan both modes pass with leak checking enabled. Actual llvmpipe 4x3 pixel
readback covers all 256 alpha values, inverse/underline/wide glyphs, copied
metadata, retained strip damage and no damage. Frozen grid/adapter/trace checks
are reused. Native DRI3 Present lifecycle is explicitly SKIP on Xvfb: init
returns UNSUPPORTED and caller resources are cleaned. No synthetic T6.
Final full project check after all test additions: 23 binaries passed.
make fuzz: 12 fuzzers built; no GL parser/fuzzer needed.

Both final full benchmark matrices completed on Intel Iris Xe at 2880x1800,
(M)[bat], TRACK/indicative with concurrent load. Each mode: 200 warm-up + 2,000
full-frame samples per atlas size, 2,000 typing samples, 10,000 scroll frames,
five after-idle samples at 15 s, worker startup and module .text (11,118 bytes).
All 16,407 benchmark submits per mode passed the release allocation assertion.
Quiet [AC] G3/G3z verdict and competition selection remain coordinator work.
Minimap absent; 0.18 ms (E) reserved, not measured.

Next: coordinator quiet [AC] remeasurement of both VBO modes. For the full
native frozen lifecycle suite use a disposable real DRI3 test display; all
worker X11 tests stay on Xvfb, never the user's display. Separate platform
finding: x11.c emits additional raw-serial T6 records that can collide with
render IDs, and its callback lacks kind/mode. Frozen platform files untouched.
No Makefile changes, no unresolved implementation item, no git/bd commands.
