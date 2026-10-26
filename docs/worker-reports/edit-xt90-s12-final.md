Done. [Report](docs/worker-reports/edit-xt90-s12.md).

Linux `make check`: **exit 0**, 106 binaries passed, LeakSanitizer enabled.

```text
ASan fired: ok
Windows check: 6 release + 6 sanitizer test binaries passed; ASan negative control passed
```

No blocking coordinator decision. Merge Windows build lists with W0.5; the editor lease handoff remains for its migration worker.