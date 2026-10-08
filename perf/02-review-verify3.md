**No—the supplied gate table still has measurement-contract defects.** The file identifies itself as **v3.2**, not v3.1, and the hardware profile now has three addenda. This review covers v2 → the supplied v3.2, including the historical v3.1 claims. A complete v3.1 snapshot was not supplied. All four files were read in full; every arithmetic result below was recomputed with Python. No files were modified.

**Round-2 dispositions**

| Finding | Disposition |
|---|---|
| F1 | **Fixed:** AC-derived scan gates are provisional; rejecting a scalar-derived SIMD ceiling is justified. |
| F2 | **Partial:** actual raster measurements replace memcpy, but bottleneck attribution and architecture necessity still exceed the evidence. |
| F3 | **Fixed:** gates use percentiles of paired differences, with matched refresh phase. |
| F4 | **Fixed:** G4a excludes upload and Expose waits. |
| F5 | **Fixed:** copying after notification no longer promises preservation; invalidation/cancellation replaces that promise. The detection race remains explicit. |
| F6 | **Fixed:** B’s endpoint is data-flushed replacement acknowledgement; power-loss durability is provisional. |
| F7 | **Fixed:** composite tails are chosen whole-operation budgets; APST is an estimated allowance. |
| F8 | **Partial:** dense counting and verifier workloads are separated; the Rabin–Karp alternative lacks the promised worst-case guarantee. |
| F9 | **Fixed:** wall-time slices govern; placeholders cannot stop correctness clocks. |
| F10 | **Partial:** table structure improved, but readiness, cancellation, CPU-time instrumentation and several row contracts remain incomplete. |
| F11 | **Fixed:** Windows commit and working set are reported separately. |
| F12 | **Partial:** memory rounding and metadata accounting improved; minimap arithmetic and propagation contain new errors. |

**Changed-number verification**

Triplets are A/B/C; times are milliseconds. Chosen gate constants are budgets, not measured quantiles.

| Operation | Python recomputation | Assessment |
|---|---|---|
| Active optical mean; ×1.1 | **27.9372 / 69.0200 / 64.3000**; **30.7309 / 75.9220 / 70.7300** | Supports G2a p50 **31/76/71**. |
| Active mid-screen extrema | **18.6567–37.2178 / 48.7867–89.2533 / 45.8667–82.7333** | Correct revised extrema. |
| No compositor / no vsync wait | **16.8261/11.2706**, **52.3533/44.0200**, **47.6333/39.3000** | Published values retain the old app costs. |
| Naive raster: cycles/pixel at assumed 4 GHz | `0.0131×4e9/(2880×1800)` = **10.1080** | Correct conditional estimate. |
| Naive idle factors | `33.9/13.1` = **2.5878**; `6.3/1.4` = **4.5** | Correct **2.6×/4.5×**. Raster alone is **2.358×/6.102× T/2** warm/idle. |
| SSE2 idle factors, A | ST **3.4477×**, MT4 **1.7276×**, upload **4.5×** | Correct replacements. |
| 1080p measured idle factors | ST **5.45×**, MT4 **3.1593×**, upload **8×** | These differ materially from A-panel ratios. |
| A CPU totals, minimap scaled by corresponding raster ns/pixel | ST **7.7158/28.0750**; MT4 **4.7800/12.1392** warm/idle; MT8 warm **4.8233** | ST totals correct; MT minimap additions understated. |
| B-resolution CPU totals on A hardware | ST **2.7250/16.3813**; MT4 **1.8006/8.5931** | MT4 idle rounds to **8.6**, not 8.5. |
| G1 app costs | Listed A warm items sum to **0.510**; chosen PB **0.6** is conservative. Idle estimate `0.4×20.1/5.83+0.15` = **1.5291**, giving **1.308×** p99 slack | Arithmetic works; idle strip transfer remains **(E)**. |
| Idle optical mean, using stated strip-only transfer | **42.7174 / 71.9201 / 66.6651** | A/B reproduce; C’s published **67.4** does not. |
| Startup warm/cold | **12.520/14.920**, **18.570/27.220**, **33.500/42.8691** | Correct. A warm p50 multiplier is **1.9968**, approximately 2—not “≥2”. |
| Historical v3 startup after removing upload | Warm **9.3360/16.1849/31.3988**; cold **11.7360/24.8349/40.7679** | Changelog reproduces. Naive v3.1 first-frame startup gives A **21.63**, B **20.20**; its complete intermediate contract cannot be reconstructed. |
| Open, using current chosen frame costs and stated read additions | Warm **0.8450/1.8268/2.8437**; cold **1.2713/2.5579/5.2806** | Before repairing A’s missing GPU minimap; current cold rounding is low. |
| Paste/undo | A **1.1862/38.1094** only if an unstated **0.18** GPU minimap term is restored; B **2.0516/16.8006**; C **3.4016/20.8000** | A’s operations use inconsistent frame definitions. |
| Ordinary scan; verifier budgets | Scan PB **62.5/100/250**; G6v limits are exactly twice ordinary gate limits | Dense p99 **4200/4200/10500** correctly removed. A ordinary p50 is **1.28×**, not exactly 1.25×. |
| Newly mapped, cached-file scan | Add stated fault model **15.259** per GB → **77.759/115.259/265.259** | Gates may remain chosen budgets; PB and k should include the newly specified mapping state. |
| C cold jump | PB **2666.667**; **5300/PB = 1.9875** | Acceptable chosen rounding; exact 2× is **5333.333**. |
| Memory | Baseline **57.656/29.144/20.498624 MB**; ×1.5 **86.484/43.716/30.747936** → caps **87/44/31** | Correct. Handoff **78.392/37.4384/24.694976 MB** and listed capacities reproduce. |
| Metadata/slices/I/O | Piece model **72 B**; path-copy allowance **1920 B/edit**; 96 KiB decode **0.49152**; 500-char shaping **0.5**; chunks **0.41943/2.09715/3.49525** | Correct conditional arithmetic; transfer times do not bound I/O tails. |

Other numeric gate changes reproduce as **chosen budgets**: G1 p50 **0.5→1**; G2b p50 **45/75/70→47/80/75**; new G3i **8/T, 10/T, 10/T**; B startup **38/55, 55/80**, C **67/90, 86/120**; A undo **63/84**; A memory **87 MB**. The startup, save and cancellation limits cannot be inferred as percentiles from primitive point measurements.

**Remaining findings**

**R1 — BLOCKER: G3 does not measure the same completion boundary across backends.**  
**Location:** §2.2 backend table; §4 T5; G3/G3i.  
**Claim:** DXGI becomes compositor-ready when `Present` returns, with later display confirmation.  
**Counter:** return from submission and completion of GPU work are separate events. Later confirmation that a frame displayed supplies T6; it does not establish readiness at the earlier return timestamp. Microsoft provides an explicit GPU-completion query for that purpose. [D3D11 completion semantics](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query).  
**Fix:** define T5 as **matching-frame render completion plus presentation submission**, with correlated clocks and frame IDs. Keep T6 separate. Give GDI an equally explicit completion timestamp instead of “BitBlt returned plus the DWM frame ID.” Validate accelerated XShm readiness separately from source-buffer reusability.

**R2 — BLOCKER: Windows G11 specifies cycles for a millisecond gate.**  
**Location:** G11; §4 idle CPU.  
**Claim:** `QueryThreadCycleTime` measures CPU time for **0.1/0.2 ms** limits.  
**Counter:** it returns cycles; Microsoft explicitly cautions against conversion to elapsed time because timer behavior varies. [QueryThreadCycleTime](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime). A quiet-window total also does not itself supply a per-blink p99.  
**Fix:** use CPU-running-time measurements in time units, with validated resolution and per-blink attribution across every participating app thread. `GetThreadTimes` supplies user/kernel durations, though its effective resolution must be checked; scheduler tracing can provide finer attribution. [GetThreadTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadtimes). Retain the numerical limits.

**R3 — MAJOR: minimap costs are understated and inconsistently propagated.**  
**Location:** §2.2, §2.4, §2.6, §2.9–2.11; PB table.  
**Claim:** A MT4 adds **0.1/0.2** minimap warm/idle; GPU open costs **0.85**, paste **1.19**, undo **38.1**.  
**Counter:** the stated ns/pixel rule gives A MT4 minimap **0.260/0.4492**, hence **4.780/12.1392** total. Warm slack is **1.162×**, not approximately 1.2 with the implied larger margin. B MT4 minimap is **0.0706/0.2231**. A paste and undo imply an extra **0.18**, while open omits it.  
**Fix:** define one complete GPU-frame PB and use it everywhere. An explicitly chosen **0.18 (E)** GPU minimap allowance would produce frame **1.00944**, open **1.02504/1.45126**, paste **1.18623**, undo **38.10944**. Alternatively measure it and substitute the result. Derived minimap/composite costs must be **(E)**, not presented as directly measured totals.

**R4 — MAJOR: bottleneck attribution and “GPU REQUIRED regardless of threading” overreach.**  
**Location:** §2.2; §3 rendering; G3/G3i binding columns.  
**Claim:** equal SSE2 coverage timings prove memory/overhead binding; **6.3 ms** upload makes CPU miss G3i regardless of threading.  
**Counter:** similar 25%/100% timings also occur when a branchless kernel executes the same instructions for every pixel. No bandwidth or execution-counter evidence establishes the bottleneck. For G3i, upload leaves **11.111−6.3 = 4.811 ms** for raster plus minimap. Measured MT4 exceeds that; **MT8 after idle was not measured**. Also B-resolution ST **16.381 ms** fits the **16.667 ms p99** cap, although it exceeds the **10 ms p50** budget.  
**Fix:** state “GPU is the chosen A path; measured MT4 XShm fallback fails the idle budget.” Label bottleneck attribution **(E)** pending counters. Restrict failure claims to tested paths. These AC results support a design decision, not universal CPU impossibility.

**R5 — MAJOR: C’s extrapolation is arithmetically inconsistent; several optical figures are stale.**  
**Location:** §2.1 C strip/idle model; §2.2 C column; §0.1.  
**Claim:** C strip **0.19** follows A ns/pixel ×4; idle mean **67.4** follows strip-only ramping.  
**Counter:** `0.40×1366/2880×4 = 0.758889`, not 0.19. With the currently published **0.19** strip and **0.31** fixed app overhead, the idle mean is **66.665**, maximum **85.098**, not **67.4/85.9**. The latter mean reproduces only by multiplying the **entire 0.5 app stage** by 3.45. C MT2 totals lack explicit raster/upload/minimap inputs.  
**Fix:** choose and state one C model, then propagate it. Correct the compositor-bypass figures to the values in the verification table. A/B idle strip factors remain transfers **(E)**; measured 1080p frame factors are **5.45/3.159/8**, not **3.4/1.7/4.5**.

**R6 — MAJOR: cancellation acknowledgement is conflated with completed I/O cancellation.**  
**Location:** §2.7 cancellation; G6c; §2.15.  
**Claim:** chunking and async cancellation make the **5 ms** acknowledgement hold through APST.  
**Counter:** payload transfer times exclude wake/tail latency, and cancellation may encounter non-cancellable requests. Cancellation submission does not establish safe buffer release. [liburing cancellation contract](https://kernel.googlesource.com/pub/scm/linux/kernel/git/axboe/liburing/+/refs/heads/master/man/io_uring_cancelation.7), [Windows cancellation contract](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations).  
**Fix:** retain **1/5 ms** for **logical acknowledgement: generation invalidated, future publication suppressed, cancellation requested**. Keep buffers and handles alive until original I/O completes; track physical cleanup separately. Describe ≤5 ms as a worker CPU slice, not a guaranteed I/O completion time.

**R7 — MAJOR: the alternative search algorithm does not guarantee linear verification work.**  
**Location:** §2.7; G6v.  
**Claim:** “Two-Way or SIMD Rabin–Karp with verification” supplies worst-case linear time.  
**Counter:** exact Rabin–Karp verification can take **O(S·needle length)** under collisions. SIMD does not remove that worst case. [Algorithm analysis](https://algs4.cs.princeton.edu/lectures/keynote/53SubstringSearch-2x2.pdf).  
**Fix:** require a deterministic linear fallback after bounded verification work. Keep G6v **160/250, 250/400, 630/1000** as chosen budgets pending measurement.

**R8 — MINOR: the table still fails its literal row contract.**  
**Location:** §0.2.  
**Claim:** every row has p50/p99, binding-resource evidence and a design consequence.  
**Counter/fix:** G3z, G10 and G10f have scalar/formula caps; explicitly mark them **hard maxima applying to both percentiles**—do not weaken them. G3z’s consequence is blank. G2c/G6c/G8s provide **(G)** budgets rather than binding-input evidence; G10f/G11 omit an explicit resource in that column. Name the CPU/GPU queue, scheduling, RAM or CPU-running-time resource and label unmeasured inputs **(E)**. Scope **(M)[bat]** tree latency to A, and **(M)[AC,A-hw]** 1080p measurements to their actual machine.

**Correct distinctions to retain**

- **16 GB/s is a conservative AC scan input**, not a battery-certified rate; **6.3 GB/s is not an optimized SIMD ceiling**.
- The naive **~10 cycles/pixel**, **2.6× raster** and **4.5× upload** calculations are correct. State the **scalar, approximately 100%-coverage** caveat wherever those historical values are cited. The optimized measurements actually include **25% coverage**; do not attach the naive coverage caveat to them.
- G4a correctly uses first-after-idle raster and excludes upload/WM waiting. **58–68 ms map→Expose** is neither app rendering nor optical startup.
- Native GTK’s **56–60 ms init** supports the chosen no-toolkit startup budget.
- Strict gates may remain engineering budgets despite unproven feasibility. The missing battery/GPU/B measurements alone do not require looser caps.
- Memory deduplication, **87/44/31 MB**, bounded search output, whole-operation tail budgets and paired differences should remain.

**Residual risks**

- Battery scan/raster/upload rates, GPU wake, idle strip timing and contention tails remain unmeasured.
- B is still uninstantiated; its raster evidence is A hardware at 1080p. C remains extrapolated and provisional.
- APST, whole-save tails and Windows power-loss rename durability remain unresolved.
- External mmap changes can race reads; detected cancellation cannot recover overwritten bytes or undo a completed save commit.
- Cold fonts, Unicode/IME/long-line completion and live-snapshot allocation peaks need implementation measurements.
- First-run Defender pairing needs reproducible reset conditions; readiness/cadence instrumentation needs backend validation.

**Top three outstanding measurements, in priority order**

1. **A on battery:** GPU and SSE2 MT4/MT8 full frames through actual readiness, warm and after ≥15 s idle; include first-key strips, input latency and defined bulk-worker pressure.
2. **A on battery:** newline/ASCII scan, dense counting and adversarial verification in the specified cached-file/new-mapping state, including faults and contention.
3. **Actual B fixture:** warm/idle raster and upload through verified DXGI/GDI completion, then optical tails and monitor/keyboard acceptance.

**Verdict: No, the current ship-gate table is not safe to build against as an engineering contract.** Repair G3/G3i’s completion endpoints, G11’s time units, G6c’s acknowledgement semantics and the inconsistent PB propagation. Keep G1 **1/2 ms**, G3 **A 5/(T/2), B 4/5 ms**, G3i **A 8/T provisional, B 10/T**, and the existing startup/edit/memory caps; the recomputations do not justify changing those limits. Make the hard maxima explicit and preserve scan provisional status. The required changes are chiefly to what passing means—not to numerical deadlines.