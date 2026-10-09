# edit-e6x.20 report (x11-1 #16 #17 #20 #27)

All four findings confirmed and fixed; none rejected. Design: docs/decisions/P2.2f.md.

## Files (worktree /home/tobias/Projects/editor/.wt/edit-e6x.20)
- src/x11/xi2.h, src/x11/xi2.c: xi2_dev now holds up to 4 xi2_axis {num, dir, preferred, inc, last, rem, last_ok}. QueryDevice seeds history from ValuatorClass values (#16), parses into scratch and commits only on full success incl. padded-name check (#20). Decode stages values, validates all extents first, then commits; per-axis remainder (#17); all scroll classes kept, Preferred axis wins per direction when several move together (#27).
- tests/x11_xi2_test.c: test_e6x20 (existing field assertions adapted to the axis struct).
- fuzz/x11_input_fuzz.c: appended XI2 ops (well-shaped QueryDevice with scroll+valuator classes from fuzz bytes, truncation, motion events; traps if a rejected parse/decode changed state or a remainder leaves [-0.5,0.5]).
- docs/decisions/P2.2f.md (new), src/x11/STATUS.md (section appended).

## Red run (old xi2.c, new tests)
    FAIL tests/x11_xi2_test.c:132: #16 first scroll after query: dy 0 want 64 (order 0)
    FAIL tests/x11_xi2_test.c:141: #16 first scroll after rescan: dy 0 want 128
    FAIL tests/x11_xi2_test.c:132: #16 first scroll after query: dy 0 want 64 (order 1)
    FAIL tests/x11_xi2_test.c:141: #16 first scroll after rescan: dy 0 want 128
    FAIL tests/x11_xi2_test.c:151: #17 cumulative total 0 want 256
    FAIL tests/x11_xi2_test.c:159: #17 3x0.4 notch total 306 want 307
    FAIL tests/x11_xi2_test.c:166: #20 missing name rejected
    FAIL tests/x11_xi2_test.c:179: #20 table unchanged after rejected QueryDevice
    FAIL tests/x11_xi2_test.c:192: #20 history unchanged after rejected motion
    FAIL tests/x11_xi2_test.c:206: #27 first axis alone (perm 0)
    FAIL tests/x11_xi2_test.c:207: #27 second axis alone (perm 0)
    FAIL tests/x11_xi2_test.c:206: #27 first axis alone (perm 1)
    FAIL tests/x11_xi2_test.c:207: #27 second axis alone (perm 1)
    x11_xi2_test: FAILED

## Green run
    x11_xi2_test: ok   (build/san/tests/x11_xi2_test, ASan/UBSan, leaks on, DISPLAY=:99)

## Gates
- make all: green (gcc release, -Werror).
- make check: "check: 31 test binaries passed" (leaks on, DISPLAY=:99), 0 FAIL lines.
- make fuzz: "fuzz: 18 fuzzers built".
- x11_input_fuzz 120 s: 5,385,346 runs in 121 s (44.5k exec/s), 0 crashes, leaks on (M) (box loaded, shared).
- No benches run (not in scope; decode path adds no allocation: stack scratch only).

## Open problems / notes
- x11.c (not mine) ignores a -1 from xi2_parse_query_device; the table now survives -1 intact, so no correctness hook is needed (proposal in P2.2f.md).
- Not done: seeding history from classes inside XI_DeviceChanged events (QueryDevice rescan covers it); real-touchpad verification still needs Tobias.
- Axis cap is 4 per device (extras silently ignored); the no-emulation flag is deliberately not used as a filter.
- The red line for the 3x0.4 notch check shows the old code losing a unit (306 vs 307) even without a repeated-zero case.
