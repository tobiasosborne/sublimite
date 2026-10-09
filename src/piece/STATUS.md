# Piece status — P1.4d / edit-4w1.36

Implemented review findings §1, §2 and §11: checked snapshot owner cap,
preflight insertion/ref growth, checked internal byte-length updates, permanent
uint64_t boundary tests, an owner-slot race, and wide-position/reference-growth
fuzz cases. The near-maximum test also corrected cursor deletion's intermediate
pointer arithmetic. Public/frozen headers and tests are unchanged; findings
§3–§10 remain assigned separately.

Design and pasted red/green evidence: `docs/decisions/P1.4d.md`.
Test hooks are absent from ordinary release objects. Their synthetic repeated
byte fixture has exact logical byte sums but must not be exhaustively counted
or iterated; the physical-add-length hook is for rejection-only probes.

Verify with `DISPLAY=:99 EDIT_DISPLAY=:99 make all`, then
`DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 make check`,
and `DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz` plus
`DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/fuzz/piece_fuzz -max_total_time=120`.
Use `build/san/tests/piece_bounds_test --owners`, `--growth`, or `--wide` for
the focused regressions. The unchanged Makefile discovers the new suite.
The regular release `build/tests/piece_mt_test` checks the active malloc guard.

Completed: GCC `make all`; Clang ASan/UBSan `make check` (29 binaries and replay
CLI); `make fuzz` (15 fuzzers); the requested piece fuzz run (182751 executions,
121 s reported, no findings). Run counts/time are (M)[AC], Full, load 12.85,
2026-10-09T13:25:17+08:00. An explicit GCC `PIECE_TESTING` bounds build also
passes. Ordinary release `piece_mt_test` reports zero malloc calls with its
active guard (M)[AC], Full, load 12.54, 2026-10-09T13:26:18+08:00.

Only the one before/after `piece_bench --quick` pair was run. `typing_1e4`
insert p50/p99: before 0.058/0.314 us (M)[AC], Charging, load 6.63,
2026-10-09T13:14:11+08:00; after 0.060/0.357 us (M)[AC], Full, load 4.58,
2026-10-09T13:28:45+08:00. Both are TRACK; batch median stayed 0.019 ms.
Full typing cells and stamps are in the decision. Quiet-box timing gates and
LeakSanitizer-enabled verification belong to the coordinator.

Environment limits: raster's live X11 check and x11_clip/live/stall tests
skipped on Xvfb connection/socket failures; no other modules were changed.
This sandbox used `detect_leaks=0`. No implementation work remains for this
bead's three findings; the separate findings and coordinator checks remain.
