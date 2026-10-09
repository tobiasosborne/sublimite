# minimap — P4.5 / edit-457.5

Standalone implementation complete. Public API is minimap.h: caller-owned
state/storage, init/fini, density fill, stale query, click/drag hit and inverse
line-to-strip-row mapping. No allocation/global state/worker/blocking index
calls in fill. Only bead-scoped files were changed; no editor/lineidx/header/
Makefile changes. Integration is deferred by coordinator decision.

Small-file density is exact under the documented clipped non-space-byte model.
Large-file density uses <=256 representative nominal chunk-boundary samples,
shared across adjacent rows, with <=256 bytes per sample. It is approximate;
actual lineidx chunk starts/counts are not public. Warm fills reuse summaries;
edits or stale events force fresh summaries. Viewport/hits retain full row
resolution. Pending-index fills never read source; hits reject stale state.

Tests cover independent density/reference computation, fragmented spans,
viewport colours, dirty rows, exact large-index hits, click/drag round trips,
same-size edits, cached fills, sample cap, resize invalidation, argument/source
errors and release malloc-guard checks. Fuzzer generates line-length vectors,
whitespace, fragmentation, dimensions and stale/refill events. Benchmark gates
both warm and after-edit fills and checks stale/refill reference correctness.

Verification commands (safe display):

```
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/minimap_test
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/minimap_bench
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/minimap_fuzz -max_total_time=300 -max_len=2048
```

Before measurements: cat /sys/class/power_supply/BAT0/status and
cut -d' ' -f1 /proc/loadavg. Do not regenerate corpus.

Final policy benchmark returned zero; all (G) p99<=0.5 ms rows passed in a
shared-box TRACK run (M)[AC], Full, load1=12.67: small warm 0.085377 ms,
small after edit 0.131373 ms, log_1g warm 0.088010 ms, log_1g after edit
0.189800 ms. Both stale/reference checks passed. Quiet-box gate verdict is
pending coordinator verification. See ../../docs/decisions/P4.5.md for design,
proposed index accessor and the exact UI-thread integration contract.

Final-tree verification: make all exit=0; make check exit=0 (29 test binaries
passed plus replay CLI); make fuzz exit=0 (16 fuzzers built). Release and
ASan/UBSan minimap tests pass. Final libFuzzer run: (M)[AC], Full, load1=9.04,
19823 runs in 301 seconds, exit=0, no sanitizer findings; detect_leaks=0.
Release fill guards report zero allocations. Both warm and regenerated fills
are gated by the standalone benchmark, which returned zero.
Editor-loop integration/combined G3 full-frame measurement is a follow-up bead.
Coordinator must re-run leak detection outside this sandbox. Existing raster live-X11, x11_clip, x11_live and x11_stall scenarios skipped
because this sandbox could not connect/create Xvfb sockets. This is outside
this module's scope; remaining headless and XI2 checks passed.
