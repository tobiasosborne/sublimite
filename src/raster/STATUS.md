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
# raster P2.5b — edit-e6x.22 review fixes

Done (review sections 1, 2, 3, 5, 6, 7, 8, 15, 17):
- Authenticate strip/fence work slot, epoch, generation and strip index; explicit
  completion flags prevent duplicate uploads even for zero-duration jobs.
- CPU-only diagnostic casts and RGB888 TrueColor visual/depth validation.
- Cancellation-aware reliable essential publication through work mailboxes.
- Per-frame glyph descriptor ceiling (4096); single atlas validation on submit,
  frozen all-cell validation retained; guarded maximum-table typing regression.
- Worker cancellation between <=256-pixel tiles, independent of cell dimensions;
  exact cached/SSE2 output and fuzzed interruption/store bounds.
- Immutable fence-job inputs through work_submit; retained issued phase on
  enqueue failure, no private armed atomic or repeated Present requests.
- Explicit row-local kernel destination contract and independent placement test.
- Separate T6 observation timestamp, preserving the adapter's T4 clamp.
- Appended deterministic regression fixtures; existing comparison/allocation
  sections and separately owned benchmark left unchanged.

Missing / coordinator work:
- Section 4 is VALID and NOT fixed: current rollback can block UI behind queued
  cancelled jobs. work_submit_batch needs an atomic reserve/enqueue primitive in
  src/work; proposal and explicit expected-RED probe in docs/decisions/P2.5b.md.
  A free-slot precheck or returning with retained jobs is not a safe workaround.
- Layout integration must bind compact per-frame tables within the new ceiling;
  larger incremental tables/all-cell validation require a new frozen-contract
  bead. Quiet-hardware G3/G3z acceptance and cap calibration remain external.
- P2.5c owns review sections 9–14, 16, 18 and bench changes; not handled here.

Verify from the worktree, always DISPLAY=:99 EDIT_DISPLAY=:99:
- make all
- ASAN_OPTIONS=detect_leaks=0 make check (coordinator reruns with leaks enabled)
- make fuzz
- ASAN_OPTIONS=detect_leaks=0 build/fuzz/raster_fuzz -max_total_time=120
  -max_len=4096 -rss_limit_mb=512 -print_final_stats=1
- build/tests/raster_test --review all (fixed findings)
- build/tests/raster_test --review 4 (intentional RED for the work proposal)
- build/tests/raster_test --live-only (short release live lifecycle/allocations)
- Once at the end, with battery/load stamp: --review budget-track and
  build/bench/raster_bench --track --quick --idle 0; loaded Xvfb is TRACK only.

Evidence: docs/decisions/P2.5b.md contains per-finding RED/GREEN and final results.
Verification complete for the implemented scope:
- make all passes (gcc warnings as errors); make check: 29 test binaries and
  replay CLI passed (clang ASan/UBSan, leaks disabled). Final strengthened review
  cases also pass in release and sanitizers. Short release live test passes.
- make fuzz: 16 fuzzers built; requested raster run 120 s, actual 121 s, 59,067
  inputs, no mismatch/sanitizer findings (M)[AC], start load 6.46.
- Descriptor cap self-check once: Target-A 43,200 cells / 4,096 glyphs, p50
  102,183 ns / p99 181,090 ns, 0 guarded allocations (M)[AC], load 2.67, TRACK.
- Module bench ONCE (--track --quick --idle 0), exit 0; (M)[AC], start load
  2.46, after load 10.40. Full rows p50/p99: 15 px 7,232,431/11,425,929 ns;
  30 px 6,708,355/14,526,699 ns. No acceptance/gate claim; minimap excluded.
- No production edits after bench; only stronger appended test synchronization/
  duplicate checks, verified in release and sanitizers. No bench rerun.

Section 4 remains an explicit unresolved MAJOR, not a green or closed finding.
