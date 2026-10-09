# P2.4b (a): subdata

This candidate includes the shared opt-in implementation in `src/gl/gl.c`.
The default when compiled here is `subdata`; the production build remains unchanged
unless `EDIT_GL_UPLOAD` is set. No fork of `bench/gl_bench.c`.

From the repository root, run `sh variants/P2.4b/build.sh` once, then
`sh variants/P2.4b/a/test.sh` on Xvfb. The bench interface is
`sh variants/P2.4b/a/bench.sh [--quick]`; display environment is supplied
by the caller, with the repository display guard still in force.

Design, ownership limits and the coordinator-only display command:
`docs/decisions/P2.4b.md`.
