Rebase completed at `a9ac75e`, preserving both sides in `bench/editor_large_bench.c`.

- `make all`: exit 0, GCC 13 with `-Werror`.
- `make check`: exit 0, Clang 18 ASan/UBSan on `:99`; leaks were off.
- No interaction fixes needed.

[Report](docs/worker-reports/edit-2j9-s9-rebase.md) contains resolutions and log tails; it remains untracked. Shared display waits exceeded the 20-minute target.