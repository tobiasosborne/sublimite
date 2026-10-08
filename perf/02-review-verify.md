**Verdict: No.** v2 repairs much of v1’s arithmetic, but several measurement contracts remain unsafe. All six files were read in full, v1/v2 were diffed, and every numerical check reported below was recomputed with Python. No files were modified.

“Fixed” below means the revision addresses the review point; it does not certify measured feasibility. Finding numbers **F1–F12** refer to the remaining findings below.

**Round-1 disposition — arithmetic review**

| Point | Original severity | Disposition |
|---|---|---|
| #1 Lower bounds versus estimates | BLOCKER | **Partially fixed:** §0 separates evidence correctly; the mandatory rendering conclusion still overextends a measured primitive (F2). |
| #2 Memcpy is not scan throughput | BLOCKER | **Partially fixed:** rejecting 6.3 for the measured AC SIMD kernel is justified; making 16 binding for battery release gates is premature (F1). |
| #3 Keystroke-chain means | MAJOR | **Fixed:** §2.1 gives A 27.0, B 68.3, and corrected bypass means. |
| #4 Compositor/scaler assumptions | BLOCKER | **Fixed:** these are scenarios; B-slow is separate; optical tails and idle scenarios are specified. |
| #5 On-glass startup stages | BLOCKER | **Partially fixed:** software/optical split and display stages added; software endpoint still includes work occurring after that endpoint (F4). |
| #6 Startup request counts/read budget | MAJOR | **Fixed:** ceilings, actual payload sizes and C’s 640 KiB footprint reproduce. |
| #7 Rendering measurement boundaries | BLOCKER | **Partially fixed:** timestamps separated; completion semantics still need backend-specific validation (F10). |
| #8 Clock/contention constraints | MAJOR | **Partially fixed:** crossover and contention added; first-burst extrapolation remains unjustified (F2). |
| #9 UTF-8/index contradiction | BLOCKER | **Fixed:** index is newline count plus ASCII flag; full validation is separate. |
| #10 Generic literal-find model | BLOCKER | **Partially fixed:** bounded output and linear verifier added; low match density does not bound verification cost (F8). |
| #11 Metadata cache/miss model | MAJOR | **Partially fixed:** keeping 371 ns pessimistically is justified; two misses/node still lacks a node-layout derivation (F12). |
| #12 Undo/tab omitted work | MAJOR | **Partially fixed:** tab includes minimap; undo still omits the newly included minimap term (F12). |
| #13 Edited-file memory cap | BLOCKER | **Fixed:** unedited metadata and retained edit/history terms are separated. |
| #14 Memory accounting | MAJOR | **Partially fixed:** mmap double counting and minimap omission repaired; Windows accounting introduces another double count (F11). |
| #15 Size-independent first text | MAJOR | **Partially fixed:** asynchronous bounded-prefix contract added; foreground limits and placeholder rules conflict (F9). |
| #16 Save workload/tails | MAJOR | **Partially fixed:** warm/cold source models added; joint flush tails and Windows durable completion remain unresolved (F6–F7). |
| #17 Native toolkit inference | MAJOR | **Fixed:** Python-GI inference withdrawn; native GTK measurement supports a scoped design choice. |
| #18 Historical calibration | MAJOR | **Fixed:** ratio becomes 1.10; history is illustrative and no longer derives k. |

**Round-1 disposition — systems review**

| Point | Original severity | Disposition |
|---|---|---|
| #1 Estimated floors | BLOCKER | **Partially fixed:** evidence taxonomy repaired; rendering necessity still exceeds its evidence (F2). |
| #2 Presentation pipelines | BLOCKER | **Partially fixed:** fixed-frame claim removed and fallback queues acknowledged; readiness instrumentation remains incomplete (F10). |
| #4 B monitor/scaler | BLOCKER | **Partially fixed:** acceptance envelope and threshold specified; exact SKU and acceptance measurement remain pending. |
| #5 Keyboard timing/start | MAJOR | **Fixed:** scan timings are explicitly unmeasured, descriptor verification is required, and starts/mean polling penalty are clarified. |
| #6 Scheduling/queue age | MAJOR | **Fixed:** G1 starts at ingress; queue age is traced; HZ is not a latency guarantee. |
| #7 First-burst/core placement | BLOCKER | **Partially fixed:** sustained CPU-fit guarantee withdrawn; actual battery raster and E-core behavior remain unmeasured (F2). |
| #8 GPU/display wake | MAJOR | **Partially fixed:** idle optical scenario added; wake estimates and borrowed clock multiplier are not measurements (F2). |
| #9 Raster/upload/scan primitives | MAJOR | **Partially fixed:** raster/upload correctly labelled proxies; SIMD measurement remains AC-only (F1–F2). |
| #10 Partial redraw/completion | MAJOR | **Partially fixed:** copy, request overhead and buffer-age repair included; readiness endpoints remain unverified (F10). |
| #11 Concurrent bandwidth | MAJOR | **Partially fixed:** one bulk worker and report-only combined completion times are appropriate; active concurrency/cancellation policy is incomplete (F10). |
| #12 Long lines | BLOCKER | **Partially fixed:** bounded mode and corpus added; byte cap exceeds the UI time slice and does not bound Unicode shaping (F9). |
| #13 On-glass startup | BLOCKER | **Partially fixed:** rejecting old enforced A startup caps is justified by measured WM mapping; revised software bookkeeping remains inconsistent (F4). |
| #14 First-use initialization/fonts | MAJOR | **Partially fixed:** initialization is deferred; cold-glyph placeholders conflict with correctness-gate endpoints (F9). |
| #15 Mapping/residency states | MAJOR | **Partially fixed:** three states named; “warm” background gates still need to specify populated mappings versus cached/new mappings (F10). |
| #16 NVMe power exit | MAJOR | **Partially fixed:** allowance added; policy is not a measured 100 ms I/O bound (F7). |
| #17 UTF-8 versus newline scan | BLOCKER | **Fixed:** separate validation, compute budget and boundary carry added. |
| #18 Search output/verification | BLOCKER | **Partially fixed:** output explosion repaired; verifier throughput and new dense-match budget remain deficient (F8). |
| #19 Mmap snapshots | BLOCKER | **Partially fixed:** problem acknowledged; copying after notification cannot recover overwritten original bytes (F5). |
| #20 Mmap residency/page tables | MAJOR | **Fixed:** unconditional B no-fit claim removed; residency and page tables separated. |
| #21 Complete insertion cost | MAJOR | **Partially fixed:** split scans, pools, lazy counts and chunked add storage added; mutation/path-copy costs remain unexplained (F12). |
| #22 Gap crossover | MAJOR | **Fixed:** explicit 0.5 ms movement budget; no universal file-size failure claim. |
| #23 Memory/history/minimap | BLOCKER | **Partially fixed:** numerical cap and minimap repaired; retained structural accounting and Windows metric remain incomplete (F11–F12). |
| #24 Save overlap/source/tails | MAJOR | **Partially fixed:** workload scenarios added; full-operation percentile model remains unsupported (F7). |
| #25 Windows replacement durability | BLOCKER | **Partially fixed:** unsupported flag withdrawn; acknowledged durability gap still prevents the advertised endpoint (F6). |

**Round-1 disposition — gates review**

| Point | Original severity | Disposition |
|---|---|---|
| #1 Dequeue hides queueing | BLOCKER | **Fixed:** ingress start, 0.5/2 ms limits and bounded foreground slices added. |
| #2 Optical tails/reference | BLOCKER | **Partially fixed:** absolute optical limits added; reference subtraction does not define a safe incremental statistic (F3). |
| #3 Support envelope | MAJOR | **Partially fixed:** C’s mandatory-release removal is **justifiably rejected** by making it provisional; B still awaits an instantiated fixture. |
| #4 Startup | BLOCKER | **Partially fixed:** rejecting enforced old A on-glass values is justified; revised endpoints need correction (F4, F10). |
| #5 Eager copy/first text | BLOCKER | **Partially fixed:** prefix published before bulk copy; long-line and correctness contracts still conflict (F9). |
| #6 Full-frame/cadence | MAJOR | **Partially fixed:** readiness and finite cadence gates added; actual backend completion and idle raster remain unvalidated (F2, F10). |
| #7 Scan rate | MAJOR | **Partially fixed:** relevant measurement obtained, but on AC; battery caveat does not reach the release table (F1). |
| #8 Complete editing workload | MAJOR | **Partially fixed:** bulk edits/undo/corpus added; highlighting exclusion is acceptable only with the stated shipped-feature gate requirement; IME/cold-glyph completion remains unspecified. |
| #9 Find/output/cancel | MAJOR | **Partially fixed:** bounded output and cancellation added; rejecting 65/90 as chosen tighter budgets is defensible, but claiming they necessarily assume MT is incorrect (F8). |
| #10 Memory metric/peaks | BLOCKER | **Partially fixed:** formulas and handoff model repaired; Windows accounting is incorrect (F11). |
| #11 Save gate/durability | MAJOR | **Partially fixed:** gates added; Windows durable endpoint remains unresolved (F6). |
| #12 Tabs/navigation/minimap | MAJOR | **Partially fixed:** cold tabs, 100-tab corpus and sidebar correctness added; navigation completion endpoint remains ambiguous (F10). |
| #13 Toolkit inference | MAJOR | **Fixed:** native GTK replaces Python-GI inference; no-toolkit is a chosen plan. |

No complete BLOCKER/MAJOR was silently ignored. Several subrequirements remain unfixed inside the partial dispositions.

**Changed-number recomputation**

Triplets below are A/B/C. Times are milliseconds unless stated. New gate constants are engineering choices; their implied multipliers were checked rather than treated as measured percentiles.

| Changed operation | Python recomputation | Result |
|---|---|---|
| Active optical mean; ×1.1 | 26.9972 / 68.3000 / 63.9000; 29.6969 / 75.1300 / 70.2900 | Correct. |
| No compositor; tearing scenarios | 15.8861 / 51.6333 / 47.2333; 10.3306 / 43.3000 / 38.9000 | Correct. |
| Mid-screen extrema | 17.7167–36.2778 / 48.0667–88.5333 / 45.4667–82.3333 | Correct. |
| Idle means | 38.7193 / 68.8740 / 64.5850 | B/C require applying A’s multiplier despite the added-term table saying “A” (F2). |
| Full frame including minimap | 4.595745 / 2.443886 / 2.920448 | Correct. |
| Half-frame slack; ×1.37 | 1.20885 / 3.40987 / 2.85344; 6.29617 / 3.34812 / 4.00101 | Arithmetic correct; transfer unjustified (F2). |
| Compute crossover; A at 400 MHz | 1.175 / 0.875 / 0.375 GHz; 8.68596 | Correct conditional arithmetic. |
| Warm software startup, published formula | 11.541915 / 17.369829 / 32.797568 | Sum correct; endpoint wrong (F4). |
| Cold 2 MB additions | 16 / 31 / 31 requests; 2.400 / 8.650 / 28.8333 | Correct. |
| C 640 KiB; revised cold startup | 9.369067; 13.941915 / 26.019829 / 42.166635 | Correct published formula. |
| Optical startup, 5 ms effects-off map | 39.3141 / 82.3698 / 92.7976; A effects-on 102.3141 | Reproduces; 5 ms remains an estimate. |
| Open warm; cold | 4.427519 / 2.396043 / 2.841259; 4.838129 / 3.100901 / 5.234475 | Reproduces with **zero explicit warm-open cost** and four cold request latencies. |
| ST scan 1 GB; 400 MB jump | 62.5 / 100 / 250; 25 / 40 / 100 | Correct at stipulated rates. |
| Cold 400 MB jump | 160 / 800 / 2666.667 | Correct; C’s 5000 gate is **1.875×**, not 2×. |
| Indexed lookup; complete split model | 10.774 / 9.2536 / 19.624 µs; 7.906 / 8.1536 / 18.284 µs | Reproduces the assumed miss model. |
| Gap movement at 0.5 ms | 4.7 / 3.5 / 1.5 MB | Correct. |
| Dense-find PB; twice PB | 2062.5 / 2100 / 5250; 4125 / 4200 / 10500 | A 4200 is conservative rounding, **not exact equality**. |
| Save 1 MB, two p50/p99 flush inputs | 5/41 / 22.5/102.5 / 60/220 | Arithmetic correct; not total-save quantiles (F7). |
| Save 1 GB warm/cold source | 1004/1404 / 2520/4520 / 20040/26706.667 | Correct conditional pipeline formulas. |
| Minimap cache; baseline; ×1.5 | 5.184 / 1.5552 / 1.10592 MB; 57.656 / 29.144 / 20.498624; 86.484 / 43.716 / 30.747936 | Correct; A’s 86 cap rounds downward. |
| Handoff peak | 78.392 / 37.4384 / 24.694976 MB | Correct for the declared single CPU buffer. |
| Unedited mmap 1/10 GB; edit fixture | 2.488288 / 6.882816 MB; 25.725 MB | Correct. |
| File capacity after published baseline caps | 1 MB: **6464 / 3231 / 792**; 100 MB: 67/33/8; mmap 1 GB: 3417/1708/419 | A’s 6463 corresponds to a **87 MB** baseline cap. |
| Fault-around/PTE illustration | 15,259 faults; 1.953128 MB leaf PTEs/GB | Correct under stated assumptions. |
| Foreground decode cap | 128 KiB × 5 ns/B = **0.65536**, not 0.64 | Exceeds the 0.5 ms slice (F9). |
| Concurrency traffic | 7.46496 + 3.73248 + 1.86624 = 13.06368 GB/s | Correct conditional accounting. |
| Historical ratio/midpoint; zero-miss confidence | 1.098214; 12.992857; 0.029953% | Correct revised rounding. |

**Remaining findings and new defects**

**F1 — MAJOR, NEW: AC throughput becomes a battery release input.**  
**Location:** §1, §2.5–2.7, §3, §5.1, G6/G7/G7j.  
**Claim:** 16 GB/s binds A’s release scan gates. **Counter:** the relevant measurement is charging/powersave. `[AC]` appears in the derivations but disappears from the gate table and parts of the architecture. Neither 125 ms/GB nor the edit-boundary scan terms have battery evidence. **Fix:** mark these gates provisional pending battery measurement, and rederive **all** scan-dependent terms. A conservative interim 6.3 GB/s planning input gives 158.73 ms/GB, approximately **200/320 ms p50/p99**, and **80/130 ms** for the 400 MB warm jump. This is an interim budget, not a claim that scalar throughput limits SIMD.

**F2 — MAJOR, NEW: the 1.37 multiplier is transferred across workloads and machines.**  
**Location:** §2.1–2.2, §3 rendering.  
**Claim:** measured first-burst behavior establishes 6.30 ms raster time and forces GPU or multithreaded raster. **Counter:** `2.19/1.60 = 1.36875` measures a **20 MB memcpy after 2 seconds idle on AC**. It does not measure blending, server upload, syscalls, B/C, or a 15-second battery-idle scenario. The measured first-burst payload rate is 9.132 GB/s; multiplying the separate battery 9.4 proxy is a new estimate. **Fix:** tag transferred penalties **(E)**, make rendering necessity conditional, and measure actual first-burst completion. The numeric multiplication itself is correct.

**F3 — BLOCKER, NEW: G2c can hide added-frame delays.**  
**Location:** G2c, §2.1, §4.  
**Claim:** subtracting reference latency cancels shared stages and bounds editor-added p99. **Counter:** subtraction of percentiles is not the percentile of added latency. Python counterexample: reference has 60% at 20 ms and 40% at 40 ms; add 11.111 ms to 5% of the fast inputs. Both reference and editor retain **p50=20, p99=40**; percentile subtraction gives **0/0**, while added-latency p99 is **11.111 ms**. **Fix:** define matched-phase/condition paired differences and gate their p50/p99 at **1/3 ms**. Interleaved, independently randomized samples do not establish those pairs. Apply the same statistical definition to G4c.

**F4 — MAJOR, NEW: G4a’s formula crosses its revised endpoint.**  
**Location:** §2.3, G4a.  
**Claim:** back-buffer completion costs 11.5/17.4/32.8 ms. **Counter:** those sums include server upload, although the endpoint is the CPU back buffer and presentation waits for Expose. Removing upload gives **9.33596/16.18491/31.39878 ms**. If upload really waits for Expose, including it imports A’s **57.8–67.5 ms** WM wait into the supposedly independent software gate. **Fix:** end G4a at completed CPU buffer plus map request; assign upload/readiness to a separate timestamp. Retain startup limits as chosen budgets, with corrected PBs.

**F5 — BLOCKER, unresolved: notification followed by copying is not a stable snapshot.**  
**Location:** §2.14; G6, G8s/G8d, G10f.  
**Claim:** freezing still-referenced ranges after external modification preserves the original. **Counter:** an overwritten byte is already lost before notification; copying captures changed bytes or a mixture of versions. A SIGBUS handler handles access failure, not restoration. Mmap semantics do not provide the promised frozen backing. [Linux mmap documentation](https://man7.org/linux/man-pages/man2/mmap.2.html).  
**Fix:** specify stable backing/versioning or explicit invalidation and cancellation of affected operations/history. The new full-file private-copy remedy also conflicts with large-file memory budgets; **238 ms/GB is a warm-copy estimate, not an upper bound**.

**F6 — BLOCKER, unresolved: Windows “durable” has no defined endpoint.**  
**Location:** §2.8, G8d.  
**Claim:** “saved” appears only at durable completion. **Counter:** the same section explicitly says the replacement recipe does not guarantee rename durability across power loss. Timing FlushFileBuffers plus rename acknowledgement therefore cannot certify the advertised gate. **Fix:** define and validate the supported filesystem/crash contract. Until then, label B’s existing values **data-flushed replacement acknowledgement**, with full durability explicitly provisional.

**F7 — MAJOR, NEW: primitive percentiles are being added as operation percentiles.**  
**Location:** §2.8; G4a/G5 APST annotations.  
**Claim:** two p99 flush inputs produce the 41/102.5/220 ms tail PBs. **Counter:** two individual p99 limits jointly provide only a **98%** union-bound guarantee. Python example: each flush has p99=20 ms, with disjoint 1% 100 ms tails; their sum has **p99=120 ms**, not 40 ms. Separately, `default_ps_max_latency_us` filters advertised power-state latency; it does not measure host I/O completion. [Linux 6.8 NVMe source](https://raw.githubusercontent.com/torvalds/linux/v6.8/drivers/nvme/host/core.c).  
**Fix:** measure complete save/read distributions, or use suitable joint-tail inputs. Keep the current caps as **(G)** budgets; describe +100 ms APST as an **(E) allowance**, not a measured upper bound.

**F8 — MAJOR, partly NEW: revised search scope still misidentifies binding work.**  
**Location:** §2.7, G6 adversarial, §6 rejection rationale.  
**Claim:** ≤1 match/KiB makes search bandwidth-bound; one-byte dense search needs a per-match 2 ns bill. **Counter:** repetitive absent patterns have zero matches but substantial verification work. Conversely, counting `a` in all-`a` input can use SIMD masks/popcount after collecting 4096 offsets; it need not enumerate a billion results. The first page is only **32,768 B**. The new dense p99 caps are **33.6× A’s** and **21× B/C’s** ordinary caps.  
**Fix:** separate periodic/near-match verifier benchmarks from dense one-byte counting. Use the ordinary bounded-count budgets for the latter, subject to measurement. Rejecting 65/90 ms is a slack choice: at 16 GB/s those are **1.04×/1.44× ST PB**, so they do not inherently require MT.

**F9 — MAJOR, NEW: foreground and correctness contracts contradict each other.**  
**Location:** §2.4, fonts, §3, §4 corpus.  
**Claim:** 128 KiB foreground work respects 0.5 ms slices; placeholders provide immediate response, while placeholders cannot pass gates. **Counter:** ASCII decoding alone is **0.65536 ms**. At the document’s 1 µs/character shaping example, 128 KiB is **131.072 ms**. Deferred glyph discovery also cannot count its tofu frame as the correct update. **Fix:** impose a wall-time slice with input checks; even the ASCII proxy permits only 100,000 B per slice, or conservatively **96 KiB = 0.49152 ms**. Distinguish immediate acknowledgement from correct-content completion; placeholders never stop G5 or optical completion clocks.

**F10 — MAJOR: the final table does not satisfy the requested gate contract.**  
**Location:** §0.2, §4, concurrency.  
**Claim:** every gate has p50/p99, a measurement endpoint and tagged binding input. **Counter/fix:**

| Audit | Failure | Concrete fix |
|---|---|---|
| Binding evidence | **None of the 23 rows carries a (P)/(M)/(E) binding-input tag.** Some indirect PB tags are wrong: find includes an unmeasured verifier; B/C lookup latency is estimated. | Add resource/evidence columns, including `[bat]/[AC]`, and the design consequence. |
| Percentiles | G6 adversarial lacks p50; G4b has “report” instead of a p99 limit. | Supply limits or move report-only rows outside the gate table. A/C dashes in B-only G4c are legitimate N/A. |
| Endpoints | G4c “first launch,” G7 completion, G7j completion and G9 “frame” are underspecified. | Define request→published index, request→correct submitted viewport, and precise startup/frame endpoints. |
| Readiness/cadence | Buffer reuse, GPU completion and compositor readiness are equated without validating each backend; app Present feedback alone does not define physical missed-refresh counting. | Establish backend-specific readiness and actual displayed-frame identities. |
| Warm state | Cached/new mapping versus populated mapping is not attached to background gates. | State the required mapping state and include faults where applicable. |
| Cancellation/concurrency | One bulk worker may block on cold I/O for the allowed 100 ms while cancellation requires 5 ms. “All running” lacks scheduling/progress semantics. | Use asynchronous I/O/control handling; specify time slicing and active-progress conditions. |
| Blink CPU | Monotonic wall timestamps do not measure app CPU time. | Specify CPU-running-time attribution and a quiet measurement interval. |

**F11 — MAJOR, NEW: Windows baseline memory double counts resident private pages.**  
**Location:** §4 memory, G10/G10f.  
**Claim:** measure “private commit plus working set.” **Counter:** working set includes resident private allocations already counted in private commit. A 20 MB resident private allocation becomes **40 MB** under that sum; mapped source residency also re-enters a metric that excludes it. [Private commit definition](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex), [working-set definition](https://learn.microsoft.com/en-us/windows/win32/memory/working-set).  
**Fix:** report commit and working set separately; gate a deduplicated owned-allocation total, including attributable shared/graphics allocations exactly once.

**F12 — MINOR arithmetic/precision; MAJOR metadata-model remainder.**  
**Location:** §2.6, §2.9, §2.12, units, changelog.

- **New minimap omission:** complete undo PB is **41.695745/17.443886/20.920448 ms**. Its 1.5×/2× values are **62.5436/83.3915**, **26.1658/34.8878**, **31.3807/41.8409**. Use approximately **63/84, 26/35, 31/42** if preserving the stated rule. A paste including split and minimap is approximately **4.77 ms**, rather than 4.58.
- **Exact versus chosen caps:** 1.5× A memory PB is **86.484 MB**. Either retain 86 explicitly as a stricter chosen cap, or use **87 MB**; the latter reproduces the listed 6463-file capacity.
- **Units remain inconsistent:** decimal 4 GB/32 is **125 MB = 119.209 MiB**; 128 MiB requires 4 **GiB**. Label installed RAM consistently with the binary budgets.
- **Metadata constraint remains missing:** sixteen pointer/byte/newline entries occupy **384 B = six cache lines**. Explain the claimed two dependent misses, mutation/count updates and retained persistent nodes; require those allocations in the piece/history accounting.
- Change the nonexistent §2.17 reference to §2.15; change “gates verified” to “must be verified.”

**Correct distinctions to retain**

- **16 GB/s is a conservative AC SSE2 scan input.** Restoring 6.3 as an optimized-scan hardware ceiling would be wrong.
- **57.8–67.5 ms is map→Expose with effects ON**, not app rendering time or an optical startup measurement.
- **Native GTK init at 56.4–60.5 ms** supports the chosen 25 ms software-startup budget decision; it does not prove a universal toolkit minimum.
- **9.4 GB/s remains valid battery memcpy payload throughput.** Do not add another automatic factor of two.
- **371 ns remains a legitimate pessimistic cold-metadata input**, provided the access model is labelled and justified.
- The revised UTF-8 split, bounded result storage, single-buffer handoff arithmetic, exact `T/2`, and finite zero-miss confidence calculation should remain.

**Final verdict:** **No, the v2 gate table is not yet safe to build against.** Keep G1 **0.5/2 ms**, the optical caps, and G3 **A 5/T÷2, B 4/5 ms**, but repair their statistics/readiness contracts. Make A’s AC-derived scan gates provisional; an interim conservative scan budget is **200/320 ms per GB** and **80/130 ms per 400 MB warm jump**, including dense one-byte counting rather than its 4200 ms allowance. Correct A undo to approximately **63/84 ms** and use **87 MB** if G10 truly preserves 1.5×. Keep G4a’s chosen limits with corrected PBs; move G4b’s report-only tail outside the ship-gate set, and withhold Windows full-durability certification. The **single most important remaining measurement** is A’s **on-battery first full redraw after ≥15 seconds idle, through actual upload/GPU readiness, under the defined bulk-worker pressure**: it decides whether the 5.556 ms deadline actually forces the proposed rendering architecture.