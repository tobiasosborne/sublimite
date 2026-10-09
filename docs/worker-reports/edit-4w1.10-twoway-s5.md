Completed the Two-Way baseline and removed `scan_pair`.

- `make check`: **23 binaries passed**, ASan/UBSan/LeakSanitizer enabled.
- Frozen fuzz: **361,708 runs in 301 seconds**, clean.
- Full benchmark: correctness PASS throughout; G6/G6v battery misses are indicative.
- `.text`: **14,426 bytes**.
- Raw `scan_count`: **5.322 GB/s [bat]** before, **10.343 GB/s [AC]** afterward.

[Full report with RED/GREEN output, benchmarks, decisions, and the Xvfb flake](/home/tobias/Projects/editor/.wt/edit-4w1.10-twoway-only/docs/decisions/P1.10-twoway-only.md). Frozen files unchanged; no Makefile changes.