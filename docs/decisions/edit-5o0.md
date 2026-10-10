# edit-5o0 — raster/render retirement and admission decisions

Scope: docs/reviews/P2-1.md sections 7–12, 15–16, 34, 38. This worker does not revise binding gates or the frozen single-frame acceptance contract.

## Retirement

Authenticated strip publication is the last access to the snapshot; authenticated final fence publication follows QueryFence, matching PIXMAP completion, and matching IdleNotify, and is the last access to fence inputs/native state. A worker can remain physically live after publication. Submit retains unfinished handles in fixed storage for twenty leases, reaps physically finished handles, and refuses promptly with BUSY when quiescence or storage is unavailable. It never cancels or sleeps. Shutdown cancels and physically joins current and retired leases before releasing state/native storage. This adds no globals or typing-path allocation.

Raw DEVICE_DONE/PRESENT_COMPLETE events are rejected for raster by the common adapter. `render_backend_signal` is explicitly a trusted backend operation; native clients must use event routing. The original queued-fence lifetime regression now proves raw events cannot retire a server-owned frame and that shutdown still joins the queued job.

## Native margins

Initialize the complete admitted native pixmap to black, with opaque alpha when required by the visual. Clear it again on resize using one asynchronous native rectangle request; the next required full frame paints the grid. This covers stale and initially unused right/bottom margins without a UI pixel scan or resize allocation. The shared grow/shrink/fractional-margin native contract now runs against raster, normalizing visual alpha in RGB readback.

## UI admission

Raster admission bounds used descriptors to 4096 glyphs, 64 pages, and 65536 cells before descriptor/cell validation, including zero damage. Init capacity does not enlarge these frame budgets. Larger grids are refused; resumable preparation or a versioned retained-validation contract is not implemented. Back-to-back maximum-admitted-table TRACK measurements are in the worker report; they are not a display/G1 gate verdict.

## Deadlines

Inline frames store a two-second completion deadline when native presentation is issued. Poll first drains ready acknowledgements, then expires missing fence/PIXMAP/idle acknowledgement, latching device failure. Subsequent submit/present/resize requires recovery. `raster_completion_timeout` provides a rounded-up remaining timeout to clamp an existing damage-blocked wait. The production editor wait integration belongs to `src/editor/editor.c`, excluded from this worker's edit set, and remains required. Clean frames need no periodic poll timer.

## G11 evidence

Benchmark CPU attribution carries process CPU over later completion/timeout turns through observed retirement. A turn that retires an old blink and starts a new one is charged once to the old window; later CPU stays with the new window. This is conservative attribution across that boundary, not an exact per-frame CPU trace. The final pending frame is observed through the benchmark's existing quiet-observation turn and a nonblocking native drain; no extra production wake/timer is introduced. Its sample conservatively includes quiet-turn CPU. Process context-switch counters include all threads and timeout sleeps, but also preemption and are not authenticated wakeups. They are explicitly TRACK, and qualifying G11 runs return unmeasured until scheduling traces exist. Sample-count policy is unchanged for the parallel edit-9jo worker.

## Unresolved binding gates and source ownership

G10 remains failed. The default raster reserves two native surface payloads already beyond the 87 MB (G) cap; heap census adds another lower bound and excludes the editor's mmap arena, thread stacks, server overhead, and transient peak allocations. No capacity clamp silently changes default editor geometry. The required display/budget capacity policy lives in `src/editor/open.c`, outside the permitted editor files. A shared pixmap or worker-owned surface-growth/retirement design also requires a separate implementation decision. No gate exemption is granted.

G1 acceptance while T6 is pending remains failed under the frozen queue-depth-one contract. Safe superseding needs independently owned snapshots/pixmaps and changes to editor submission scheduling; neither the contract nor its gates are revised here. Xvfb cannot provide a real refresh-phase fixture.

Section 38 remains failed: GL accepts resize with a mapped lease and exports no cancellation API. The explicit diagnostic compiles the real GL lease/resize implementation with renamed exports, without native calls. Fixing `src/gl/renderer.inc` and `src/gl/gl.h` is outside this worker's permitted source edit set.

## Ownership fuzzing

Raster fuzzing includes the real private backend with a deterministic transport and an independent lease/native-ownership model stored per invocation. Every input exercises publication followed by delayed physical return and a new submit. Operations add atomic refusal, mailbox saturation, foreign/raw completion, partial native acknowledgement, cancellation, failed init, shutdown/replacement, resize, busy retry, and inline deadline expiry. Native upload/clear cannot occur while the model says the server owns the pixmap; native destruction cannot occur while a physical job lease remains; callbacks require a live owner. The same mandatory schedule fails against the pre-bead raster source. No real windows or global transport state are used.
