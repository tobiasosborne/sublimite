# x11 STATUS (edit-e6x.11)
Done: all software items of the bead; make check green, clip test 5/5, 300 s fuzz 19.0M runs 0 crashes, G4a platform TRACK p50 4.96 ms p99 16.9 ms (M)[AC] load 3.67.
Missing: tools/evtrace (needs group `input`), real-touchpad XI2 smooth-scroll check (the "drop core 4-7 within 2 ms of XI2 scroll" rule is still (E)), G4a verdict on a quiet [AC] box and on a real window.
Read first: docs/decisions/P2.2b.md. X tests: always `xvfb-run -a -s '... -noreset'` (tests/x11_xvfb.h does this itself).

## edit-e6x.20 (xi2 review fixes: x11-1 #16 #17 #20 #27)
Done: xi2.c/xi2.h rewritten around `xi2_axis` (multi-axis per device, valuator-class history seeding, per-axis remainder, all-or-nothing QueryDevice/motion decode); tests `test_e6x20` in tests/x11_xi2_test.c; XI2 ops in fuzz/x11_input_fuzz.c. Design: docs/decisions/P2.2f.md.
Missing: x11.c ignores a -1 from xi2_parse_query_device (table now survives it; proposal in P2.2f.md); seeding from classes inside XI_DeviceChanged; real-touchpad check.
Verify: `make build/san/tests/x11_xi2_test && build/san/tests/x11_xi2_test`.
