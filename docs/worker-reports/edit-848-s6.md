# edit-848 (s6): GL completion timeout on a stalled X server

Usage at start: weekly 87 %, 5 h 9 %. At end: weekly 87 %, 5 h 11 %.
Worktree: .wt/edit-848-s6 (branch wt/edit-848-s6, from main f477c02).
Power: BAT0 "Not charging" (AC), load1 3.18 at the start (M).

## (1) Does GL have the cee policy? Yes.

`gl_present` armed `completion_deadline = swap + GL_COMPLETION_TIMEOUT_NS`
(3 s, (G) with no recorded derivation; unchanged). `gl_event` called `gl_fail`
on expiry if the swap's PIXMAP CompleteNotify had not arrived, whatever the cause.
A stopped or stalled X server for 3 s or more during a frame
therefore ended the editor with -11, as on raster. The GL fence
(`glClientWaitSync`) is local and is not affected by the server.

## (2) Fix (src/gl/renderer.inc, `gl_completion_window`)

At expiry, one `GetInputFocus` round trip goes out on `native_conn`. Mesa sends
the swap's PresentPixmap on that connection, so the answer is ordered after it.
If the server has not answered, the window re-arms (one probe only). After the
answer, the window restarts and a missing PIXMAP still fails. An X connection
error, or no X connection (Wayland), still fails. Normal frames send no probe.
A probe still pending at the next swap is discarded. Rationale:
docs/decisions/edit-848.md.

Test: tests/gl_test.c `--review edit-848` (also run from the default suite,
both the native and the non-native branch). It uses a scripted xcb connection,
selected by sentinel address. Every other connection reaches real libxcb.

RED (main renderer, test added; built with -Wno-error=unused-function only
because the new mocks were not yet referenced):
```
gl_test:889: FAIL rc==RENDER_OK && !s.failed && b.active && !b.t6_sent
edit-848 gl: lost=0 first expiry=-11 probes=0
exit=1
```
GREEN:
```
edit-848 gl: lost=0 first expiry=0 probes=1
edit-848 gl: lost=1 first expiry=0 probes=1
edit-848 gl: answered, PIXMAP missing a window later: -11
gl_review: edit-848 PASS (stalled server re-arms; answered server without PIXMAP still fails)
exit=0
```

Live repro: **not possible on this box.** I started a private Xvfb :147 and ran
EDIT_BACKEND=gl. It logged
`sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry`.
gl_test on :99 also skips the native Present contracts (result=-10). The fix is
verified only at unit level. A GPU X session would be needed for the SIGSTOP
drive.

## (3) The cee residual window

- GL: closed by construction. The probe goes out after the expiry, so its answer
  comes after any stall that began during the window. Failing now needs a
  second stall of 3 s or more that starts within a vblank of the answer.
- Raster: not done. It is not a few lines. It changes two paths (inline UI poll
  and the worker fence job) and both review fixtures, whose reply mock answers
  every sequence. The fuzz transport needs a new mock and oracle, and that fuzzer
  already crashes on main. The window covers about 3.3 % (E) of stall starts and
  hit 0 of 10 runs (M, cee). The decision doc describes the port.

## Verification

- `make all`: exit 0 (gcc, -Werror).
- Under tools/with_display_lock.sh with DISPLAY=:99, EDIT_DISPLAY=:99 and a
  private XDG_RUNTIME_DIR, run serially: build/san/tests/gl_test exit 0,
  `gl_test --review edit-848` exit 0, raster_test exit 0, raster_scroll_test
  exit 0. Release gl_test exit 0 (section 6 includes the new check).

## Found, not fixed (law 3)

- My first four EDIT_BACKEND=gl launches on :147 (05:14 to 05:15) ran
  without XDG_DATA_HOME, so they used the real ~/.local/share/sublimite.
  Their startup housekeeping may have set aside orphan journals there. There
  are `.set-aside-*` names stamped 05:14 to 05:15, but other agents were active in
  that directory too, so I cannot attribute them. Set-aside keeps the data.
  I deleted nothing. One run exited with `error -103` (journal
  session-RpmCc4). The cause was not investigated. Later runs used a private
  XDG_DATA_HOME.
- If Mesa does a blocking round trip on the UI thread during a stall
  (eglMakeCurrent, drawable invalidation), the editor would pause, not exit.
  This is unverified, because GL cannot start here.
