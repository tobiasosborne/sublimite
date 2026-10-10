Rebase completed successfully as `9c4cfe1`; all conflicts resolved.

- GCC 13 `make all`: **0**
- Clang 18 ASan/UBSan `make check`: **0**, all 59 binaries and replay CLI checks passed
- Display: **`:99` only**; leak detection **off**
- Fuzzing: **0**, 42,151 runs

Preserved backing-fault checks in the new literal and publication paths. Fixed the fuzzer’s mailbox saturation setup and viewport-selection oracle, and moved fault injection to the first scan hook.

Follow-up fixes and the [rebase report](docs/worker-reports/edit-lez-s9-rebase.md) are staged. The report includes conflict resolutions, exit codes, run tails, and the bead’s remaining documented gaps.