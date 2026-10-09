# P4.14 / edit-457.15 — startup experiment

Done: normal CPU exec, warm GL-preferred/CPU-fallback server, warm CPU control;
common benchmark protocol; first-original-frame hooks; strict round-robin real
CLI exec and src/ipc wait lifecycle; map/unmap and cleanup checks; raw power/load
stamps, startup percentiles, executable sizes, page-fault proxy, idle RSS/PSS,
all-thread idle CPU ticks/context switches; release and sanitizer verification.

Decision: [P4.14](../../docs/decisions/P4.14.md) recommends **needs a real-display
run**, with no adoption or winner.patch. All three points remain on the strict
latency/residency Pareto front. Native GL is unsupported on Xvfb; B's valid rows
are explicitly CPU fallback. CPU warming helps under measured shared-box load,
but consumes permanent residency and does not justify GL-specific complexity.

Verify from repository root, with approved local X11/IPC socket access:

```
env DISPLAY=:99 EDIT_DISPLAY=:99 make all
env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
env DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
tools/zygote_bench.sh --self-check
env ASAN_OPTIONS=detect_leaks=0 CC=clang ZYGOTE_SAN=1 tools/zygote_bench.sh --self-check
python3 variants/P4.14/audit.py
```

`tools/zygote_bench.sh --run` rebuilds and measures all candidates in one
interleaved pass. Do not rerun it to seek better load. It forces :99 and unsets
real-display opt-in. Native real-display experiments require coordinator
announcement to Tobias and a separately authorized display-aware runner,
complete CLI workload, separate native CPU-ready/device-done endpoints and
validation (current validation is CPU-specific), graphics accounting,
GPU power/wake and vblank/on-glass evidence; see decision doc.

Final: GCC all passed; ASan/UBSan check passed all tests and replay CLI checks;
fuzz built all fuzzers; IPC decoder smoke passed without findings. Exact
red/green lines, counts and stamps: verification.txt. Valid measured rows:
results.log. Independent raw audit: audit.py / audit.txt. The invalid initial
first-frame attempt is retained and excluded explicitly. No leaks verdict.

Missing by design: native GL measurements, real GPU/vblank/power behavior,
production service/IPC file+tab+journal lifecycle, exact G10 ownership and G11
focused blink percentiles, root-only cold-cache rows. This is a standalone
variant module; production editor/ipc APIs and STATUS files remain accurate
and unchanged. No implementation work is being moved into src in this bead.
