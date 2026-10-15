Implemented the fif engine. Release build, targeted sanitizer checks, allocation guard, and fuzz smoke pass.

[Worker report](docs/worker-reports/edit-34l-s7.md) includes red/green evidence, benchmark misses, sandbox display/IPC blockers, and the libc `opendir` allocation caveat. Stopping at edit-34l.