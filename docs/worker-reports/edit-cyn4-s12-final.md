Partial. [Worker report](docs/worker-reports/edit-cyn4-s12.md).

```text
Windows release: 6 test binaries passed
Windows sanitizer: 6 test binaries passed
ASan fired: ok
```

Linux `make check`: **exit 0**, all 104 binaries passed with LeakSanitizer enabled.

Coordinator: decide whether to defer editor-dependent Windows font suites and retain the shell helper’s existing LeakSanitizer override.