Done. [Report](docs/worker-reports/edit-pf3l-s12.md)

- `nm`: no `file_bus`; exactly one `os_fault_service` and `fault_install` per file-linked binary.
- Linux `make check`: exit **0**, 105 binaries passed; LeakSanitizer enabled.
- Windows summaries:

```text
file_test: Windows ok
check: 4 Windows release and 4 sanitizer test binaries passed
ASan fired: ok
```

Coordinator: retain W0.5’s broader Windows build lists when merging. Metadata coverage limits are documented; no blocking design decision remains.