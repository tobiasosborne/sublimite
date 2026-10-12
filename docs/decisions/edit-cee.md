# edit-cee: a stalled X server is slow, not a lost completion

## Context

Astra drive 3 (docs/reviews/astra-vs-sublime-3.md §19) saw one exit with
`sublimite: error -11` and a later BadWindow. -11 is `RENDER_ERR_DEVICE`
(src/render/render.h). That run used the raster backend because EGL was unavailable (code=-10).
The raster backend failed a frame whose Present completion took longer than the
edit-5o0 §15 window, 2 s (G, planning value from edit-5o0, not derived; unchanged here).

## Decision

The window now starts when the X server answers the fence query. That query is
sent in the same flush as the PresentPixmap. While the server has not answered
and the connection reports no error, the server has not yet processed our
requests. That is a slow server, not a lost completion, so the deadline is
re-armed instead of latching device failure. This holds on both paths:

- inline (caret-only) frames: `raster_poll_completions` re-bases
  `completion_deadline` at the fence answer (only when a deadline is armed)
  and re-arms on expiry while `inline_fence` is false;
- full frames: `fence_job_fn` re-bases at T5 and re-arms while `!got_fence`.
  `work_should_stop` still ends the job at shutdown.

If the server answered and then sent no PIXMAP or idle notification for the
whole 2 s window, the frame still fails exactly as before. An X error or a
connection error still fails immediately.

## Residual

A stall that starts after the fence answer but before the Present executes
still counts against the window. The Present waits at most one vblank after the
answer (16.7 ms at 60 Hz (P)). A caret blink frame is issued about every 500 ms
(E, blink period). So the window covers a few percent of stall start times
(16.7/500 = 3.3 % (E)). The SIGSTOP repro (6 + 4 runs) did not hit it. A
liveness probe after the Present would close the gap; it is not done here.

The GL backend's completion handling (src/gl/renderer.inc) was not examined
for the same stall behaviour. The 3 s wait in src/gl/gl_driver.h is a harness
helper, not the editor path.
