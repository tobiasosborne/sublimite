# edit-40t — GL review ownership decisions

## Worker continuation: benchmark fault and skip evidence

Benchmark fault injection now identifies the stage actually reached. An
injected acquisition fault enters the owner cleanup directly, without printing
an assertion failure. Tests require the stage marker; a generic failure return
cannot prove that the negative control ran. The arena fault has no native
prerequisite and cannot be skipped. Native faults explicitly distinguish an
unreached injection from exercised cleanup, including thread checks on skips.

Only `RENDER_ERR_UNSUPPORTED` is an infrastructure skip in initial benchmark
driver preparation/initialization. Resource/init errors and observation timeouts
are benchmark failures. A headless test scripts native admission while keeping
the real worker pool and benchmark owner cleanup, so this classification and
prepared-worker cleanup remain testable with the X socket denied.

This changes cold benchmark/test diagnostics and error handling only. The
renderer, editor factory selection, and typing path are unchanged. The existing
startup/blink/paired-tool scope boundaries below still apply.

GL diagnostics authenticate the implementation by its submit operation and require initialized, non-null opaque state before casting. Diagnostics on other backends return neutral values/errors.

Each GL backend opens its own native XCB connection using the platform’s default DISPLAY, then owns the resulting EGLDisplay. The platform window remains borrowed and must outlive the backend. DISPLAY must stay the same between platform creation and backend initialization. This avoids a process-global display registry and lets every successful/failed backend terminate its initialized display before disconnecting its native identity. Backends using windows from a shared platform connection therefore have distinct EGLDisplays; terminating one cannot invalidate another.

Driver initialization retains its work handle. Quiescent cleanup cancels and physically joins that handle before inspecting backend state, shutting it down, or releasing its arena. The test wait helper varies the observation deadline only; it does not pretend to preempt EGL initialization.

Completion work uses a dedicated foreground lane when the pool has one, otherwise a raster lane. Bulk-only pools are refused. The test/bench driver provisions a raster lane. No additional pool is created for benchmark contention: jobs borrow the renderer pool, and their owners cancel/join individual handles before freeing arguments without shutting down the renderer’s pool.

Contention keeps explicitly named surrogate rows and adds actual index/find/durable-save rows. Actual searches and saves borrow an immutable snapshot; the UI owns lineidx and adopts its mailbox results. Index recreation occurs before the measured frame and allocator guard. The queued-three module scenario settles with find active and index/save queued, while standalone cases exercise active index and save. Progress/correctness checks support overlap claims; real GL latency acceptance remains unmeasured on the worker’s Xvfb fixture.

Startup raster bootstrap and EGL handoff require editor lifecycle changes outside the permitted editor file. Blink native-readiness conversion must land with caret-aware editor polling changes. Paired EGL identity requires an explicit association in the injector/joiner path. These three findings remain deferred and have optional failing review lanes; no factory-only workaround, constant serial offset or weakened default test assertion was used to claim them fixed.
