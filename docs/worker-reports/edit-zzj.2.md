Implemented P3.2. Full eight-part report, including RED/GREEN output and benchmark rows: [STATUS.md](/home/tobias/Projects/editor/.wt/edit-zzj.2/src/view/STATUS.md).

- ASan/UBSan: **23 test binaries passed** under isolated Xvfb.
- Fuzz: **126,590 runs in 311 seconds**, no findings.
- Allocation guard: **0 malloc calls over 10,000 keys** (M)[AC], meeting the zero gate (G).
- All corpus benches completed. Times remain TRACK and indicative; the reference kernel’s full-file line queries dominate large-file results.

[Decisions and integration contract](/home/tobias/Projects/editor/.wt/edit-zzj.2/docs/decisions/P3.2.md). No Makefile changes needed.