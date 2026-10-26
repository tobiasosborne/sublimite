Done. [Report](docs/worker-reports/edit-177r.3-s12-r2.md).

`make TARGET=win check`: exit 0 (M), [AC, desktop Spiral].

```text
rel os_test: 17 cases, 0 failed
san os_test: 17 cases, 0 failed
rel os_win_test: 18/19 passed, 1 unavailable
san os_win_test: 18/19 passed, 1 unavailable
```

Linux `make -j64 all`: exit 0. Release and sanitizer `os_test`: both `17 cases, 0 failed`. Frozen files unchanged.

Coordinator: decide where to run native file-symlink coverage (unavailable: error 1314) and real visible data-tag/provider fixtures.