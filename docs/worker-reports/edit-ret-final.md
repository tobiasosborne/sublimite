Implemented silent reloads for clean buffers; dirty buffers retain the R/K prompt. View preservation/clamping, undo reset, and self-save suppression are covered.

`make all` and headless ASan/UBSan/LSan suites passed. Release typing guards reported zero allocations.

Delivered [worker report](docs/worker-reports/edit-ret.md) and [decision record](docs/decisions/edit-ret.md). Stopped; display verification remains with the coordinator.