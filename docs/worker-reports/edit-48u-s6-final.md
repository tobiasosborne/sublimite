Implemented edit-48u and stopped.

`make all`, focused regressions, and the headless editor suite pass. Full `make check` remains blocked by sandbox display/IPC access and an unrelated journal replay failure. Sanitizer runs required disabling LeakSanitizer.

[Worker report with red/green runs](docs/worker-reports/edit-48u-s6.md) · [Design decisions](docs/decisions/edit-48u.md)