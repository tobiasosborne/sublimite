# Find status — P1.10 simd-filter-verify (edit-4w1.10)

Worker continuation completed 2026-10-09 in this variant worktree, on top of the
synthesised piece kernel. Implementation selection and gate acceptance remain
coordinator work. Design, complete numbers and RED/GREEN evidence:
[decision](../../docs/decisions/P1.10-simd-filter-verify.md).

## Done

- Rank-selected two-byte SIMD filter, SSE2 baseline and runtime AVX2 dispatch,
  bounded full-needle verification, deterministic Two-Way fallback when
  `verified_bytes > 4 * scanned_starts + 8 * needle_length`.
- One-byte exact counting with bounded offsets, fragmented-snapshot windows,
  long-needle reader Two-Way, reference regex engine with literal-prefix search.
- Combined polling meter, including failed comparisons, advances, AVX2 lookahead,
  preprocessing and snapshot copies. The inherited final boundary fix polls
  on `>= FIND_POLL_UNITS` and charges Two-Way retirement before match return.
- New deterministic regression exercises every comparison/retirement boundary
  for periodic/nonperiodic matches and right/left misses. Temporarily reverting
  each match-return fix produces RED; restoring the checkpoint produces GREEN.
  Production find sources are unchanged by this continuation.
- `make all`: exit 0. `make check`: 24 ASan/UBSan binaries and replay CLI passed
  on existing Xvfb :99 with socket access, `ASAN_OPTIONS=detect_leaks=0`.
  Both GCC find tests also passed; the frozen release test checks allocations.
- `make fuzz`: 12 fuzzers built, exit 0. Frozen `find_fuzz`: 1,380,831 runs in
  301 seconds, no crash or sanitizer finding, (M)[AC], Charging, launch load 3.45.
- Quick and full frozen matrices each ran once and passed all correctness
  checks, including cancellation and stale-publication suppression. Quick exit
  0; full exit 1 from ordinary reference timing misses. All measurements TRACK.

## TRACK numbers and remaining work

Full stamp: 2026-10-09 12:49:04 +08:00, Charging, (M)[AC], launch load 4.09;
observed mid-run load 7.86 at 13:27:31, Full, (M)[AC]. Full n=31, times in ms:

| Row | p50 / p99 (M)[AC], load 4.09 at launch | Reference p50 / p99 (G) |
|---|---:|---:|
| `G6_ERROR` | 193.871830 / 393.045078 | 80 / 125 |
| `G6_newline` | 110.269708 / 159.944659 | 80 / 125 |
| `G6v_a31b` | 143.954184 / 207.331407 | 160 / 250 |
| `G6v_first_last_middle` | 130.128522 / 143.445114 | 160 / 250 |
| `G6v_first_last_early` | 130.719536 / 213.778502 | 160 / 250 |
| `G6c_logical_TRACK` | 0.000189 / 0.000225 | 1 / 5 logical |

No functional item remains for this continuation. The ordinary timing misses
need quiet-box acceptance measurements and comparison with the other variant;
the loaded run does not settle their cause. The reference regex-without-prefix
row is slow and ungated; its complete numbers are in the decision. Worker return
is wall time, distinct from logical acknowledgement and the worker CPU-slice
bound. The coordinator must rerun with LeakSanitizer enabled.

## Verify

Use `DISPLAY=:99 EDIT_DISPLAY=:99` for every command. The sandbox cannot connect
to the live Xvfb socket; full `make check` needs access to the existing :99
socket. Leak detection is disabled below only for the documented sandbox
restriction; the coordinator should enable it.

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 \
  ./build/fuzz/find_fuzz -max_total_time=300 -max_len=4096
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/bench/find_bench --quick
DISPLAY=:99 EDIT_DISPLAY=:99 ./build/bench/find_bench
```

Before measurements, read `cat /sys/class/power_supply/BAT0/status` and
`cut -d' ' -f1 /proc/loadavg`; retain both stamps. Use the existing
`/tmp/edit-corpus`; the frozen benchmark creates its all-a fixture untimed.
Frozen headers, `tests/find_test.c`, `fuzz/find_fuzz.c`, `bench/find_bench.c`
and Makefile are unchanged. No build integration change is needed.
