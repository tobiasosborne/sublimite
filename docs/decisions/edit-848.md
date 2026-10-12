# edit-848: the GL backend also treats a stalled X server as slow, not lost

## Context

edit-cee (docs/decisions/edit-cee.md) found that the raster backend failed a
frame whose Present completion was missing for 2 s, so a paused X server made
the editor exit with `error -11` (RENDER_ERR_DEVICE). The GL backend was not
examined.

## Finding

The GL backend had the same policy. `gl_present` sets
`completion_deadline = swap time + GL_COMPLETION_TIMEOUT_NS` (3 s, src/gl/renderer.inc).
This is a (G) planning value. No derivation is recorded for it. It is unchanged here.
`gl_event` latched `gl_fail` once the deadline passed without the swap's PIXMAP
CompleteNotify. That holds whatever the reason, including an X server that is
stopped or stalled. A stall of 3 s or more during a frame therefore ended the
editor with -11 on GL as well. The GPU fence (`glClientWaitSync`) is local and is not
affected by a stopped server.

## Decision

When the window expires, `gl_completion_window` sends one round trip
(`GetInputFocus`) on `native_conn`. That is the connection Mesa uses for the swap's
PresentPixmap (EGL_PLATFORM_XCB_EXT on native_conn), so the answer is ordered
after that request.

- No answer yet, and the connection has no error: the server is stalled, not
  lost. The window re-arms, and no second probe is sent.
- An answer: the window restarts at the answer. If the PIXMAP is still missing a
  full window later, the frame fails as before.
- The connection has an error, an X error arrives, or there is no X connection
  (Wayland): the frame fails as before.

The probe is sent only after an expiry. Normal frames, including every typed
key, issue no extra request and do no extra work, so law 2 is untouched.
A probe still pending when the next swap is issued is discarded
(`xcb_discard_reply`), so libxcb never keeps an unread reply.

## Residual window (cee item 2)

On GL the probe goes out at expiry, which is after any stall that began during
the window. Its answer therefore comes after that stall has ended. A stall that
starts between the swap and the Present executing is covered. Failure now
needs a second stall of 3 s or more that starts within the vblank after the
probe is answered, before the already-queued Present executes.

For raster, this design (probe at expiry, re-arm until answered) would close
cee's window too. It was not done here because it is not a few lines.
Two code paths would change: the inline poll on UI and the worker fence job.
Both review fixtures in tests/raster_test.c (`P2-1-15`, `edit-cee`)
answer every reply sequence with a fence reply, so they would need a
sequence-aware mock. The fuzz transport (fuzz/raster_fuzz.c) would need a
`GetInputFocus` mock and a new oracle for its expiry op, and that fuzzer
already crashes on main (cee report). The window it would close covers about
3.3 % of stall start times (E, cee: 16.7 ms vblank / 500 ms blink period).
The live repro hit it in 0 of 10 runs (M, cee). It is left as the backlog
candidate it was.

## Not verified live

EGL cannot start on Xvfb on this box. A private Xvfb :147 with EDIT_BACKEND=gl
logs `EGL init failed: EGL/GL/Present unavailable (code=-10); using raster`.
gl_test on :99 also skips the native Present contracts (result=-10). The
SIGSTOP repro therefore cannot run on GL here. The fix is verified at unit
level only (gl_test `--review edit-848`), with a scripted connection.
Whether Mesa itself blocks the UI thread in a round trip during a stall
(eglMakeCurrent or drawable invalidation) is not known. That would be a pause,
not an exit.
