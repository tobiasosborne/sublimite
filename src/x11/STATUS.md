# x11 STATUS (edit-e6x.11)
Done: all software items of the bead; make check green, clip test 5/5, 300 s fuzz 19.0M runs 0 crashes, G4a platform TRACK p50 4.96 ms p99 16.9 ms (M)[AC] load 3.67.
Missing: tools/evtrace (needs group `input`), real-touchpad XI2 smooth-scroll check (the "drop core 4-7 within 2 ms of XI2 scroll" rule is still (E)), G4a verdict on a quiet [AC] box and on a real window.
Read first: docs/decisions/P2.2b.md. X tests: always `xvfb-run -a -s '... -noreset'` (tests/x11_xvfb.h does this itself).
