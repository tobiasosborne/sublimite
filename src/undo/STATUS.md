# Undo status — P1.5d / edit-4w1.35 + edit-4w1.26

Findings 2–8 in `docs/reviews/P1.5-1.md` are implemented:

- One-slot insert admission / seven-slot capture scratch, with initialization-time
  virtual capacity for expansion of every admitted unknown record.
- Packed 64 B span storage; bounded relocation, tail-page decommit, explicit
  committed/retired/virtual accounting and combined piece+undo G10f checks.
- Constant-time redo detachment; bounded whole-group cap detachment and <=16
  reclaimed slots per key. `undo_maintain` amortises the remainder. Very large
  cap reductions may discard extra oldest closed groups to bound traversal.
- Sliced replay with piece-operation budgets, absolute deadlines, UNDO_MORE,
  separate operation/span counts and a borrowed pre-group snapshot. Legacy
  entry points still compile. Boundary cursor state is returned on completion.
- In-tree synthesised-kernel 10k undo gate: (G) p50 <=58 ms / p99 <=78 ms;
  real misses propagate to exit. Mock gate unchanged; label-only option removed.
- Transient/persistent undo+redo failure tests, batch state/dirty checks, physical
  admission/capture regressions, and small-capacity/failure/sliced replay fuzzing.

**Finding 1 is blocked.** The frozen piece API cannot guarantee allocation-free
rollback. See the exact **piece.h amendment proposal** in
`docs/decisions/P1.5d.md`. The atomicity test remains compiled and skipped by
normal runs; `UNDO_TEST_REQUIRE_ATOMIC_GROUP=1 ./build/tests/undo_test 1` opts
into its expected failure. A partial replay still changes the mutable tree and
locks edits/opposite replay; the borrowed snapshot supports safe rendering/saving
but is not rollback. Check deadlines between operations; a single costly piece
call or snapshot acquisition cannot be interrupted by this API.

Verify from this worktree (use only the supplied Xvfb :99):

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 \
  ./build/fuzz/undo_fuzz -max_total_time=300 -max_len=2048
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/bench/undo_bench
./build/bench/undo_bench --review-gate-check
./build/bench/undo_bench --review-label-check
```

LeakSanitizer is disabled in the sandbox; coordinator rechecks with leaks on.
The sandbox cannot connect to Xvfb's socket: the full sanitizer check needs
approved execution with that socket accessible. The retry against :99 passed
all 23 sanitizer binaries and the replay CLI checks. All release targets and
all 12 fuzzer targets built. See `docs/decisions/P1.5d.md` for final 300 s fuzz
and stamped shared-box TRACK benchmark evidence.

Final full fuzz: **(M)[AC] Charging, load1=3.28**, 521,407 executions / 301.03 s,
exit 0. An earlier attempt exposed a model-only explicit-group trimming mismatch;
the reduced input passes after correcting the model's deferred trimming.

One final benchmark: **(M)[AC] Charging, load1=3.74, TRACK**, real tree p50/p99
2.005664 / 2.089733 ms versus **(G)** 58 / 78 ms; mock 0.425297 / 0.447824 ms,
both rows pass and exit 0. Combined piece+committed-undo G10f checks pass;
100k-history cap reduction drains from 6,402,048 committed undo bytes to zero.
Coordinator owns quiet-box gate verdicts, integrated rendering and leaks-on QA.
