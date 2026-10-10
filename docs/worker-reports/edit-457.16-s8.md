The named regression was already fixed in the WIP; controlled removal reproduced the red, and restoration passed.

`make all`, `make check`, and editor/tabs/ipc fuzz runs passed. Typing counted **0 allocations (M)[AC]**. LeakSanitizer was disabled; the coordinator must rerun with leaks on.

Timing acceptance remains uncertified on the loaded box. Contract gaps are documented without widening scope.

[Mandatory report](/home/tobias/Projects/sublimite/.wt/edit-457.16/docs/worker-reports/edit-457.16-s8.md). No Git writes.