# edit-cee (s6): Astra drive 3 "error -11" at the end of §19

Usage at start: weekly 84 %, 5 h 31 %. At end: weekly 85 %, 5 h 2 % (the window reset).
Worktree: .wt/edit-cee-s6 (branch wt/edit-cee-s6, from main acc1a36).

## Evidence (build/astra3, binary 11baa63, raster backend)

- stderr, logs/19-external-controlled-sublimite.editor.log:
  `EGL init failed ... (code=-10); using raster` and
  `sublimite: error -11; journal: .../session-W3BwwS`.
- -11 is `RENDER_ERR_DEVICE` (src/render/render.h:17). It is a render-module
  code, not an errno. EDITOR_ERR_* are -100..-105. No other module enum uses -11.
- When it exited (M): the trace dump and editor log both have mtime
  1791812971.783. The "final" shot started at 1791812969.589, so the exit came
  2.19 s later. The driver's WM_DELETE_WINDOW came at 1791812975.818, 4.03 s
  after the exit. **The BadWindow is the driver's ClientMessage hitting an
  already-destroyed window, not a second fault.** The reload/keep code is not
  involved; the K readback had finished 6.2 s before the exit.
- Load during the drive (M)[AC]: load1 17.22 at the start of the run.
  ffmpeg x11grab ran at 2880x1800 and 60 fps. Each `import -window root` shot took
  1.55-2.9 s (M, the gap from a shot mark to the next mark in
  19-external-controlled-sublimite.json).
- The trace dump (traces/19-external-controlled-sublimite.txt) has only the
  startup and latency summaries. It cannot attribute the failure.

## Cause

The raster backend treats a Present completion that is missing for 2 s as
device loss. The 2 s is a (G) planning value from edit-5o0 §15. Both the inline
caret path (`raster_poll_completions`) and the worker fence job
(`fence_job_fn`) counted the 2 s from the moment the Present was issued. When
the X server is stalled for 2 s or more (load plus a root screenshot), the
frame failed, `pump()` returned the error, and the editor exited. The journal
was kept, so no data was lost, but the window vanished.

## Repro (private Xvfb :147/:148, so SIGSTOP never touches the shared :99)

Script: SIGSTOP Xvfb for t seconds while the editor (EDIT_BACKEND=raster) is
idle with a blinking caret, then check the process.

- main @acc1a36, 7 runs: t=1.0/1.5/1.5 all alive; t=2.5/2.5/3.0/3.0 all exited
  with `error -11`. The drive's message reproduces deterministically.
- With only the inline fix, 6 runs: 3 still exited with -11. That exposed the
  same 2 s issue in the worker fence job.
- With both fixes, 10 runs (t=2.5 x3, 3.0 x5, 5.0 x2): all alive. Functional check
  across a 3 s stall: typing "AB", stall, typing "CD", then Ctrl+S saved
  `ABCD01 line`.
- Power and load during the runs: BAT0 "Not charging"; load1 between 5.96 and
  30.00 (M, other landers).

## Fix (red then green)

- src/raster/raster.c: `RASTER_COMPLETION_NS` (still 2 s). The window now starts
  when the fence query is answered, and is re-armed while that answer is missing
  on a healthy connection. Both paths are covered. On the inline path the
  re-base happens only when a deadline is armed. Rationale:
  docs/decisions/edit-cee.md.
- tests/raster_test.c:
  - `--review P2-1-15` was rewritten. An unanswered fence now re-arms and the
    frame then completes; a missing PIXMAP or idle after the answer still fails
    at answer + 2 s. RED against main: `raster_test:1534 FAIL ...
    missing=fence expiry=-11`. GREEN: expiry=0 for fence, pixmap and idle.
  - New `--review edit-cee` drives the worker fence job with 1 s of fake time
    per loop turn for 10 turns. RED: `missing=fence messages=1 fail=1`.
    GREEN: the fence case gives `messages=0 fail=0`; the pixmap case still
    gives `fail=1`.
  - The full build/tests/raster_test and build/san/tests/raster_test (ASan/UBSan)
    exit 0 on :99 under tools/with_display_lock.sh. build/san/tests/raster_scroll_test
    exits 0.
- fuzz/raster_fuzz.c: the inline deadline-expiry op now expects a re-arm or a
  re-base first, then failure only if the fence was answered.

## Found, not fixed (law 3)

- **The raster fuzzer crashes on main for every input, including empty input.**
  `cpu_paint_margins` (edit-u82, 712b440) calls `xcb_change_gc`, and the fuzz
  transport does not mock it, so it segfaults in libxcb. I verified the fuzz op
  with a scratch, uncommitted one-line mock: 19954 runs in 91 s and 28808 runs
  in 60 s, no crash (M). Then I reverted the mock, so the landed fuzzer
  still has the main-branch crash.
- Residual window: a stall that starts after the fence answer and before the
  Present executes (at most one vblank) still counts. See the decision doc.
- GL backend completion handling (src/gl/renderer.inc) was not examined for the
  same stall behaviour.
- With caret-only frames, the raster fd is outside epoll
  (`raster_poll_set`), so completions are seen only at the next wake. This did
  not cause the failure; noted for whoever revisits blink CPU.
