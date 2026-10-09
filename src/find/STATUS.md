# Find status — P1.10b best-of fold-in (edit-4w1.52)

The P1.10 pick is the SIMD filter/verify kernel. Its uncovered losing-variant
tests and verifier edges are folded in by this bead. Comparison, every port,
design argument and red-green evidence:
[P1.10b decision](../../docs/decisions/P1.10b.md). Historical pick and design:
[P1.10](../../docs/decisions/P1.10.md),
[SIMD filter/verify](../../docs/decisions/P1.10-simd-filter-verify.md).

## Done

- `tests/find_bestof_test.c`: every uncovered Two-Way-only test family,
  exhaustive binary critical-factorisation cases, periodic powers/perturbations,
  all byte values and count boundaries, every random single/pair snapshot cut,
  tiny spans, long needles, arbitrary streaming offsets, oversized-needle
  cancellation, adversarial/dense subjects, and regex prefix failures/retries.
- Deterministic probes cover cancellation at every actual poll of representative
  count/next paths, preprocessing, stitch-copy checkpoints, long snapshot
  matching/misses, and periodic memory. Existing short Two-Way comparison and
  retirement boundary tests remain green.
- Fixed the picked long snapshot verifier's repeated tree seeks: independent
  forward suffix/prefix readers, ascending prefix comparisons, metered span
  advances, constant stack space, no allocation. The regex reader remains
  rewindable for anchors and overlapping prefix retries.
- Traversal regression RED: seeks/span fetches 4608/4608 (M)[AC], `Not charging`,
  launch load 11.96, versus three seeks/3072 fetches (G fixture bounds).
  GREEN: 2/768 (M)[AC], `Not charging`, release launch load 27.71.
- Additive long-snapshot fuzz operation with an independent KMP oracle; the
  original small-subject literal/regex operations are retained. Needles and
  spans exercise the repaired reader, count/next and precancel clearing.
- GCC `make all` and all release find suites passed. Clang `make fuzz` built
  all fuzzers; full ASan/UBSan `make check` and replay CLI passed on Xvfb :99,
  with `ASAN_OPTIONS=detect_leaks=0`. Result lines are in the decision record.
- `find_fuzz`: 21541 executions in 301 seconds (M)[AC], `Not charging`, launch
  load 27.00, exit 0, no mismatch or ASan/UBSan finding.
- The find quick matrix ran once, all correctness/cancellation/publication
  checks passed, exit 0, (M)[AC], `Not charging`, launch load 27.71, TRACK only.
  Existing `/tmp/edit-corpus` fixtures were reused unchanged.

## Remaining / limits

No functional find item remains for this fold-in. The coordinator owns full
performance gate verdicts and the LeakSanitizer-enabled rerun. The sandboxed
check cannot connect to Xvfb; use access to the existing safe display socket.
The frozen restarted NFA's no-prefix regex path remains slow and ungated, as
recorded in the original design. No new regex linearity claim is made.

`src/find/find.h` and `tests/find_test.c` match their frozen hashes. No Makefile,
picked-suite, losing-worktree, or unrelated-module edits are needed.

## Verify

All commands use the existing Xvfb display; never enable the real-display bypass.
Leak detection is disabled below only for this sandbox; the coordinator enables
it for the final leak check.

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/find_bestof_test
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/find_simd_filter_verify_test
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/find_test
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 \
  ./build/fuzz/find_fuzz -max_total_time=300 -max_len=4096 -print_final_stats=1
```

The benchmark command is `DISPLAY=:99 EDIT_DISPLAY=:99
./build/bench/find_bench --quick`; check power/load first, stamp them, and follow
the one-run benchmark policy. The best-of test contains a counted production
source copy for deterministic probes and checks normal linked entry points
against its independent models as well.
