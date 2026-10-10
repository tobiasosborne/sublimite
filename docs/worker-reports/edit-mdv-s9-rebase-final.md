Rebase completed at `ec10169`, preserving both sides.

- `make all`: exit 0, GCC 13 with `-Werror`.
- `make check`: exit 0, all 59 tests passed; ASan/UBSan used `:99`, leaks off.
- Renamed test parameter `notification` → `notified` to fix the `-Wshadow` interaction. Fix and report are staged.

[Rebase report](/home/tobias/Projects/sublimite/.wt/edit-mdv/docs/worker-reports/edit-mdv-s9-rebase.md). No unresolved conflicts.