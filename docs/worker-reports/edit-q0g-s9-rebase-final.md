Rebase completed as `8613fd1`; both conflicts resolved, preserving both sides.

Interaction fix: changed the alignment test’s `pool_ready(&c)` call to `pool_init(&c)` to match main’s eager initialization.

- `make all`: **0**, GCC 13 with `-Werror`.
- `make check`: **2**, timed out acquiring shared `:99` at `refproto_test`. Full acceptance remains unmet. Leaks were off.
- Pure sanitizer alignment, XI2, and display-policy tests: **0**.

[Report with resolutions and log tails](/home/tobias/Projects/sublimite/.wt/edit-q0g/docs/worker-reports/edit-q0g-s9-rebase.md) is written and uncommitted.