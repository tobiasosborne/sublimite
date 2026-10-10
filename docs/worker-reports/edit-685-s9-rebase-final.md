Rebase completed as `f8c4a6d`, preserving both sides.

- `make all`: exit 0.
- All 61 sanitizer binaries and replay CLI passed, with leaks off.
- Full `make check`: exit 2 after stopping the final contract’s `:99` lock wait at the time budget. Exit-zero verification remains incomplete.

No additional source fixes were needed. Details: [rebase report](/home/tobias/Projects/sublimite/.wt/edit-685/docs/worker-reports/edit-685-s9-rebase.md).