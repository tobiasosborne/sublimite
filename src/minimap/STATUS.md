# P4.5 / P4.5b minimap status

Standalone density and navigation module; review §§5–9 addressed within the
permitted module scope. Decisions and red/green evidence:
[original contract](../../docs/decisions/P4.5.md) and
[review fixes](../../docs/decisions/P4.5b.md).

Done:
- Cache validates source identity; additive stable buffer binding handles
  adapter context reuse. Publication also checks buffer token and revision.
- Fill writes/marks only changed cells/rows. Actual backend submission tests
  prove cached typing fills retain typing-only damage and blink fills add none.
- Pending indexing shows an approximate scrollbar; additive hit_approx gives
  current byte targets without an index or source reads. Exact hit's stale
  rejection and all existing prototypes remain intact.
- Additive prepare/prepare_source, publish and fill_cached APIs support worker
  snapshot summaries and source-read-free UI fills. Protected sample pages,
  stale-publication checks, identity switches and allocation guards pass.
- Fuzzer covers prepared publication, edits, source switching and approximate
  navigation. Bench self-checks incomplete-index sidebar correctness and
  records foreground major faults after requesting sample-file eviction.

Outstanding (outside this bead's allowed edits):
- Frozen render_strip encodes full-width rows. Changed sidebar rows still
  submit text columns. Rectangle/column damage requires a render/backend bead;
  the proposal is in P4.5b §7. No frozen header was edited.
- Editor/work/view wiring must adopt cached fill, worker snapshot lifetime and
  mailbox publication, buffer binding for reused adapters, and byte scrolling
  for approximate targets. P4.5b §§5,8,9 specify the additive handoff. Legacy
  synchronous fill can still fault on nonresident bytes; existing large exact
  hits can scan an index chunk. Use cached fill and approximate drag on UI when
  residency cannot be guaranteed. Full integrated G3 cost is not measured here.

Verify with DISPLAY=:99 EDIT_DISPLAY=:99:
```
make all
ASAN_OPTIONS=detect_leaks=0 make check
make fuzz
ASAN_OPTIONS=detect_leaks=0 build/fuzz/minimap_fuzz -max_total_time=120 -max_len=2048 -timeout=10
```
Before the single final TRACK bench invocation, read BAT0/status and loadavg:
```
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
build/bench/minimap_bench
```

Final verification: make all exit=0; make check exit=0, 39 test binaries and
replay CLI passed (with access to Xvfb :99); make fuzz exit=0, 21 fuzzers built.
Minimap fuzz: Done 33244 runs in 121 second(s), exit=0, no sanitizer findings,
(M)[AC], Not charging, load1=4.52. Release allocation guards pass.
The one final minimap TRACK bench exited zero: (M)[AC], Not charging,
load1=7.04; p99 ns warm/after-edit including UI publication: small
148168/159695, log_1g 163587/168443, against (G) 500000 ns. Incomplete-index
sidebar/drag and reference checks pass; cold-sample verification records
foreground major faults=0 and source calls=0 with eviction requested.
Full stamped bench lines and each finding's red/green are in P4.5b.
LeakSanitizer is disabled in this sandbox; coordinator reruns with leaks on.
X11 tests require socket access to Xvfb :99 (sandbox-only make check cannot
connect; the required check is retried with X socket access).
