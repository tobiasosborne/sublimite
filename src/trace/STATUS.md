# edit-2ts — retained startup stages

Startup stages have independent first-write stamps, survive frame-ring wrap, and round-trip in the existing dump record format. tracedump prints a startup section with absolute monotonic timestamps and the elapsed-time derivation. Headless startup exercises actual file-prefix publication and both worker phases; missing native stages are explicitly reported as absent.

Evidence, exact validation outcomes and remaining acceptance: [worker report](../../docs/worker-reports/edit-2ts.md).
Design: [decision record](../../docs/decisions/edit-2ts.md).

