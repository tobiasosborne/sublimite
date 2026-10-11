Raster now paints while EGL initializes, with startup stages in `tracedump`. Headless startup tests and `make all` pass.

`make check` ran; socket restrictions and an unrelated reload/wait failure prevent full acceptance. Native verification remains with the coordinator.

[Worker report](docs/worker-reports/edit-2ts.md) includes red/green runs, suite results, and remaining work.