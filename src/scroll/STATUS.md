# Scroll — P3.4 (edit-zzj.4)

Implemented: caller-owned Q8 vertical pixel accumulation, three-row wheel
notches, deterministic reversal and clipping, page/document motion mapping,
resize, byte scrolling and bounded partial-index resolution, physical-anchor
publication correction and cursor following by row ordinal or cursor byte.
Core transitions are pure; the source/index adapter is explicit. No globals
or allocation calls. Editor, view, layout and frozen headers are untouched.

Rebase fix-up complete: cursor following now requires a proven line seed and
margin/EOF backtracking. It returns `SCROLL_MORE` with unchanged state when a
bounded scan cannot prove them; byte scrolling retains its coarse fallback.
The caller retains pending follow intent until publication or cancels it on
pure scrolling. This fixes an existing scroll implementation omission, not a
lineidx/work contract change. Every scroll-test failure destroys its index
and ends any active allocation guard.

Verification: `tests/scroll_test.c` includes active gcc allocation guards and
sanitizer-safe tests, unchanged-state/proof/margin regressions and publication
retry; `fuzz/scroll_fuzz.c` compares independent pixel/physical line models;
`bench/scroll_bench.c` measures an unbuilt-index warm deep jump and a null-backend
per-frame scroll-work proxy. See
`docs/decisions/P3.4.md` for exact event/frame calls and measurement evidence.

Integration dependencies: editor wiring is a separate follow-up. The frozen
renderer currently has no pixel-origin/overscan contract, so sub-row offsets
are represented exactly but cannot yet be displayed. Wrapped visual-row
scrolling needs the proposed layout adapter; current index integration is
wrap-off. Real displayed-frame G3z remains unmeasured. Very long unindexed
logical lines can retain a coarse earlier byte boundary after a bounded scan.

Verify (all use Xvfb :99; no real-display window):
read power and load immediately before each measurement/run. Leak checking
and Xvfb socket access require execution outside sandbox isolation here;
use `detect_leaks=0` for sandbox-only sanitizer runs.

```
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
env DISPLAY=:99 EDIT_DISPLAY=:99 make all
env DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/scroll_test
env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 make check
env DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/scroll_fuzz -max_total_time=120 -max_len=2048
env DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/scroll_bench --track
```

Benchmark comparisons are TRACK on the shared box. The coordinator owns the
gate verdict. A benchmark rerun is justified only by a relevant hot-path change,
per the repository policy; no closure rerun is needed for this fix-up.

Fix-up single-run TRACK results: (M)[AC], power Not charging, load1=12.91:
G7j p50=89.745047 ms / p99=145.540021 ms, misses the 30/50 ms (G) warm
comparison. G3z work proxy: 0 over-budget steps in 10,000, p50=35.014 us,
p99=77.587 us, maximum=195.957 us against 4.166667 ms (G). Real displayed
refresh count is still unmeasured. Correct deep target: 8,053,057 (G) on the
provided corpus; correct byte 966,366,840 (M)[AC], load1=12.91.

Fix-up fuzz: 17,519 runs / 121 s (M)[AC], power Not charging,
pre-run load1=13.17; ASan/UBSan clean. Both saved fuzz regression inputs
replay successfully. Release unit tests pass with active guards reporting
0 allocations (M)[AC], pre-run load1=15.13. `make all` exit 0;
`make fuzz`: 23 fuzzers built (M)[AC], pre-run load1=14.53.

Final-code `make check` exit 0: `check: 45 test binaries passed` and
`test_replay_cli: all passed`. No unrelated build/test failures remain.
ASan/UBSan/LSan were enabled outside sandbox isolation, power Not charging,
pre-run load1=13.99 (M)[AC]. Temporary assertion-failure probes in every
index-owning fixture also passed leak checking, load1=5.92 (M)[AC].
The tiny saved fuzz reproducers are in `build/scroll-repro-top.bin` and
`build/scroll-repro-follow.bin` for coordinator replay (generated artifacts).
