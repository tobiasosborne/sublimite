Completed scoped implementation and [worker report](docs/worker-reports/edit-e6x.26-s8.md).

`make all` and `make check` pass. TSan passes; both fuzzers ran 61 seconds clean (M) [AC]. Typing allocations remain zero (M) [AC].

Xvfb’s Present/MSC lane skips; LeakSanitizer was disabled as requested. The report distinguishes reconstructed baseline reds from this session’s red-green fixes. No git writes or benchmark edits.