# hl — edit-v0o.2 partial, blocked on upstream source acquisition

Implemented only the refresh-derived budget and highlight mailbox protocol in
`hl.h` / `hl.c`, with unit tests and a protocol fuzzer. The existing worker
transport supplies generation/lease filtering; readiness also checks the byte
range and viewport phase. These helpers allocate nothing and do not change
editor rendering. Plain text remains the only editor output.

The mailbox contract passed direct Clang ASan/UBSan and GCC release runs;
the release allocator guard is active. The protocol fuzzer passed its seed
corpus. These are helper checks, not parsing or editor latency acceptance.

Full GCC `make all` passed. Sanitizer suite results are in the worker report:
headless suites passed except the sandbox-denied IPC socket setup; the failing
file-kill sections also require X. Other display suites were blocked by the
shared lock or X connection denial. The coordinator must rerun unrestricted
with the assigned Xvfb display and leaks enabled.

The runtime is **not vendored**, no parsing worker exists, and this bead is
**not complete**. Shell networking is disabled; the available web transport
cannot deliver a complete, faithful upstream source tree. Details and intended
continuation are in `docs/decisions/edit-v0o.2.md` and the worker report.

Budget derivation (P): one frame times the supplied refresh interval equals
that interval in nanoseconds. An unknown interval is refused. No hardware rate
or arena capacity is invented.
