# x11 STATUS (edit-e6x.11)
Done: all software items of the bead; make check green, clip test 5/5, 300 s fuzz 19.0M runs 0 crashes, G4a platform TRACK p50 4.96 ms p99 16.9 ms (M)[AC] load 3.67.
Missing: tools/evtrace (needs group `input`), real-touchpad XI2 smooth-scroll check (the "drop core 4-7 within 2 ms of XI2 scroll" rule is still (E)), G4a verdict on a quiet [AC] box and on a real window.
Read first: docs/decisions/P2.2b.md. X tests: always `xvfb-run -a -s '... -noreset'` (tests/x11_xvfb.h does this itself).

## edit-e6x.20 (xi2 review fixes: x11-1 #16 #17 #20 #27)
Done: xi2.c/xi2.h rewritten around `xi2_axis` (multi-axis per device, valuator-class history seeding, per-axis remainder, all-or-nothing QueryDevice/motion decode); tests `test_e6x20` in tests/x11_xi2_test.c; XI2 ops in fuzz/x11_input_fuzz.c. Design: docs/decisions/P2.2f.md.
Missing: x11.c ignores a -1 from xi2_parse_query_device (table now survives it; proposal in P2.2f.md); seeding from classes inside XI_DeviceChanged; real-touchpad check.
Verify: `make build/san/tests/x11_xi2_test && build/san/tests/x11_xi2_test`.
# x11 STATUS (edit-e6x.11, review fixes edit-e6x.18 clip part)
Done: all software items of e6x.11; clip review fixes #1 #4 #5 #6 #14 #15 #28 and the bounded part of #13 (docs/decisions/P2.2d.md). make all, make check (29 binaries, leaks on, DISPLAY=:99), make fuzz build green; x11_clip_test 5/5.
Missing: #13 residuals (receive-buffer growth, plat_clip_set memcpy and Latin-1 conversion still on the UI thread; needs a work/ worker), tools/evtrace (needs group `input`), real-touchpad XI2 smooth-scroll check, G4a verdict on a quiet [AC] box. Input ordering (x11.c/input.c) is edit-e6x.19, xi2.c edit-e6x.20.
Read first: docs/decisions/P2.2b.md then P2.2d.md. X tests: tests/x11_xvfb.h starts its own Xvfb with -noreset.
# x11 STATUS — P2.2e (edit-e6x.19)

Done: review §2 raw bursts, §3 callback order, §7 repeat modifier/group updates,
§8 bounded drain slices/quit, §9 text-free releases, §10 repeat-rate bounds,
§11 detectable-repeat negotiation, §12 asynchronous device rescans and worker
keymap/state rebuilding, §19 core held-button state, §26 compose allocation
failure, §29 compose-table cleanup, and §30 startup T0. XKB action-filter storage
is prepared before typing, including state rebuilt on the worker. No `plat.h`,
frozen header, clipboard/XI2 implementation, or other module was changed.

Restricted-file proposals: §2 confirmed local clipboard completions can still
be dropped if callers fill the ring before requesting; `clip.c` must reserve
capacity/reject saturation and retain deferred completions. §18 XI2 button
selection/decoding with source/emulation identity must replace timestamp
heuristics. §19 XI2 held-mask decoding must include buttons through 32 and keep
extended bits out of the core modifier/group mask. See the finding sections in
[the decision](../../docs/decisions/P2.2e.md); these are confirmed, not dismissed.

Verification: `DISPLAY=:99 EDIT_DISPLAY=:99 make all`; the same environment
with `ASAN_OPTIONS=detect_leaks=0 make check`; `make fuzz`; then
`ASAN_OPTIONS=detect_leaks=0 build/fuzz/x11_input_fuzz -max_total_time=120 -max_len=512`.
Xvfb tests use `tests/x11_xvfb.h`; sandboxed runners need local X socket access.
The default `x11_order_test` checks all implemented fixes. Explicit arguments
`local_overflow`, `wheel_pending`, and `xi_mask_pending` are retained failing
reproductions for the restricted-file proposals, excluded from default checks.
The cold-state allocator assertion runs in the release `x11_input_test`;
the ASan allocator guard is inert. Coordinator must rerun leak detection.

The single final TRACK bench (before its discovered cold-allocation fix):
power Full [AC], one-minute load 2.16; translation p50 230 ns / p99 661 ns,
startup p50 7.320 ms / p99 18.259 ms (M)[AC] load 2.16. It exited 1 on one
allocation. The added cold-state test reproduced three allocations; state
preparation fixed that and the release test is green. No bench rerun; the
coordinator measures the final code on a quiet box. These timings are not gates.

Earlier manual gaps remain: `tools/evtrace` needs input-device permission;
real-touchpad XI2 verification and the full real-window startup verdict are
still pending. Runtime refresh uses the last published keymap until a validated
worker result emits KEYMAP; failed rebuilds retain that map. The worker's X fd
is shut down before join to cancel blocked server I/O.

Final-code fuzz: 4,874,884 runs in 121 s, zero sanitizer errors (M)[AC],
Not charging, one-minute load 4.26 (`-max_total_time=120`). Two earlier verification
runs are recorded in the decision doc. Release `x11_input_test` and sanitizer
`x11_order_test` are green, including the cold allocator and reply-poll order
regressions. `make all` and `make fuzz` exit 0. Final `make check`: 30 test binaries and
the replay CLI passed (M)[AC], Not charging, one-minute load 3.08,
`DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0`.

Runtime rename (edit-457.18):
Default title/_NET_WM_NAME sublimité (UTF8_STRING), ASCII WM_NAME sublimite,
WM_CLASS sublimite/sublimite. Verify build/tests/x11_identity_test on :99;
editor_test and cli_test verify the editor window identity.
Decision/evidence: ../../docs/decisions/rename-sublimite.md.
Final rename verification: gcc make all passed; clang ASan/UBSan make check
passed with leaks disabled; make fuzz built all fuzzers. Focused identity and
isolated desktop-install contracts passed. Complete red/green and stamped
fuzz results are in the decision document; no hot-path bench was rerun.

Amendment (edit-457.18): always lower case; UTF-8 title/_NET_WM_NAME
`sublimité`, ASCII WM_NAME `sublimite`, both WM_CLASS parts `sublimite`.
Focused X11 readback passed after the amended expectations failed first.
No remaining identity work; font/wordmark work is deferred. Verify with
DISPLAY=:99 EDIT_DISPLAY=:99 make all/check/fuzz and the identity tests above.
Amendment evidence: ../../docs/decisions/rename-sublimite.md.
Amendment final verification: gcc make all, clang ASan/UBSan make check
(detect_leaks=0), make fuzz and the focused contracts passed; no new open
problem. Stamped X11 fuzz smoke evidence is in the decision document.
## P2.2g — edit-e6x.21 (review §21–25)

Done: clipboard sequence fuzz operations against a private raw peer; P2.2f
populated XI2 reachability confirmed without changing XI2. Startup rejects
invalid dimensions and checked colormap/window/Present errors with cleanup.
Clipboard test initialization failures after Xvfb startup are fatal;
EDIT_X11_STRICT=1 also rejects unavailable private infrastructure. Startup bench
errors and subset budget misses fail; --self-check is synthetic, unmeasured.
The live test now includes tests/x11_test_main.c.in with mapping, expose, resize,
focus, close, real Present serial/timestamps, burst/order and zero-idle-wakeup
assertions; an independent parent watchdog bounds it. Historical manual template
under src/x11 is untouched. See docs/decisions/P2.2g.md for each red/green.

Verified: make all exit 0; make check passed 35 sanitizer binaries and replay CLI
with DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 EDIT_X11_STRICT=1;
make fuzz built 20 fuzzers. Final raw-peer fuzz: 9364 runs / 121 s, no errors
(M)[AC], Not charging, load 7.20. Leak detection remains for coordinator.

Missing in this bead: full G4a exec → completed CPU buffer + map + accepted input,
including verified cold-cache mode, requires an editor/raster fixture outside
scope. The current platform subset passing cannot establish the full gate.
Earlier clipboard bulk-work and XI2/input proposals and manual touchpad/evtrace
checks remain as documented in P2.2d–f; this bead does not address them.

Additional restricted clipboard issue discovered by §21's raw-peer model:
CurrentTime (last_time=0) ownership race fails completion/data association;
nonzero server-time recipe passes. Reproducer and clip.c proposal are in P2.2g.
No clipboard fix or default failing test was introduced outside the bead scope.
Pure decoder fuzz: 2363697 runs / 121 s, no errors (M)[AC], load 6.49.
Single final TRACK bench (Not charging [AC], load 7.20): translation p50/p99
341/621 ns, zero allocations; startup subset 8.282/8.934 ms, exit 0 (M).
No full G4a verdict and no bench rerun.

## P2.2h (edit-e6x.24): clipboard bulk work off the UI thread
Done: receive growth/copy/Latin-1 conversion, final shrink and big-blob frees run on a private src/work bulk worker
behind a mailbox; the UI thread only hands replies over and receives completed buffers. Memory accounting stays
exact (see docs/decisions/P2.2h.md). New API in clip.h: x11_clip_ui_bytes, x11_clip_max_poll_ns, x11_clip_buf_*
and x11_clip_set_buf (zero-copy publish). tests/x11_clip_test.c: 64 MiB receive latency/bytes/mem test, Latin-1
expansion, worker-side limit failure, zero-copy set.
Missing: plat_clip_set still memcpy's on the caller (borrowed buffer; use x11_clip_set_buf); serving converts Latin-1
per chunk on the UI thread; the 1 ms polling wake instead of the pool eventfd (needs an x11.c edit, proposed).
Verify: DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 make check; run build/tests/x11_clip_test for the
release numbers (guard test of "no allocation when typing is queued" is active only in the release build).
