Implemented the requested chords. `make all` and headless ASan/UBSan tests pass with leak detection disabled. Full `make check` remains blocked by sandbox restrictions and out-of-scope failures.

Save As’s pre-write journal retention remains incomplete. The [report](docs/worker-reports/edit-o20-s6.md) includes that crash limitation, files touched, red/green evidence, and display checks requiring Xvfb `:99`.

Stopped after edit-o20.