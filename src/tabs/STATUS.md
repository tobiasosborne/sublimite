# tabs — edit-457.4 / P4.4

Standalone implementation complete. Public API: tabs.h. Integration into the
editor loop is deferred by coordinator decision; no editor/main/frozen/build
files changed. Design and exact UI-thread integration calls are in
../../docs/decisions/P4.4.md.

Implemented: stable caller-owned tab-set control block; init-reserved base arena;
borrowed piece/undo handles and persistent view state; copied title/path and
modified status; open, select, close, index-move reorder, bounded closed retention
and reopen; frozen MRU traversal while Ctrl is held and promotion on release;
resident-glyph strip rendering, Unicode cluster width/truncation, active colors,
wide halves, row damage, and matching hit geometry. Title segmentation/widths
are cached at open/rename, so switching does no title layout, allocation, file I/O
or buffer queries. The caller owns input routing, close/save policy, atlas warming,
viewport layout, full-frame marking, frame submission, and resource retirement.

Verification commands (safe display only):

```sh
export DISPLAY=:99 EDIT_DISPLAY=:99
make all
./build/tests/tabs_test
ASAN_OPTIONS=detect_leaks=0 make check
make fuzz
ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/tabs_fuzz /tmp/tabs-fuzz-corpus \
  -max_total_time=300 -max_len=4096 -timeout=10 -print_final_stats=1
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
./build/bench/tabs_bench
```

Release and module ASan/UBSan tests pass. Independent model sequences, renderer
contract checks, capacity/errors/eviction, name aliasing, closed real buffer/undo
retention, and allocation guards pass. Release guard reports zero mallocs over
switch+strip+MRU work and over close/reopen/reorder work. ASan's allocator guard is
inert as documented by base; the release test enforces it. Final `make all` exits
successfully; `make fuzz` reports `fuzz: 16 fuzzers built`. Leak detection is off
for sandbox sanitizers; coordinator must rerun with leaks enabled.

Post-cache benchmark: (M)[AC], Full, load1=10.91. Warm select+strip elapsed ns,
including scheduling delays, without dropping samples:

- viewport strip: p50=2,506, p99=5,039; module p99 <=500,000 ns (G): PASS/TRACK.
- all-visible strip: p50=57,164, p99=72,933; same gate: PASS/TRACK.
- tab-set own bytes=557,184, including page-rounded storage and closed reserve;
  caller undo bytes=426,400; combined excluding buffers=983,584, <=1,000,000 (G):
  PASS/TRACK. Fixture has 100 small-file tabs and closed capacity 16.

Initial uncached code missed p99 on the shared box; exact before/after evidence
and the caching change are recorded in P4.4.md. No repeated quiet-seeking run.
These numbers are TRACK; full-frame G3 and app-total G10 need integrated quiet-box
coordinator measurement. Buffer memory retained by the closed stack must be
budgeted by its caller independently.

Timed libFuzzer run: (M)[AC], Full, load1=12.05, 63,917 runs in 301 seconds,
zero crashes, model failures, or ASan/UBSan findings. Operations are checked after
every step; arbitrary byte titles/windows are also validated against frozen
render_grid invariants. Fuzzer handles are opaque borrowed tokens (never
inspected by the module); real resources are exercised by the unit suite.

Repository-wide `make bench` exits 2 at layout, before reaching tabs:
(M)[AC], Full, pre-run load1=12.62. layout_ascii_code.c_360x300 p50/p99
198,262/395,932 ns and layout_log_1g.txt_360x300 395,005/758,097 ns exceed
150,000/150,000 ns (G). Those unrelated results were reported and layout was
not changed. The dedicated tabs bench exits 0. Quiet-box gate adjudication,
integrated full-frame timing, full-app peak memory, and leak-enabled sanitizer
verification remain coordinator work, outside this standalone bead.

Final sanitizer suite: `check: 29 test binaries passed` and
`test_replay_cli: all passed`; `make check` exit 0 with ASAN_OPTIONS=detect_leaks=0.
The final tabs sanitizer output includes `closed buffer, undo history and view
retention passed` and `tabs_test: all passed`. No module work remains in this
bead. Integration and coordinator gate/leak verification are explicitly deferred.
