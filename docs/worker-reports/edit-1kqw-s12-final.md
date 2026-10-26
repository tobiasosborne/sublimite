Done, with documented Windows dependency SKIPs.

[Report](docs/worker-reports/edit-1kqw-s12.md)

- Linux `make check`: exit **0**, LeakSanitizer enabled.
- Windows release: **33/33 passed**.
- Windows sanitizer: **33/33 passed**.
- `ASan fired: ok`

Coordinator must decide whether to accept file/journal SKIPs until W2.1/W2.2 merge, then align the shared file identity conversion and verify production integration.