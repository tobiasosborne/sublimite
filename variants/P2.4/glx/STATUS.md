# GLX Session 5 handoff

Implemented: GL 3.3 core worker init/transfer, instanced dirty strips, retained
FBO, atlas cache, orphan and persistent VBO with extension fallback, fence T5,
verified GLX-to-Present serial mapping for T6, native WORK routing, src/trace
observer hooks. Submit is CPU-only; real release guard reports zero allocations
on first submit and 10,000 typing submissions in both VBO modes. Both modes pass
Xvfb llvmpipe pixel/lifecycle/trace diagnostics. No real display was used.

Read first: docs/decisions/P2.4-glx.md, src/gl/gl.h, tests/gl_test.c.

Verification: `xvfb-run -a -s '-screen 0 1280x1024x24 -noreset' make -C WT check`
passed all 23 binaries with ASan/UBSan and LeakSanitizer enabled. Plain Xvfb resets
hit the known x11_stall_test platform startup failure, outside this scope.
Release GL tests pass both modes and persistent fallback; all 12 fuzzers build.
Production benchmark was run both modes on [bat] Xvfb: displayed rows SKIP,
because OML sync control and swap control are absent. Diagnostic T6 is explicitly
synthetic; production never substitutes it. ASan diagnostic library modules stay
resident for LLVM/Mesa cache accounting; production cleanup is unchanged.

Platform integration finding: x11.c records raw-serial T6 before the renderer
callback, producing extra/misattributed records alongside original-frame hooks.
See decision report for the proposed plat_callbacks trace-ownership flag;
platform files were read-only and this remains for the coordinator/platform owner.

Next: coordinator quiet [AC] display-capable benchmark in both modes, including
10,000 displayed MSC-paced scroll frames and five 15-second idle samples per
atlas. Gate/Pareto acceptance remains pending those hardware measurements.
