Continued the EGL backend. Submit now makes no native calls and asserts zero allocations; T5/T6 hooks emit through `src/trace`.

- `make check`: **23 tests passed**.
- Release VBO/fallback and Xvfb llvmpipe readback tests passed.
- `make fuzz`: **12 fuzzers built**.
- Both full `[bat]` benchmark matrices recorded as TRACK. Quiet `[AC]` verdicts remain pending; native Present conformance is explicitly SKIP under Xvfb.

[Full report: RED/GREEN, numbers, decisions, findings](/home/tobias/Projects/editor/.wt/edit-e6x.4-egl/docs/decisions/P2.4-egl.md).