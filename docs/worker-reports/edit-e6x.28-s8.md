Implemented and verified on `:99`. `make all`, `make check` with `detect_leaks=0`, and fuzz build passed. Three fuzzers ran clean for 61 seconds; release typing allocations were zero (M)[AC].

[Mandatory report](docs/worker-reports/edit-e6x.28-s8.md) includes red/green evidence and decisions.

Coordinator follow-ups remain: real-panel/leak validation, urgent close’s discarded queued commands, and the unrelated file/raster UAF observed during the first check. No git writes.