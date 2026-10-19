# check18 triage: eight failing sanitizer suites on main df277ff (2026-10-20)

Worker: diagnosis only (no source edits, no git, no bd). Binaries: build/san/tests/* as built 02:12 (df277ff).
All runs: AC (BAT0 "Charging"), load1 16-40 (stamped per run below). Run logs:
/tmp/claude-1000/-home-tobias-Projects-sublimite/2d812017-7f93-44c2-a0ab-fe83e6bed443/scratchpad/runs/<suite>-<tag>.log

Standard command (one suite alone, private runtime dir, display :98 reserved for this triage):

    XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:98 EDIT_DISPLAY=:98 ASAN_OPTIONS=detect_leaks=1 \
      sh tools/with_display_lock.sh build/san/tests/<suite>

How `make check` differs from the check18 loop: the Makefile's SAFE_DISPLAY maps DISPLAY to
`${EDIT_DISPLAY:-:99}` only when DISPLAY is real (:0 etc.) and runs DISPLAY_TEST_SAN suites under
with_display_lock.sh on that one display; check18 ran every suite with DISPLAY=:98 under the :98 lock,
so suites that hard-code :99 (x11_identity_test, x11_clip_review_test) talked to :99 without its lock.
Session 16 (build/check16/make-check.log, 8f8a21e) ran at load1 1.42 on :99, with /tmp/edit-corpus present.
The libEGL "DRI3 error: Could not get DRI3 device" warnings are also in the session-16 log (40 lines):
Xvfb never offers DRI3; they are not the cause of anything below.

## Root cause 1 (environment): the restarted Xvfbs lack `-noreset`

The documented launch line (HANDOFF/worklog) is `Xvfb :99 -screen 0 2880x1800x24 -nolisten tcp -noreset`.
After the reboot :95/:97/:98/:99 were started as `Xvfb :N -screen 0 2880x1800x24 -dpi 100` (no -noreset).
Without -noreset the server regenerates every time its last client disconnects; a connection that arrives
during regeneration is accepted on the socket and then hung up during setup, so xcb_connect returns a
connection with error 1 (XCB_CONN_ERROR). plat_init maps that to PLAT_ERR_NO_DISPLAY (-1).

Evidence (03:18-03:21, load1 ~18-22, AC):
- probe linking build/san/libedit.a, 200 x (plat_init, plat_map, plat_shutdown) on :98: `87/200 plat_init failures`, every one rc=-1 (PLAT_ERR_NO_DISPLAY = xcb_connect failed). gdb: x11_plat_shutdown never reached on the failing iteration, i.e. failure precedes the colormap/window/Present/xkb steps.
- bare xcb_connect/xcb_disconnect loop (scratchpad probe/conn.c): :98 33/300 and 68/300 failures, all code 1; :95 37/200, :97 47/200; :99 0/200 and :0 0/200 at that moment (:99 had 3 worker clients connected, :0 is the real Xorg). strace of a failing connect: connect() = 0, setup request written, then POLLERR|POLLHUP.
- same loop while one extra client stays connected (`xprop -display :98 -root -spy` or a held connection): 0/300, 0/300, 0/300.

Fix (exact line, per display; needs a restart by the coordinator):

    Xvfb :N -screen 0 2880x1800x24 -dpi 100 -nolisten tcp -noreset

Workaround without restart: keep one idle client connected per display, e.g.
`xprop -display :N -root -spy >/dev/null 2>&1 &` (used for the confirming runs below).

## Root cause 2 (environment): /tmp/edit-corpus wiped by the reboot, and mkcorpus does not make sparse_10g.bin

/tmp is on / and is cleared at boot. tools/mkcorpus regenerates the perf corpus but NOT
`sparse_10g.bin` (HANDOFF: the sparse 10 GiB G5 fixture) nor `all_a_1g.txt` (G6v near-miss fixture).
The coordinator's 03:24 regeneration has no sparse_10g.bin. Needed in addition:

    truncate -s 10G /tmp/edit-corpus/sparse_10g.bin

(editor_large_test:2104 mapped_preview_first and lineidx_test:333 test_combined_ownership hard-require it.)
For verification before the coordinator's corpus existed I ran tests in a private mount namespace
(`unshare -rm` + nested `unshare -U --map-user=1000`, private tmpfs /tmp with /tmp/edit-corpus bound to
a scratch corpus = mkcorpus output + the truncate above, X socket dir and the :98 lock file bound through),
so the shared /tmp was not touched while another lane was diagnosing lineidx_test.

---

## editor_large_test: (a) corpus + (b) load flake

- check18: `editor_large_test:714 FAIL !e->buffer->pending_open && editor_large_status_get(e).mapped` then `:2069 wrapped_byte_jump() == 0` (known load flake, edit-gfx).
- Alone on :98 without corpus (03:13, load1 21.5): rc=1, `editor_large_test:1133: FAIL stat(path, &st) == 0` (starved_follows needs /tmp/edit-corpus/log_1g.txt), `:2072 starved_follows(true, false) == 0`.
- Private corpus incl. sparse_10g.bin (03:16:20-03:16:44, load1 17-19): **rc=0**, `editor_large_test: ok`.
- Coordinator corpus (no sparse_10g.bin), :98 holder, two runs at load1 26.5 and 30.6: rc=1 at
  `:1162 FAIL trace_now_ns() < deadline` (5 s watchdog waiting for the 1 GiB index, starved_follows(true,true)) and
  `:931 FAIL trace_now_ns() < deadline` (cooperative_large_jobs). Different site each run (714, 1162, 931): 5 s fixture watchdogs under ASan at load1 > 25.
- Would additionally fail at :2104 mapped_preview_first until sparse_10g.bin is recreated.

## gl_test: (a) Xvfb without -noreset

- check18 on :98: `gl_test:582: FAIL gl_driver_prepare(d,b,cfg)==RENDER_OK` (via :640 gl_review_readback_open, :1667 gl_review_binding_test). On :99: same :582 via :716/:1670.
- Alone on :98 (03:16:38, load1 17.5): rc=1, `gl_test:582` via `:920 gl_review_readback_open`, `:1674 gl_review_continuation_test` - the failing section moves between runs.
- Which probe: gl_driver_prepare (src/gl/gl_driver.h:73) returns RENDER_ERR_UNSUPPORTED when plat_init != PLAT_OK; plat_init fails with PLAT_ERR_NO_DISPLAY (-1) at xcb_connect (src/x11/x11.c:492-496) because the previous window's disconnect left :98 with no clients and the server was regenerating (root cause 1). Not EGL/GLX/DRI.
- With an idle client held on :98 (03:21:46-03:22:11, load1 19.8-22.6): **rc=0**.

## raster_test: (a) Xvfb without -noreset

- check18: `raster_test:605: FAIL render_test_prepare(&b,&cfg)==RENDER_OK` (raster_native_resize_test shuts g_plat down and re-inits it).
- Alone on :98 (03:16:56, load1 16.3): rc=1, now at `render_test:229: FAIL render_test_prepare(&b, &cfg) == RENDER_OK` (first prepare).
- Which probe: tests/raster_test.c:265 render_test_prepare returns RENDER_ERR_INIT when plat_init fails; same xcb_connect failure as gl_test.
- With an idle client held on :98 (03:22:11-03:25:05, load1 22.6-26.1): **rc=0**.

## wayland_startup_test: (c) test bug since edit-chy (0f09330, 2026-10-18)

- check18 and alone (03:11:45, load1 26; again 03:41 with holder): rc=134, `tests/wayland_startup_test.c:37: void startup_case(_Bool): Assertion 'readable' failed.` Deterministic, environment-independent.
- Mechanism: edit-chy made EDIT_ALLOW_REAL_DISPLAY a no-op and made refusal opt-in via EDIT_REFUSE_REAL_DISPLAY (src/main.c:33-50), which tests/display_guard.h's constructor now exports unconditionally (and with_display_lock.sh too). startup_case(false) (tests/wayland_startup_test.c:24-25) still models the old opt-in: it sets EDIT_ALLOW_REAL_DISPLAY=1 and expects "cannot connect to Wayland compositor", but the child inherits EDIT_REFUSE_REAL_DISPLAY=1, so main.c:44-49 prints "Wayland display guard refused compositor" and the substring never matches; assert(readable) at :37 aborts. Session 16 (8f8a21e, before edit-chy) passed.
- Proof with the release binary: same env as the child plus EDIT_REFUSE_REAL_DISPLAY=1 prints the guard refusal; with EDIT_REFUSE_REAL_DISPLAY unset it prints `sublimite: cannot connect to Wayland compositor (...)`, rc=1.
- Fix direction: in the unguarded child `unsetenv("EDIT_REFUSE_REAL_DISPLAY")` instead of (or besides) setting EDIT_ALLOW_REAL_DISPLAY. Test-only; product behaviour is as designed by edit-chy.

## work_foreground_test: (a) corpus missing

- check18 and alone without corpus (03:11:46): rc=1, `work_foreground_test:102 FAIL ... pauses[active].entered` (x2), `:242 FAIL fd >= 0`, `:243 FAIL write(...)`, then UBSan `src/file/file.c:970:53: member access within null pointer` and ASan SEGV in file_open_mode from `tests/work_foreground_test.c:247`.
- Mechanism: the test writes into /tmp/edit-corpus (savectl path at :87, mkstemp at :241). With the directory gone, the save never reaches FILE_STEP_TEMP_CREATED (so the pause at :102 is never entered), mkstemp fails, editor_open fails, and the non-fatal CHECK at :246 lets :247 dereference e == NULL. The SEGV is the test continuing after a failed CHECK (a test robustness nit, not a product bug).
- With corpus (private 03:16, coordinator's 03:41): **rc=0** both times, `work_foreground_test: ok`.

## x11_clip_review_test: (a) no -noreset on :99 + (b) display sharing in the check18 loop

- check18: `FAIL: connect DISPLAY=:99` / `FAIL: fixture`. The test hard-codes `xcb_connect(":99")` (tests/x11_clip_review_test.c:11) regardless of DISPLAY, so in check18 it ran on :99 without the :99 lock.
- Under the proper :99 lock (03:27:22-03:37:13, waited ~10 min for a worker, load1 38->21): rc=1, same `FAIL: connect DISPLAY=:99` after `clipboard arena adoption/borrow/release: PASS`. Each section does fixture_init/xcb_disconnect; when the test is :99's only client its own disconnect triggers server regeneration and the next fixture's xcb_connect is hung up (root cause 1).
- Under the :99 lock with one idle client held on :99 (03:37:33-03:40:41, load1 21-27): **rc=0**, sections arena, 1, 26, 27, 28 PASS.

## x11_clip_test: (b) load flake (test timing window)

- check18 and alone on :98 three times (03:11:52, 03:25:05 with holder, plus two gdb runs; load1 25-40): rc=1, `FAIL tests/x11_clip_test.c:272: large snapshot released`. The test runs its own private Xvfb (tests/x11_xvfb.h, `-displayfd ... -noreset`) whenever DISPLAY/EDIT_DISPLAY are not both :99, so neither shared displays nor root cause 1 apply (it did so in session 16 as well; the "server already running" lines are -displayfd probing).
- Mechanism: x11_clip_busy() == 1 at :272 is `frees_out == 1`: the 20 MiB snapshot's deferred free is still on the clipboard worker (src/x11/clip.c:475 increments, :439 MSG_FREE decrements). The test allows only `pump(a, sa, r, 3)` = 3 x plat_run_for(4 ms) ~ 12 ms (E) for the worker to free a 20 MiB ASan block and post MSG_FREE. gdb at :272: after the pump, busy=1 and frees_out=1; one more `x11_clip_poll(a)` call (after gdb's own delay) gives busy=0, frees_out=0. The slot is released, just later than the fixed 12 ms window. The same run reports `worst UI poll 16.770 ms wall` (M), i.e. scheduling gaps longer than the window at this load. Session 16 (load1 1.42) passed. Fix direction (backlog, not trial-affecting): pump until a deadline as :290 already does (`while (x11_clip_busy(a) && now_ms() - t0 < 1000) pump(...)`).
- No src/x11/clip.c change since session 16; the one src/x11 change (5646a5f edit-q0g, x11.c keymap listener) does not touch the clip free path.

## x11_identity_test: (c) test hard-codes :99 (harness portability, not a regression)

- check18 and alone on :98: rc=1, `x11_identity_test:23: FAIL getenv("DISPLAY") != NULL && strcmp(getenv("DISPLAY"), ":99") == 0`.
- Under the :99 lock (03:37:13-03:37:17): **rc=0**, `WM_CLASS, UTF8_STRING _NET_WM_NAME and WM_NAME readback passed on :99`.
- Mechanism: tests/x11_identity_test.c:23 asserts DISPLAY is exactly ":99". It passes under `make check` (SAFE_DISPLAY defaults to :99) and fails on any other private display. Fix direction: accept any display the guard accepts (e.g. editor_runtime_display(d, NULL, "") == d) or skip with a message. Low priority; affects only off-:99 sanitizer loops.

## Summary

| suite | class | cause |
|---|---|---|
| editor_large_test | (a)+(b) | corpus missing (log_1g, sparse_10g); then 5 s watchdogs miss at load1 > 25 (site varies: 714/931/1162); passes with full corpus at load1 17-19 |
| gl_test | (a) | Xvfb without -noreset: xcb_connect hung up during server regeneration, plat_init -1 -> gl_driver_prepare fails; passes with an idle client held |
| raster_test | (a) | same as gl_test via render_test_prepare -> plat_init |
| wayland_startup_test | (c) | test still uses EDIT_ALLOW_REAL_DISPLAY opt-in; since edit-chy the inherited EDIT_REFUSE_REAL_DISPLAY wins (tests/wayland_startup_test.c:24-25, :37) |
| work_foreground_test | (a) | /tmp/edit-corpus missing; passes with corpus |
| x11_clip_review_test | (a)+(b) | hard-coded :99 without its lock in check18; under the lock still fails from no -noreset (own disconnect resets :99); passes with idle client |
| x11_clip_test | (b) | 20 MiB deferred free not reported within the fixed 3 x 4 ms pump at :272 under load; slot is released one poll later |
| x11_identity_test | (c) | asserts DISPLAY == ":99" (tests/x11_identity_test.c:23); passes on :99 |
