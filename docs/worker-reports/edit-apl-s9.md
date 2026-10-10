# edit-apl session 9 — production raster ownership

Status: **incomplete; P4-3 section 9 remains RED for production defaults.** The census and measured capacity option are done. No production default fix is applied: the defaults reside in src/editor/open.c, outside the bead's named source scope. Authorization to extend the source scope to a defaults-only open.c change was requested; no answer was received before completing this report. The file remains untouched. No G10 acceptance or bead closure is claimed.

## Scope and rules

Read CLAUDE.md, the supplied bead, P4-3 section 9, P2-1 section 10, docs/worker-reports/edit-5o0-s9.md, docs/decisions/edit-5o0.md, perf/01-perf-target.md sections 0.2 and 2.12, and raster/editor STATUS.md. Product source is unchanged. The only code edit is tests/raster_test.c; the remaining deliverables are this report and docs/decisions/edit-apl.md. Git was read-only; no bd, HANDOFF.md or docs/worklog edits, and no new globals. All native actions use DISPLAY=:99 and EDIT_DISPLAY=:99, never :0.

Power checks: AC online=1, BAT0=Not charging (M)[AC]. The box is shared; observed load1 ranged from 1.02 to 5.08 (M)[AC] during verification/benchmark startup. Native tests required sandbox escalation because the sandbox cannot connect to the existing Xvfb :99 Unix socket. Automatic approval review allowed these executions. LeakSanitizer is disabled with ASAN_OPTIONS=detect_leaks=0 because it cannot run inside the sandbox; the coordinator must rerun with leaks on.

## Finding 9 — done and missing

Done: replaced the previous explicit G10 diagnostic with a production-default target-A census, retaining its P2-1-10-gate alias and adding P4-3-9-gate. Only visible geometry/font are supplied; maximum cell capacities retain production defaults. Both direct raster startup and forced failed-EGL-to-raster startup are covered. The census runs in release and ASan builds, obtains native pixmap geometry through XCB, checks actual backend capacities/state, and counts snapshot arrays, strips, embedded worker palettes/job buffers, SHM and independently allocated native pixmap payload once each. No worker owns an additional framebuffer. ASan adds all startup heap and the editor mmap arena reservation to a separate combined lower bound; backend state is already inside that arena and is not added twice.

The required raster-share derivation: A's 87,000,000-byte cap (G), less the planned non-surface baseline of 16,228,000 bytes (E; 57,700,000 − 2×20,736,000), less one 20,736,000-byte handoff-buffer reserve (E), gives 50,036,000 bytes (G, derived). The review itself does not prescribe an explicit numeric sub-cap. This conservative necessary subsystem check preserves the planned other ownership; it is not a new binding gate or full G10 certification. Native pixmap payload is an estimate (E), not a server/driver allocator measurement.

Missing: the actual default-policy fix and its production GREEN run. The diagnostic remains explicit and excluded from the ordinary correctness suite while RED; a green make check must not be read as memory acceptance. Backend connection overhead, thread stacks, driver/native metadata and transient/handoff peaks still need complete peak ownership evidence. Startup heap covers XCB in the separate combined lower bound, but the backend-only payload omits XCB heap.

## Pasted RED run

Final-source command, exit 1 (M)[AC]:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 sh tools/with_display_lock.sh build/san/tests/raster_test --review P4-3-9-gate
```

```text
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
raster_test:935: FAIL !miss
P4-3 section 9: production-defaults default-raster target=2880x1800 snapshot=2180320 state_workers=266064 SHM=51840000 (M)[AC] native_pixmap_payload=51840000 (E) backend_owned_lower_bound=106126384 (E) raster_share=50036000 (G,derived) RED
P4-3 section 9: editor_arena=6751696 (M)[AC] (includes backend state; do not add it twice); backend payload excludes XCB/stacks/driver/transient peaks
P4-3 section 9: startup_heap=3468839 (M)[AC] deduplicated_startup_lower_bound=113900535 (E) G10=87000000 (G) RED
P4-3 section 9: production-defaults failed-EGL-to-raster target=2880x1800 snapshot=2180320 state_workers=266064 SHM=51840000 (M)[AC] native_pixmap_payload=51840000 (E) backend_owned_lower_bound=106126384 (E) raster_share=50036000 (G,derived) RED
P4-3 section 9: editor_arena=6753072 (M)[AC] (includes backend state; do not add it twice); backend payload excludes XCB/stacks/driver/transient peaks
P4-3 section 9: startup_heap=3474376 (M)[AC] deduplicated_startup_lower_bound=113907448 (E) G10=87000000 (G) RED
```

This is a memory assertion failure after both successful opens, not an initialization/transport failure. An earlier sandbox-only attempt could not connect to :99 and was discarded as memory evidence.

## Pasted GREEN capacity-option control — not a production fix

The following explicit override tests the concrete option max_cols=360, max_rows=120 (G geometry). Production defaults are untouched. It is labeled explicit-capacity-option in every payload row and must not be substituted for a production GREEN run.

Command, exit 0 (M)[AC]:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 sh tools/with_display_lock.sh build/san/tests/raster_test --review P4-3-9-capacity-option
```

```text
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
P4-3 section 9: explicit-capacity-option default-raster target=2880x1800 snapshot=873520 state_workers=266064 SHM=20736000 (M)[AC] native_pixmap_payload=20736000 (E) backend_owned_lower_bound=42611584 (E) raster_share=50036000 (G,derived) GREEN
P4-3 section 9: editor_arena=4119304 (M)[AC] (includes backend state; do not add it twice); backend payload excludes XCB/stacks/driver/transient peaks
P4-3 section 9: startup_heap=2162039 (M)[AC] deduplicated_startup_lower_bound=47753343 (E) G10=87000000 (G) GREEN
P4-3 section 9: explicit-capacity-option failed-EGL-to-raster target=2880x1800 snapshot=873520 state_workers=266064 SHM=20736000 (M)[AC] native_pixmap_payload=20736000 (E) backend_owned_lower_bound=42611584 (E) raster_share=50036000 (G,derived) GREEN
P4-3 section 9: editor_arena=4120680 (M)[AC] (includes backend state; do not add it twice); backend payload excludes XCB/stacks/driver/transient peaks
P4-3 section 9: startup_heap=2167576 (M)[AC] deduplicated_startup_lower_bound=47760256 (E) G10=87000000 (G) GREEN
```

The capacity control and final production RED were measured consecutively on the same shared AC box. The backend lower bounds differ by 63,514,800 bytes (E). A below-cap lower bound does not prove full peak ownership fits G10.

## Build, correctness and fuzz verification

Final-source GCC 13.3 make all exits 0 (M)[AC], with C11 -Wall -Wextra -Werror -Wshadow -Wconversion. Final-source tests/raster_test.c also builds under clang 18.1.3 ASan/UBSan with the same warning flags. Sanitizer raster conformance passed native pixel, retirement and upload checks; its counting allocator is inactive under ASan and is not a release allocation-guard claim. git diff --check exits 0 (M)[AC]. Product raster code is unchanged; no typing-path allocation was introduced.

Final-source full make check exits 0 (M)[AC], with DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0, including the shared display lock. The final-source raster test was built before this suite reached it. Leaks remain disabled. Actual green correctness summary (this does not include the explicit RED memory diagnostic):

```text
check: 63 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
raster law2: windows=10000 allocations=0 guard=0 scope=input->submit
render no-malloc: ASan guard inactive; release run required
raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)
render no-malloc: ASan guard inactive; release run required
```

The nested display-lock contract deliberately emits timeout/nonzero subprocess output and passes after verifying it; that expected negative-control output is not a failed outer make check.

Raster fuzz: built the module target, then ran unchanged production raster with:

```sh
ASAN_OPTIONS=detect_leaks=0 build/fuzz/raster_fuzz -max_total_time=60 -max_len=4096 -rss_limit_mb=512 -print_final_stats=1
```

Exit 0 (M)[AC], 61 seconds (M)[AC] against the requested 60 seconds (G), no sanitizer/fuzzer finding. Start load1=3.53 (M)[AC]. Actual tail:

```text
Done 65223 runs in 61 second(s)
stat::number_of_executed_units: 65223
stat::average_exec_per_sec:     1069
stat::new_units_added:          683
stat::slowest_unit_time_sec:    0
stat::peak_rss_mb:              255
```

## G1 comparison and out-of-scope result

The unchanged editor benchmark uses target-A geometry, its production injection-to-T4 endpoint, and the log_1g fixture in /tmp/edit-corpus. No G1/G11 benchmark source, count policy or endpoint was changed. Initial baseline command:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_BACKEND=raster sh tools/with_display_lock.sh build/bench/editor_bench --track --no-idle --no-p4 --serial-only --keys=1000
```

Exit 1 (M)[AC], due to the pre-existing structural allocation count, not a gate verdict from one timing. Relevant pasted rows:

```text
STAMP (M)[AC] BAT0=Not charging load1=3.92 shared box
STAMP (M)[AC] BAT0=Not charging load1=3.92 shared box
BENCH name=editor_raster_serial_injection_T4_G1 n=1000 required_n=10000 p50=16783503 p99=33205283 ci95_p50=[16737079,16827254] ci95_p99=[33067619,33455646] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=3.92 verdict=TRACK
G1 raster workload=serial keys=1000 completed=1000 arrivals_active=1000 coalesced=0 allocations=41 mutations=1000 journal=1000 structural=FAIL mode=TRACK (M)[AC]
```

This allocation guard failure is out of scope and is not fixed. Xvfb and abbreviated TRACK counts do not certify G1. The short archived-baseline/final-executable comparison ran sequentially under one shared display lock, both with --track --no-idle --no-p4 --serial-only --keys=128. Runs took 3.654/3.712 seconds (M)[AC] in the same minute; production sources are identical. This is an unchanged-code control, not evidence for a default fix. Both fail structurally; timings are descriptive TRACK only, never a single-sample gate pass/fail.

```text
before (unchanged production sources), exit 1 (M)[AC]
BENCH name=editor_raster_serial_injection_T4_G1 n=128 required_n=10000 p50=19805055 p99=37960997 ci95_p50=[19451658,21054962] ci95_p99=[36907238,38126538] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=1.02 verdict=TRACK
G1 raster workload=serial keys=128 completed=128 arrivals_active=128 coalesced=0 allocations=3 mutations=128 journal=128 structural=FAIL mode=TRACK (M)[AC]
after (unchanged production sources), exit 1 (M)[AC]
BENCH name=editor_raster_serial_injection_T4_G1 n=128 required_n=10000 p50=19531867 p99=37568218 ci95_p50=[19075567,20523064] ci95_p99=[37427946,37954143] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[AC] power=Not charging load1=1.10 verdict=TRACK
G1 raster workload=serial keys=128 completed=128 arrivals_active=128 coalesced=0 allocations=2 mutations=128 journal=128 structural=FAIL mode=TRACK (M)[AC]
```

G11 is unchanged and no new G11 gate claim is made.

## Decisions and options for the coordinator/Tobias

The full rationale, concrete minimal defaults patch, and measured costs are in docs/decisions/edit-apl.md. The simplest production-font fix lowers default max_rows from 300 to 120 while preserving explicit maxima and larger initial-window admission; a stronger font-aware policy derives both default cell maxima from target-A pixel geometry after choosing the font. Both require the excluded open.c file. A display/work-area policy additionally needs the editor configuration/resize boundary. A raster-only silent clamp would violate advertised init-reserved capacity or break supported resize, so none is made. Sharing a pixmap alone leaves the old 51,840,000-byte framebuffer (E) plus snapshots/state above this 50,036,000-byte share (G). Worker growth/retirement requires an explicit design change to init-only allocation and peak ownership.

Required acceptance still missing: actual production default fix, production GREEN census, full peak G10 proof (including handoff), and coordinator LeakSanitizer rerun. No gate exemption is granted. Stop after this bead; do not treat the explicit-capacity GREEN control as closure.

Completed this report within the worker wall-clock budget. No additional scope was added; no production fix is claimed. Generated logs and archived baseline executable are retained under build/edit-apl (ignored build artifacts).
