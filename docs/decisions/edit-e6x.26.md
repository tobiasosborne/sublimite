# edit-e6x.26 — P2.4c GL review fixes

## Ownership and binding

Production now includes `src/gl/renderer.inc`; the experimental candidate copies
remain untouched. All existing upload selections remain supported. Rebind the
owned EGL context at every native operation boundary, including readback,
completion polling and teardown. There is no process/thread binding cache and
no new production global. Contexts sharing an EGLDisplay must survive another
backend's shutdown: destroy only owned resources, surface and context. Keep the
vendor loader/display cache rooted with RTLD_NODELETE rather than terminate a
shared display. The coordinator must validate vendor lifetime behavior with
LeakSanitizer enabled.

Native connection, window and visual are immutable lifetime handles. Startup
queries X geometry into backend storage; its draw/probe never reads mutable UI
height. Present refreshes the height only on UI after the initialization mailbox
handoff. Concurrent startup draw/ConfigureNotify is covered under TSan.

## Presentation transaction and completion

Dirty atlas rows are committed only after successful upload. Upload/draw errors
and fence creation are checked before native swap. Reserve a continuation before
swap; an accepted swap commits its frame ID/serial and has no fallible trailing
step that could authorize a second swap. Bind failure, failed swap, context loss,
Skip, invalid completion, reset, failed fence and missing completion latch device
failure until shutdown. A failed persistent backend also refuses writable cell
leases, even while the adapter still owns the failed frame.

Only matching private typed PIXMAP completions acknowledge display. Copy, Flip
and SuboptimalCopy are accepted; Skip fails startup admission or the active
frame. Untyped platform notices only request a poll. Preserve authoritative MSC
and observation time after verification; native serials never become renderer
trace IDs. T6 is emitted through the common adapter under the original frame ID,
with the existing device-completion ordering rule.

Each pending frame arms a cancellable, argument-free worker wake. Publication
routes through an identity/generation-bound mailbox receiver on UI. A poll drains
at most eight native events (G); a still-pending frame rearms automatically.
A missing completion expires after three seconds (G), checked on completion
continuations. Workers do no GL or backend-memory access. Teardown unbinds the
receiver, cancels and waits for its last worker lease before freeing state.
This is cooperative scheduling, not a hard real-time guarantee under worker/OS
starvation. Pools must remain alive through backend shutdown, and the UI must
service their eventfd/mailbox and check `gl_completion_status`.

## Admission and native conformance

Bound accepted initialization capacity before the common adapter can scan
caller descriptors: 4096 glyphs, 64 pages and 16 MiB atlas storage (G). Bound unique
referenced glyph-copy work to 262144 pixels per submit (G); overlapping rectangles
still count separately. Rejection preserves snapshot/lease ownership. These are
work limits, not measured latency gate claims. Larger runtime font tables need a
separate preparation/contract decision rather than unbounded typing work.

Native diagnostics read actual X window pixels after EGL swaps. They cover
changed grid/cell sizes, grow/shrink, fractional margins, reused swap buffers,
close/destroy pending and fresh initialization. Clearing the default framebuffer
before blit prevents old cells surviving in margins. Diagnostics do not fabricate
Present acknowledgements. An unavailable Xvfb/EGL context skips native diagnostics
explicitly while pure completion/trace tests still run. A context with no matching
PIXMAP/MSC clock still fails production admission and skips the timing lane.

## Evidence

Session-8 red/green outputs, compiler checks, sanitizer results, TSan, counting
allocator and fuzz evidence are in `docs/worker-reports/edit-e6x.26-s8.md`.
No timing variant was selected in this session. Benchmark honesty sections are
owned by edit-e6x.27; `bench/gl_bench.c` was not edited.
