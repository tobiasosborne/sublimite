Rebase completed as `45f8b41`, preserving both changes. Checkpoints now exclude retired Reload leases.

- `make all`: exit 0, GCC 13 with `-Werror`.
- `make check`: exit 0, Clang 18 ASan/UBSan; 63 test binaries passed. Leaks were off; display `:99` only.

[Rebase report](/home/tobias/Projects/sublimite/.wt/edit-9lf/docs/worker-reports/edit-9lf-s9-rebase.md) written and staged. HANDOFF.md and docs/worklog/ untouched.