Implemented save recovery and retry for edit-1vo.

`make all`, release regressions, and targeted ASan/UBSan checks passed. Full verification still has display restrictions and unrelated `journal_kill_test`/`ipc_test` failures.

[Worker report](docs/worker-reports/edit-1vo-s6.md) includes red/green output, decisions, and remaining checks. Stopping after this bead.