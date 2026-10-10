# edit-5cv — shared Xvfb ownership

The sanitizer check driver serializes the display-dependent test families,
while other module tests remain outside the lane. The Makefile matches CLI, editor,
X11, reference-tool, GL and raster tests. New editor/X11 test names inherit
the lane automatically. The prewake display/idle scripts, optional native GL
script and zygote launcher use the same wrapper.

`tools/with_display_lock.sh` uses Python's `fcntl.flock` on a persistent
`edit-xvfb-<display-number>.lock` file in the system temporary directory.
Display host/screen spellings share the display-number lane. The wrapper
executes the command with the lock descriptor inherited; nested callers
validate its inode and reuse its open-file description. The lock file is never
unlinked, avoiding an inode replacement that would split the lane.

Using Python here preserves the acquiring process PID across exec. Wait and
timeout diagnostics read the actual flock owner from the kernel lock table,
without a stale PID sidecar. When that table is unavailable, the diagnostic
explicitly says `held by unknown`. Python is already required by the reference
pair test. `EDIT_DISPLAY_LOCK_TIMEOUT` sets a finite, nonnegative timeout; the
default operational limit is 600 seconds (G), configurable for long queues.

Direct `refwin_test` invocations enter this wrapper unless they already have a
validated inherited lock descriptor. Its held-key fixture, strict preflight,
CSV assertions, frame checks and measured endpoints are unchanged. There are
no new C globals or changes to editor allocation/typing paths.

The shell regression first ran the original sanitizer binary concurrently on
the shared display and reproduced held-key failures. It subsequently checks
paired runs, holder PID/timeout messages, display aliases, nested descriptor
reuse, command exit propagation and Makefile/script participation. The quick
lock contract is included in `make check`; the five-pair concurrency exercise
remains an explicit test-driver mode. Evidence and remaining validation limits
are in `docs/worker-reports/edit-5cv-s9.md`.
