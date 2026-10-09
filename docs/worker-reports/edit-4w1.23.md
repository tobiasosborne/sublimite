Completed P1.10a: frozen API, tests, fuzzer, benchmark, naive reference, and decision note.

The [full worker report](/home/tobias/Projects/editor/.wt/edit-4w1.23/docs/decisions/P1.10a.md) follows the requested eight-part format, including pasted RED/GREEN output, gate comparisons, decisions, and freeze hashes.

- `make check`: **15 test binaries passed**.
- Final fuzz: **481,850 runs in 126 seconds**, no mismatch or sanitizer finding.
- Full benchmark: all counts and offsets correct; expected **G6/G6v MISS**, exit **1**.
- Post-fix `--quick` benchmark: **PASS**, TRACK only.

Full timings are indicative under mixed power and concurrent load. P1.10 acceptance needs a quiet AC rerun. No Makefile changes were needed.