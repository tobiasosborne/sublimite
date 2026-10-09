# edit-e6x.11 session 6 report
Changed: docs/decisions/P2.2b.md (Session 6 finish), src/x11/STATUS.md. No code changes were needed.
Red run: none; no failures reproduced, so no fix was made.
Green: make all rc=0; make check "check: 24 test binaries passed", test_replay_cli all passed; make fuzz "fuzz: 12 fuzzers built".
Clip test: build/san/tests/x11_clip_test 5/5 "x11_clip_test: ok".
Fuzz: x11_input_fuzz -max_total_time=300: "Done 19014645 runs in 301 second(s)", 0 crashes.
Bench (Charging [AC], load 3.67): startup main->plat_init->map p50=4.961 ms p99=16.899 ms (M)[AC] TRACK vs G4a 25/40 ms (excludes exec and raster); key translate p50 187 ns p99 240 ns, 0 allocs.
Open: tools/evtrace (needs group input) and real-touchpad XI2 check need Tobias; G4a verdict on a quiet box; p99 16.9 ms is load noise-prone.
