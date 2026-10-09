Implemented long-line checkpoints and budgeted Unicode layout.

- RED → GREEN; **22 sanitizer test binaries passed**.
- **0 allocations** over 10,000 relayouts.
- Fuzz: **915 runs in 311 seconds**, no findings.
- Deep-column p99: **16.5/13.3 µs** at 10⁶/10⁸ columns (M)[AC, loaded]; checkpoint memory **4.19 MB**.
- Unicode/malformed p50 improved **2.69×/2.53×** (M)[bat, loaded]. Their 150 µs verdict still needs quiet AC verification; conditional gate proposals are documented.

[Full report, profiling, and pasted outputs](/home/tobias/Projects/editor/.wt/edit-zzj.9/docs/decisions/P3.1b.md) · [Status and next steps](/home/tobias/Projects/editor/.wt/edit-zzj.9/src/layout/STATUS.md)

No Makefile changes needed.