Continued the GLX WIP and retained its design.

- Both VBO modes pass pixel/conformance diagnostics.
- Zero allocations across 10,000 input-to-submit cycles.
- ASan/UBSan/LSan: **23 test binaries passed** under Xvfb `-noreset`.
- **12 fuzzers built.**
- Both `[bat]` benches ran; displayed-frame rows report **SKIP** because Xvfb lacks required GLX presentation support.

Quiet-AC gates and the platform’s extra raw-serial T6 trace records remain for the coordinator.

[Full report, red/green output, benches, and proposed platform change](/home/tobias/Projects/editor/.wt/edit-e6x.4-glx/docs/decisions/P2.4-glx.md)