# raster — P2.5c review harness fixes (edit-e6x.23)

Backend/kernel history: docs/decisions/P2.5.md. Current harness decisions and
red/green evidence: docs/decisions/P2.5c.md. Backend implementation review work
belongs to P2.5b; this bead changes no raster/render implementation or header.

Done in P2.5c (review §§9–14, 16, 18):
- Benchmark ingress precedes mutation, frame begin, damage and strip construction.
  Minimap-free rows explicitly remain subsets/TRACK, never a G3 acceptance PASS.
- Cadence primes a baseline and counts required intervals; drops, invalid MSC,
  errors and incomplete gate runs fail. Quick scroll names disclose their count.
- A/B dimensions and exact refresh-derived budget limits; idle threshold failures
  propagate and shorter than 15 s idle samples are explicitly TRACK.
- Idle/index scan/find scan/save copy/all-queued work-pool fixtures with real
  progress/queue checks; explicit synthetic TRACK labels for memory/queue stress.
- Gate mode returns 2 for missing/unmeasured fixtures; 1 for errors/budget misses.
  Xvfb has no real vblank: G3z explicitly SKIP/not measured.
- Every individual partial edit compared with full scalar window contents;
  disconnected rows, partition boundaries and unchanged rows covered. Readback
  failure is a failure. Dropped partial-upload negative control fails.
- External frozen conformance suite again asserts zero allocations over exactly
  10,000 input-through-submit windows, ending each guard before present/completion.
  Allocation injection at frame 1000 fails. Short mapped live tests retained.
- Bench --self-check independently covers verdicts, timing order, targets,
  contention fixtures, short idle and exact T/2 boundary values.

Verified on DISPLAY=:99 EDIT_DISPLAY=:99:
- make all: exit 0, GCC C11 with required warning flags.
- make check: 29 ASan/UBSan binaries and replay CLI checks passed; leaks disabled
  with ASAN_OPTIONS=detect_leaks=0 (coordinator must verify leaks).
- Release raster conformance: 10,000 guard windows, 0 allocations, active guard;
  full plus every partial pixel comparison passed.
- GCC and Clang ASan/UBSan bench --self-check: all sections PASS.
- make fuzz: 16 fuzzers built. Raster fuzz budget 120 s, completed 374,360 runs
  in 121 s, no finding. Start Full (M)[AC], load1=3.97; no corpus regeneration.
- One final --track --quick --idle 1 --target all --kernel-samples 20 bench:
  exit 0; all A/B proxy/atlas/contention rows completed. Start Full (M)[AC],
  load1=7.31; per-row stamps and complete warm/idle rows in P2.5c.md.
  No gate claim from shared-load Xvfb.

Missing acceptance / proposals (P2.5c decision sections name the owner boundary):
- Integrate actual minimap work/content through T5 before claiming G3/G3i.
- Integrate real index/find/save jobs (including actual save I/O), and use the
  actual B Windows/display fixture; X11 B-resolution rows are proxies only.
- Expose verified vblank source/rate via backend diagnostics before enabling
  G3z acceptance; current public diagnostics cannot attest it.
- Expose deterministic fence/Present transport for the exhaustive allocation
  fixture. Current 10,000-submit check is honest but still waits on real X
  completions; no fake acknowledgements or null-for-CPU substitution is used.

Verify:
  DISPLAY=:99 EDIT_DISPLAY=:99 make all
  DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
  DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
  build/bench/raster_bench --self-check
  DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/raster_test
Negative controls (release, expected nonzero): EDIT_RASTER_ALLOC_AT_1000=1 with
raster_test, and EDIT_RASTER_DROP_PARTIAL=1 with raster_test --live-only.
Do not repeat the performance bench on this loaded host; the single TRACK run
and its stamps are recorded in P2.5c.md.
