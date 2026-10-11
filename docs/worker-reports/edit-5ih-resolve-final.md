Resolved all conflicts into the shared DPI/zoom/font-selection lifecycle, preserving async Unicode and startup stages. No `git` or `bd` ran.

`make all` passes. ASan font/zoom/Unicode and requested editor probes pass; active release guards report zero allocations. Full editor and native font-size tests retain launcher/display failures.

[Resolver report](docs/worker-reports/edit-5ih-resolve.md)