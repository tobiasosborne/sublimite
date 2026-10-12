Fixed edit-37a. Regression confirmed red → green; `make all` and targeted socket-free ASan/UBSan checks passed.

Full verification still needs coordinator reruns for display/IPC restrictions and the unrelated `journal_kill_test` failure.

[Worker report](docs/worker-reports/edit-37a-s6.md) includes runs, limitations, and remaining work. Stopping after this bead.