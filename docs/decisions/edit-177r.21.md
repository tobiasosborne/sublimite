# edit-177r.21 — measurement boundary and proposed WSL policy

Phase 1 changes only the probe, its wrappers/build target, and documentation.
The measured capability matrix and the proposed degraded mode are in
[`wsl-share-capabilities-2026-11-01.md`](../reviews/wsl-share-capabilities-2026-11-01.md).
No production behavior or frozen `os.h` contract changes here.

The matrix's **safe as is / needs degraded mode / refuse** labels are proposed
product policy based on this host, not a claim of universal WSL behavior.
Successful mapping, flushing, stream-like filename creation, or closed-target
rename does not by itself authorize share saves. Preserving Linux mode and
ownership, source conflict checks and atomic replacement are separate obligations.
An ordinary production save currently fails without changing the original;
relaxing that failure requires a metadata-preserving transaction, not merely
a fallback rename. The coordinator must select that later write architecture.

Probe-policy estimates (E), with their derivations:

- Repetitions: an initial trial plus two repeats gives three trials. This detects
  disagreements; it does not estimate a tail probability.
- Observation window: two periods of the existing file-watch parent fallback,
  `2 × 1000 ms = 2000 ms`. This bounds an experiment, not acceptable latency.
- Helper bound: two observation windows plus a parent period,
  `2 × 2000 ms + 1000 ms = 5000 ms`.
- Child bound: three helper allowances for setup, mutation and teardown,
  `3 × 5000 ms = 15000 ms`. The supervisor terminates only its own timed-out child.
- Helper polling: five existing test-wait quanta (`5 × 1 ms = 5 ms`); notification
  timing explicitly includes helper scheduling. No cross-OS clock subtraction.
- Mapping extent: three native allocation-granularity units (E: a warm head,
  interior, and initially untouched tail). On this host that is measured as
  `3 × 65536 bytes = 196608 bytes` (M, native granularity and emitted length).
- Executable metadata fixture: octal `0751` (E), chosen to distinguish all three
  Linux permission classes and executable bits. The before/after values and
  owner IDs are actual Linux `lstat` results (M), not synthesized Windows modes.

Every destructive mapping experiment is a separate supervised child. The last
vectored handler logs otherwise unhandled faults; the normal production handler
gets its recovery opportunity first. Separate synthetic IN_PAGE_ERROR controls
verify this containment without claiming a real provider failure. The profile
lookup shim is limited to making the native Known Folder unavailable, so the
unchanged OS implementation uses its real environment fallback to a private
profile. It intercepts no filesystem capability being measured.

Stopping/restarting a distro, changing registry/profile configuration, installing
software, or using the live desktop is outside this experiment. Availability and
backing loss require a later Windows-driven disposable environment.
