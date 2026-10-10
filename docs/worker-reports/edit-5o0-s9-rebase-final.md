Rebase completed as `3c0122b`. Resolved `bench/editor_bench.c`, preserving main’s 10,000-sample rows and the bead’s delayed-retirement CPU accounting.

- `make all`: exit 0, GCC 13, `-Werror`.
- `make check`: exit 0 on retry; all 60 binaries and replay CLI tests passed. Leaks were off; display was `:99`.
- First run failed an unchanged clipboard assertion; no interaction fix was needed.

[Rebase report](docs/worker-reports/edit-5o0-s9-rebase.md) is written and staged.