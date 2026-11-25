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
