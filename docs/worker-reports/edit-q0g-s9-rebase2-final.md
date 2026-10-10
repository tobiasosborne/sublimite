Rebase completed at `5bbdb06`, preserving both sides.

- `make all`: **0**, GCC 13 with `-Werror`.
- `make check`: **2** on stale raster pixels; rerun **143**, stopped at the time cutoff after shared `:99` delays. Full acceptance remains unmet. Leaks were off.
- Staged fix: give the raster resize test a fresh window after the cancellation test. Focused sanitizer verification: **0**.

[Report with resolutions and log tails](/home/tobias/Projects/sublimite/.wt/edit-q0g/docs/worker-reports/edit-q0g-s9-rebase.md) is staged. Protected files were untouched.