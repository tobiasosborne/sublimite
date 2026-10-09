Continued and verified the inherited kernel. Retained cursor caching and compact pools; confirmed rejection of the gap prototype’s measured costs.

- `make check`: **15 binaries passed**, ASan/UBSan/leaks enabled under Xvfb.
- Fuzz: **180,960 runs in 121 seconds**, clean.
- TSan: **43 snapshots checked**, clean.
- Release typing test: **0 malloc calls**.
- Quick and full synthesis benches exited **0 [bat]**; all 36 gated cells printed PASS.

Indicative [bat] results: typing p99 **0.356 → 0.147 µs**; snapshot peak **8.08 → 6.08 MB**. Code-size and snapshot-latency costs are documented.

[Full report, red/green output, commands and matrix cells](/home/tobias/Projects/editor/.wt/edit-4w1.21/docs/decisions/P1.4-synthesis.md) · [STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.21/src/piece/STATUS.md)

No Makefile changes. The coordinator’s quiet [AC] gate verdict remains pending.