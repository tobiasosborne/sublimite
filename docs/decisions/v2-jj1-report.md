# v2-jj1 implementation report

The common service exposes the nine signed functions and the null factory. It removes the render smoke export, gives the L3 isolated link test a real render binding, and routes the downstream viewport smoke directly to the existing edit smoke. The [decision record](v2-jj1.md) contains the invariants, counterexamples and seven advisor questions.

## Red and green receipts

| Acceptance command | Red against the stub | Final lane |
|---|---|---|
| `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189` | Exit 2 compiling the new render test. The stub has neither the render service types nor the API. | Green, exit 0. Export gate, 31 lane fixtures, 6 isolated layer links, 34 sanitizer suites, 10 Law 2 suites and 64 contract records passed. |
| `nice make -j2 check-tsan` | Exit 2 compiling the same missing API under clang TSan. | Green, exit 0. All 14 threaded suites passed. |
| `nice make -j2 fuzz-smoke` | Exit 2 compiling the render sequence model against the stub. The shared render types and null factory were absent. | Green, exit 0. All 17 fuzzers replayed their seeds. |

The release render fixture and render export preflight passed before the final lanes. The first full check found a lane-registration error. The empty Law 2 fixture rewrites trailing law2 registrations. A dedicated law2_render_test keeps that fixture valid and opens the live guard around a full tile/region submit and a resident row submit. The independent sequence model checks retained footprints, coverage, queued obligations, refusal, loss and replay against null. The exact stub-red logs are under `build/jj1/`.

## Benchmark

Measured (M) with GCC 13.3 release flags on AC, with load1 measured at the start of each case. The signed comparison is the 500 µs UI slice (G). The signed spec cites 360×120 and 360×300, with one extra source row, 10×20 cell pixels, and the donor's 10,000 warm samples. Full-sheet measures direct sheet identity checks. Full-runtime also performs a live indexed metadata lookup per cell. Largest is the fixture's admission, with 108,360 cells and 2,167,200 bytes from 20 bytes per cell.

Times are p50/p99 in µs. Whole operation is submit through synthetic null retirement. The memcpy column provides the same-cell-byte comparison. Tile-only cases isolate R-A on retained cells that reference sheet slots.

| Geometry | Fixture | Load1 | Submit | Whole | Memcpy or R-A scan |
|---|---|---:|---:|---:|---:|
| 360×120 + overscan | One row, 7,200 B | 22.75 | 1.790 / 1.959 | 3.718 / 4.030 | 0.417 / 0.592 copy |
| 360×120 + overscan | Full sheet | 22.75 | 389.254 / 9,859.624 | 1,041.080 / 20,963.118 | 181.053 / 6,035.730 copy |
| 360×120 + overscan | Full runtime | 24.81 | 819.510 / 25,686.392 | 1,533.646 / 33,525.309 | 196.735 / 7,230.142 copy |
| 360×120 + overscan | One 400 B tile | 27.57 | 0.292 / 1.178 | 38.631 / 163.697 | 37.698 / 134.581 scan |
| 360×120 + overscan | Three 400 B tiles | 27.57 | 0.338 / 0.902 | 31.973 / 72.717 | 31.054 / 68.575 scan |
| 360×300 + overscan | One row, 7,200 B | 27.57 | 1.969 / 2.488 | 3.841 / 5.108 | 0.179 / 0.347 copy |
| 360×300 + overscan | Full sheet | 27.57 | 591.583 / 9,604.505 | 1,857.207 / 21,829.736 | 386.734 / 4,318.766 copy |
| 360×300 + overscan | Full runtime | 24.34 | 1,988.722 / 25,993.270 | 3,692.576 / 44,927.000 | 482.092 / 9,546.363 copy |
| 360×300 + overscan | One 400 B tile | 25.72 | 0.345 / 1.555 | 81.238 / 569.553 | 80.084 / 566.513 scan |
| 360×300 + overscan | Three 400 B tiles | 24.94 | 0.472 / 2.071 | 103.322 / 4,663.444 | 101.918 / 4,659.359 scan |

The one-row p99 fits the signed slice. The largest full-grid p50 and p99 exceed it for both identity sources. At that geometry the difference of the full-sheet submit and memcpy medians was 204.849 µs, and the analogous full-runtime difference was 1,506.630 µs. Scheduler and memory contention are part of these wall observations. A tile-free frame recorded zero R-A visits and zero scan time. With tiles, each scan counted exactly 43,560 or 108,360 cells, independent of batch size.

The after-idle run took one observation after 2 s per profile, matching the idle duration of the signed spec's memcpy source in the oracle hardware profile. On AC the ordinary row was 4.392 µs at load1 24.94, largest full-sheet 4,536.911 µs at load1 25.07, and largest full-runtime 1,493.674 µs at load1 25.07. The full warm and idle logs are under `build/jj1/`. These timings precede the final short-overscan padding and suspended-bind refinements. The timed offers use a complete extra row and a drawable binding throughout. Battery timing requires a battery run.

## Source counts

Static measured inventory, stripping blank lines and C comments from production C and headers. Helpers shared across common prep and the driver count with prep. The signed columns are the contract's estimates.

| Production group | Actual | Signed estimate |
|---|---:|---:|
| Shared values, public service and driver declarations | 232 | 140 |
| Shared prep and bounded geometry/storage helpers | 347 | 210 |
| Runtime delivery and recovery | 98 | 80 |
| Common service, admission and retirement | 477 | 230 |
| Null driver | 201 | 50 (null and selection) |
| Total | 1,355 | 710 |

The prior render stubs contained 11 comment-free lines. The total exceeds the estimate by 645 production lines. The major increases are explicit setup and retirement transitions, per-field validation diagnostics, and the null driver's retained data, sampled-coverage and damage oracle. Diff against the initial checkout is +2,708 / −28 text lines for code, test and build integration, plus 80 lines in the two bead documents (+2,788 total) and the 22-byte binary fuzz input. The export preflight reports every render export reserved against the signed contract. Existing allowances for piece, undo, file and journal remain outside the render gate.

## Outside this bead

Native factories, platform owner/poll wiring, physical waits, header adaptation when plat_native lands, end-to-end layout/font adoption, G1 and the guarded ingress pipeline, hardware upload comparison, and X11/Wayland pixel and display parity require their respective integration beads. The null fixture measures software submit and retention only. Worklog and handoff coordination remain with the parent.

## Round 2 receipts

The Round 1 results above are historical. The revised invariants and interface choices are in the [decision record](v2-jj1.md). Round 2 baseline behavioral, native ABI, diagnostic ABI and driver-header probes are recorded under `build/round2/`.

| Review item | Red at the Round 1 baseline | Green before final lanes |
|---|---|---|
| Owner handoff | UI PUMP invoked consume and poll. | Submit leaves the command in the owner slot and the retained scene untouched. An OS thread takes, binds, consumes and polls it before UI routes the evidence. |
| Staged bind | Refusing the first full after bind lost the old accepted ID and geometry. | A bad sheet or stale first full preserves old pixels, watermark and association. An active patch can still run before the staged full. |
| Idle loss | Matching epoch with frame zero returned STALE between frames. | A live epoch disconnect clears delivery credit after quiescence. Per-flight loss from a retired frame is stale. Full plus capped replay restores earlier ink. |
| Wake | Setup accepted NULL and reported WAIT without a wake. | NULL setup wake refuses. Open and physical retirement WAIT expose the supplied live wake. |
| Native type | Compile-time signed-type assertion failed. | Mutable `plat_native_binding *` type assertion passed. |
| Packed offsets | Driver header had no packing description. | Header describes run-relative offsets, trailing overscan and RGB disposition. A driver-side probe checks the cells at the boundaries of disjoint runs and short overscan. |
| Reject stats | A probe referencing rejection telemetry failed to compile. | A malformed tile after a copied prefix reports INVALID/TILE without spending credit. A stale replacement reports STALE/STAMP while retaining the accepted association. |
| Trace ID | First accepted frame restarted at 1 after rebind. | The first frame of the new epoch follows the last old frame number. The existing EDTRACE frame field stays unique within the service. |
| Release full-frame cost | Stamp read twice, and runtime identity read once per changed ink cell in release. | One stamp read. Release validates structure and accepts a stale in-range image as a coverage mismatch. Sanitizer and explicit identity-assert builds reject that image by consulting the source. |
| RV-2 replay | Recovery advanced in PUMP outside submit. | An accepted input-free offer advances at most its grant. Candidate refusal leaves queue and cursor unchanged. |
| RV-5 physical release | RELEASED carried physical completion as a notice. | Both terminal axes can wait in RETIRING. A WAIT from quiesce preserves the packet. Loss and close also wait for its return. |
| Q4 sheet | Two marker slots could stand in for printable ASCII. | Admission requires the ordered printable ASCII inventory and marker identities for both spans. |
| Q5 tile at byte cap | Invalid page length refused even when valid dimensions priced the head out of the grant. | Only declared extents are used for the non-fitting head. A selected head with malformed page or any head with invalid required extents refuses. |

Release render, ASan/UBSan render, TSan render, live-guard Law 2 and render seed replay all passed in targeted runs. Export checks passed and the production archive has no observer entry points. The supported owner interface retains one packet array description and one common retained scene. Null only installs R8 bytes and produces typed synthetic evidence. Ink observers compute one sample on demand. Borrowed test view, scan count and stopwatch live in the hook variant.

### Mutation witnesses

All 16 Round 2 adaptations of Astra's mutations compiled and were killed by test assertions. The two previously surviving mutations are **bypassing sheet validation** and **ignoring the right-half ink extent**. A marker version inconsistent with its sheet returns INVALID and preserves the active binding. A double-span tile with a one-cell ink extent leaves right-half coverage zero and retains its attributes and offset.

The remaining killed mutations were spending a frame on refusal, ignoring the sole live stamp, sampling by slot, skipping a one-tile R-A scan, running R-A on zero tiles, overwriting mismatch background, leaving STOP quiescence on WAIT, synthesizing times on loss, short watermark, marking only new-version users, crediting a partial copied prefix, ignoring physical retirement WAIT, scanning more than the replay grant, and accepting an invalid cell row. The first assertions and exit statuses are in `build/round2/mutants.log`.

### Production source inventory

Same comment/blank stripping as Round 1. The hook-only test header and conditional probe code are excluded. Shared scene logic counts with common service. Production sanitizer and explicit identity-assert validation is included.

| Function group | Round 1 | Round 2 | Signed estimate |
|---|---:|---:|---:|
| Public values, service and driver | 232 | 259 | 140 |
| Prep and checked geometry/storage helpers | 347 | 294 | 210 |
| Runtime delivery and replay | 98 | 64 | 80 |
| Common service and owner-side scene | 477 | 535 | 230 |
| Null and selection | 201 | 73 | 50 |
| **Total** | **1,355** | **1,225** | **710** |

The three structural changes reduce the inventory by 130 production lines. The remaining gap is 515 lines. The common-service figure includes 98 lines of shared owner-side scene code that the prior inventory counted primarily with null. The transport port, shared scene view, staged sheet copy and physical-wait path account for additional work in the public/common groups. The diff versus Round 1 adds 1,246 and removes 1,130 text lines for code, build and tests, including hooks. Render-directory changes alone are +806 / −853 text lines. Including the decision and report edits, the diff is +1,329 / −1,148.

### Quiet-box benchmark

**Non-probative.** Acceptance lanes were idle for the quiet-window wait and the benchmark. Fresh load1 readings over five minutes were 18.15, 17.48, 21.40, 24.04 and 41.29. The last pre-run reading from /proc/loadavg was 46.61 at 22:58:26 UTC. The box stayed above the required load1 of 2. AC online was 1 and BAT0 reported Not charging.

Each profile has 10,000 warm samples, following the signed donor fixture. Values below are p50/p99 µs. One row copies 7,200 B. The largest full fixture copies 2,167,200 B for 108,360 cells. Thread CPU brackets submit and wall time separately includes scheduling. Whole includes the synthetic null owner operation through retirement.

| Fixture | Load1 start → end | Submit CPU | Submit wall | Whole wall | memcpy wall |
|---|---:|---:|---:|---:|---:|
| 360×120 + overscan, one row | 46.61 → 46.61 | 2.104 / 2.560 | 2.535 / 3.124 | 3.330 / 4.076 | 0.291 / 0.545 |
| 360×120 + overscan, full sheet | 46.61 → 63.24 | 186.643 / 252.256 | 188.801 / 30,288.364 | 285.425 / 32,419.687 | 88.599 / 216.623 |
| 360×120 + overscan, full runtime | 63.24 → 56.66 | 353.208 / 452.835 | 360.783 / 26,432.617 | 522.613 / 26,613.960 | 147.568 / 4,259.800 |
| 360×300 + overscan, one row | 56.66 → 56.66 | 4.236 / 5.759 | 5.104 / 7.881 | 6.663 / 10.920 | 0.599 / 1.325 |
| 360×300 + overscan, full sheet | 56.66 → 42.54 | 901.268 / 1,058.260 | 917.032 / 8,964.581 | 1,391.503 / 10,490.309 | 486.964 / 5,617.704 |
| 360×300 + overscan, full runtime | 42.54 → 38.12 | 731.146 / 995.028 | 747.055 / 6,987.614 | 1,123.637 / 8,564.862 | 388.991 / 3,582.522 |

RV-1's 500 µs quiet-box thread-CPU determination remains outstanding. These timings establish neither the quiet-box result nor the RV-1 full-frame exception. All figures and the pre-run load receipt are under `build/round2/`.

### Final Round 2 lanes

| Command | Result |
|---|---|
| `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189` | Exit 2 at check-contract. The export gate, 31 lane fixtures, 6 isolated layer links, 34 sanitizer suites and 10 Law 2 suites passed. At load1 25.45 the harness had 62 PASS records, one 3 s Xvfb startup timeout and one 1 s shutdown timeout. An isolated check-contract follow-up at load1 23.72 produced 63 PASS records and repeated the shutdown timeout. Receipts are final-check.log and contract-followup.log under build/round2. |
| `nice make -j2 check-tsan` | Green, exit 0. All 14 threaded suites passed. |
| `nice make -j2 fuzz-smoke` | Green, exit 0. All 17 fuzzers replayed their seeds. |

## Round 3 receipts

| Item | Red | Green before final lanes |
|---|---|---|
| F1, stock owner | The unwrapped factory accepted a full but produced no UI wake in Astra's 100 ms observation. | The isolated L3 production link's `public_null_progress` routes the real owner's typed feedback through the UI wake, retires a full and then accepts and retires the next patch. |
| F2, physical STOP | Taking a packet and parking its owner let close return OK with the arena reset. Epoch loss also left QUIESCING while the owner held the packet. | `close_with_taken_packet` and `loss_with_taken_packet` park the real null owner between take and consume. Close or rebind reports WAIT with the UI wake and retains the arena mark. After release the wake is readable, no stale evidence is delivered, close frees storage and loss permits a full in a fresh epoch. |
| F3, implementor headers | Round 2 omitted the uncorrelated timestamp encoding, actual XShm fence distinction, Wayland pacing, WAIT resumption, cancellation and watchdog rules. | Header probes find those rules alongside the transport and operation contract. Lifecycle verifies that uncorrelated PRESENTED refuses nonzero ns and accepts zero. The stats comment now identifies the most recent submit refusal. |

Release, ASan/UBSan and TSan render targets, the isolated L3 test, the live allocation guard and render seed replay passed before the final lanes. The guard uses the stock owner. The benchmark was adapted to the public wake interface and built. Round 3 takes no timing measurement.

| Astra survivor | Named test | Mutant result |
|---|---|---|
| 18, UI poll during submit | `backend_poll_stays_off_submit` | Failed on a poll before owner dispatch. |
| 22, skip the wide-right reserved field | `wide_right_reserved_refuses` | Failed on accepting the invalid pair. |
| 25, replay during an input-bearing submit | `input_bearing_submit_skips_replay` | Failed on a directory read, cursor advance or queued tile. |
| 26, omit the owner wake | `command_owner_wake_is_signalled` | Failed on an unreadable command wake after acceptance. |

The four mutant binaries independently compiled and failed at their named assertions against both the baseline probes and the revised sources. Both pristine suites passed. The red and green receipts and the baseline snapshot are under `build/round3/`.

The Round 3 diff adds 497 and removes 96 text lines for code, build and tests. Including decision and report edits it adds 536 and removes 96. The production size review and quiet-box timing determination remain with the advisor.

### Final Round 3 lanes

| Command | Result |
|---|---|
| `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189` | Passed, exit 0. Export gate, 31 lane fixtures, 6 isolated layer links, 34 sanitizer suites, 10 Law 2 suites and all 64 contract harness records passed. |
| `nice make -j2 check-tsan` | Passed, exit 0. All 14 threaded suites passed. |
| `nice make -j2 fuzz-smoke` | Passed, exit 0. All 17 fuzzers replayed their seeds. |
