Fixed recovery resizing an uninitialized backend.

`make all`, ASan/UBSan hot-exit, journal, and headless editor tests passed. The release typing guard confirmed zero allocations.

[Report with red/green evidence](docs/worker-reports/edit-457.8-fix.md). The unrelated display-lock wait and startup pixel-rounding finding are documented. Stopped as requested.