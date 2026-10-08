# Arithmetic and physical consistency review

Both files were read in full. All numerical checks below were recomputed with Python; no files were modified. Calculations use decimal MB/GB for file sizes and rates, binary KiB/MiB where explicitly stated, and unrounded frame intervals of \(1000/90\) and \(1000/60\) ms.

Locations refer to `01-perf-target.md` unless the hardware profile is named explicitly. Recomputed estimates are conditional on the document’s assumptions; they are not new measurements.

## Findings

### 1. The document does not establish lower bounds

**Location:** Method; §0.2; “Physics floors”; forced architecture decisions  
**Severity: BLOCKER**

**Claim:** Pessimistic measured or estimated rates establish an “achievable floor,” from which physical impossibility and mandatory architecture can be inferred.

**Counter-argument:** For \(t \ge B/R\) to be a lower bound, \(R\) must be a ceiling on throughput, and \(B\) must be unavoidable work. A benchmark result is neither necessarily a throughput ceiling nor the minimum work required by another implementation.

For example, reading 1 GB at A’s assumed 9.4 GB/s takes **106.38 ms**; the theoretical 102.4 GB/s interface gives **9.77 ms** for that traffic. The benchmark does not prove that hardware cannot outperform 9.4 GB/s. Similarly, estimated compositor, process-create, flush and driver-init times are not unavoidable minimum latencies.

The introductory qualification does not repair subsequent claims that particular timings are physically unreachable or that a toolkit, rendering path or buffer representation is forced.

**Fix:** Separate:

- Physical lower bounds based on unavoidable traffic and throughput ceilings.
- Pessimistic workload estimates used to set engineering budgets.
- Empirical ship gates measured on the specified configuration.

Retain battery measurements as binding planning inputs. Remove physical-impossibility and mandatory-design conclusions that rely only on estimates.

### 2. A’s memcpy payload rate is incorrectly labelled a measured SIMD scan rate

**Location:** Target archetypes, “ST SIMD read”; §5; §7; Assumption 1  
**Severity: BLOCKER**

**Claim:** A has a measured SIMD read rate of 9.4 GB/s, obtained from the read half of 18.8 GB/s memcpy.

**Recomputation:** \(18.8/2 = 9.4\) GB/s correctly describes **copied payload per second** when memcpy counts both source reads and destination writes. It does not measure an independent read-only scan.

The profile’s measured ST sequential read is **6.3 GB/s**. Using that binding input until a relevant SIMD scan is measured gives:

| A operation | At assumed 9.4 GB/s | At measured 6.3 GB/s |
|---|---:|---:|
| Scan 100 MB | 10.64 ms | **15.87 ms** |
| Scan 1 GB | 106.38 ms | **158.73 ms** |
| Scan 64 KiB | 6.97 µs | **10.40 µs** |
| §5 lookup: 18 misses plus 64 KiB scan | 13.65 µs | **17.08 µs** |

A’s measured **MT read rate of 23.2 GB/s remains a valid separate input**.

**Fix:** Keep 9.4 GB/s as measured memcpy payload throughput. Change the ST scan input to 6.3 GB/s, or tag 9.4 as an unverified estimate and obtain the battery SIMD measurement before making it binding.

### 3. A’s keystroke-chain mean is arithmetically wrong

**Location:** §1; summaries; G2; compositor-bypass notes  
**Severity: MAJOR**

**Claim:** A’s mean is 26.4 ms; bypass means are 15.3 ms with vsync and 9.8 ms with tearing.

**Recomputation:** Following the stated midpoint convention:

\[
L_A =
3.25+0.125+0.55+0.3+2(1000/90)+0.55
=\mathbf{26.9972\ ms}.
\]

Consequently:

- Composited mean: **27.0 ms**, not 26.4.
- \(1.1L_A\): **29.697 ms**, not 29.1.
- No compositor, vsync: **15.886 ms**, not 15.3.
- No compositor, tearing: **10.331 ms**, not 9.8.

B’s corresponding mean is **68.3 ms**, rather than 68.2; its relative target is **75.13 ms**. C’s **63.9 ms** mean is correct.

**Fix:** Correct every dependent table and architecture note. Use unrounded intermediate values.

### 4. Compositor and scaler delays are configuration assumptions, not compulsory physical floors

**Location:** Target archetypes; §1; G2; Assumptions 3, 4 and 6  
**Severity: BLOCKER**

**Claim:** Every composited target must pay latch wait **plus one complete frame**, and B/C must pay another complete frame in the monitor.

**Counter-argument:** Composition does not intrinsically require a complete additional frame after the latch wait. The submission deadline, composition schedule and presentation mode determine that delay. Microsoft explicitly documents windowed Independent Flip, MPO and reduced-latency presentation paths. [Microsoft DXGI guidance](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model).

A monitor scaler likewise does not intrinsically require a full-frame buffer. Its latency must be specified or measured. Panel response also needs a detection threshold: time to first detectable change and time to complete a pixel transition are different metrics.

Under the document’s minimum A input-stage values, with no compulsory extra compositor frame, a vsynced mid-screen mean would be:

\[
0.5+0.05+0.1+0.3+0.1+1000/90
=\mathbf{12.161\ ms}.
\]

That is a counterexample to the claimed proof that 13 ms is physically unreachable; it is not evidence that this laptop currently achieves it.

There is also a pessimism inconsistency: B binds a **one-frame scaler**, while its stated range includes **three frames**. Using three frames changes B’s modelled chain mean to **101.63 ms** and its relative target to **111.80 ms**. A’s acknowledged possible PSR-exit frame changes the first-after-idle mean to **38.11 ms**, above G2.

**Fix:** Specify presentation mode, monitor and idle state. Treat these as separate measured scenarios. Preserve the one-frame model only as an explicit scenario, and remove the universal impossibility claim. Give G2 an applied p99 criterion; the table currently supplies only a mean gate.

### 5. G4’s “on glass” startup numbers omit required display stages

**Location:** §3; G4  
**Severity: BLOCKER**

**Claim:** Adding one \(T\), at best latch phase, produces process-start-to-first-interactive-frame-on-glass bounds.

**Counter-argument:** The startup sums omit latch-phase variability, scanout to the observation point, monitor scaler latency and panel response. B’s fixed scaler alone adds **16.67 ms**.

For an explicitly defined **mean at mid-screen**, retaining the document’s one-frame compositor model, add \(T/2\) latch wait, \(T/2\) scanout, scaler delay and midpoint panel response to the existing sums:

| Target | Warm model | Cold model, corrected 2 MB read |
|---|---:|---:|
| A | **35.11 ms** | **37.51 ms** |
| B, one-frame scaler | **77.37 ms** | **86.02 ms** |
| C | **87.80 ms** | **116.63 ms** |

These are conditional mean estimates, not p99 bounds. They do not support the published **47/52**, **68/85**, **99/100 ms** on-glass gate derivation.

**Fix:** Define the observation row and percentile. Either change G4 to an app-ready metric or include the entire display path. Under the mean-at-mid-screen definition, the existing rule gives approximately **70/75 ms on A** and **100/100 ms on one-frame-scaler B**. C needs a smaller startup read footprint to support a 100 ms cold estimate.

### 6. Startup request counts and C’s read budget are inconsistent

**Location:** §3, cold additions and binary-size budget  
**Severity: MAJOR**

**Claim:** A’s 2 MB binary requires 15 requests; C’s \(31(500+437)\) µs totals 28.6 ms; touching 1 MB satisfies a 10 ms read budget.

**Recomputation:** For decimal 2 MB:

\[
N_A=\lceil2{,}000{,}000/131{,}072\rceil=\mathbf{16};
\quad
N_B=N_C=\lceil2{,}000{,}000/65{,}536\rceil=\mathbf{31}.
\]

Using request overhead plus the actual transferred bytes:

| Target | Cold addition |
|---|---:|
| A | \(16(100\ \mu s)+2\text{ MB}/2.5\text{ GB/s}=\mathbf{2.40\ ms}\) |
| B | \(31(150\ \mu s)+2\text{ MB}/0.5\text{ GB/s}=\mathbf{8.65\ ms}\) |
| C | \(31(500\ \mu s)+2\text{ MB}/0.15\text{ GB/s}=\mathbf{28.83\ ms}\) |

The printed C multiplication itself is **29.047 ms**, not 28.6.

The claimed 10 ms capacities—approximately **8.6/2.3/0.7 MB**—are consistent with this serialized request model. The subsequent 1 MB allowance is not: C takes **14.67 ms** for decimal 1 MB. Ten full 64 KiB requests, **640 KiB**, take **9.37 ms**.

Also, desktop use does not make an embedded font or statically linked library pages in the editor’s new binary hot.

**Fix:** Use ceilings and consistent units. For C’s stated 10 ms budget, cap the total cold startup file footprint at **640 KiB**, including embedded assets and other required cold pages. State exactly what “cold” evicts.

### 7. Rendering times and “app-side” measurement boundaries disagree

**Location:** Notation; §2; G3; GPU redraw rows  
**Severity: BLOCKER**

**Claim:** App-side ends when the buffer is handed to the X server/compositor, but includes Xorg/glamor’s upload copy and GPU raster duration.

**Counter-argument:** In the stated XShm path, submitting the request hands shared image data to Xorg; the server-side copy occurs after that handoff. It need not block the app for another **2.206 ms**. GPU submission similarly need not block the app until its **0.829 ms** raster finishes.

Thus A’s modelled **4.412 ms** raster-plus-upload pipeline is not automatically a lower bound on the defined app-side interval. Conversely, a fast submission says nothing about when the buffer becomes display-ready.

**Fix:** Define separate timestamps for event dequeue, submission, rendering/copy completion and presentation. Gate app latency and display readiness separately. Attribute server/GPU work to the correct interval.

### 8. The CPU-path conclusion omits clock and shared-resource constraints

**Location:** §2; G1/G3/G6 under background work  
**Severity: MAJOR**

**Claim:** Compute binds only at 400 MHz; CPU raster fits half a frame everywhere; background workers preserve the same rates.

**Recomputation:** A’s stipulated 0.5 cycles/pixel requires **2,592,000 cycles**. Its raster bandwidth estimate is **2.206 ms**, so compute becomes slower below:

\[
2{,}592{,}000/0.002205957=\mathbf{1.175\ GHz}.
\]

It does not bind only at 400 MHz. At 400 MHz, the model gives:

\[
\max(6.48,\ 2.206)+2.206=\mathbf{8.686\ ms},
\]

which exceeds the **5.556 ms** half-frame budget.

The ramp to 4 GHz is unmeasured, despite being load-bearing for the battery conclusion. The input section allows only 0.15 ms ramp margin while §2 describes a ramp on the order of milliseconds.

CPU, iGPU and scanout also share memory. On B, two independent workers each consuming the assumed 18 GB/s would request **36 GB/s**, already above the **25.6 GB/s** channel peak before rendering.

**Fix:** Measure rendering after idle on battery and during the gated background workloads. Specify worker concurrency and throttling. Describe CPU-fit and GPU preference as conditional until clock ramp and contention are included.

### 9. The full-index pass contradicts the document’s own UTF-8 throughput estimate

**Location:** §4; forced SIMD decision  
**Severity: BLOCKER**

**Claim:** Fused UTF-8 validation plus newline counting is DRAM-bound, including C’s **167 ms** warm index of 1 GB.

**Counter-argument:** Compare/movemask throughput establishes a simple byte classification rate, not arbitrary UTF-8 validation throughput. The document later gives C’s non-ASCII validation rate as **0.5–1 GB/s**, explicitly compute-bound.

At those rates, 1 GB requires **1–2 seconds**, rather than 167 ms.

**Fix:** Make the background pass newline counting plus an ASCII flag, with non-ASCII validation lazy as the architecture section proposes. Alternatively, give validated non-ASCII indexing a separate compute-derived budget. C’s cold 6.67-second storage estimate can still remain storage-bound.

### 10. Generic literal-find latency cannot be derived from input bandwidth alone

**Location:** §7; G6  
**Severity: BLOCKER**

**Claim:** First-byte filtering plus verification always sustains at least 8 B/cycle, so arbitrary literal find is DRAM-bound.

**Counter-argument:** Frequent first-byte candidates and long repeated prefixes can make verification dominate. Result production can dominate independently.

For a 1 GB file containing one repeated byte and a matching one-byte literal, there are **one billion matches**. Materializing 64-bit offsets writes **8 GB**. Input plus output is approximately 9 GB of traffic; even at the theoretical interface peaks that requires approximately:

| Target | 9 GB / theoretical bandwidth |
|---|---:|
| A | **87.89 ms** |
| B | **351.56 ms** |
| C | **703.13 ms** |

Those exceed the **86/111/333 ms** gates. An 8 GB offset array also exceeds C’s RAM and B/C’s editor budgets. Compression or lazy enumeration can avoid that array, but they must be part of the operation definition.

**Fix:** Define G6’s pattern distribution, verification algorithm, result representation and output cap. Retain the current numbers only for a measured, bounded-results throughput workload. Generic literal search needs verification and output terms.

### 11. Pointer-chase arithmetic is correct; its cache model is not a lower bound

**Location:** §5; §6; §9  
**Severity: MAJOR**

**Claim:** Every tree/index access pays A’s 1 GiB pointer-chase latency of 371 ns; a B+ tree needs at most two misses per node.

**Counter-argument:** The 10 GB sparse index occupies **2.441 MB**, not 1 GiB. Its footprint falls near the measured L2/L3 regimes. Repeated access can be substantially faster. For comparison, 18 accesses at the measured 115 ns regime cost **2.07 µs**, versus **6.678 µs** at 371 ns.

The assumed 200,000 pieces occupy **9.6 MB** at 48 B/piece, before tree overhead. This likewise does not imply a 1 GiB random working set. Root and ancestor reuse especially matter for repeated undo.

The two-miss allowance also requires a node layout. Sixteen entries containing pointer, byte count and newline count occupy **384 B**, or six cache lines. Parallel fetches may help, but two serial misses cannot be asserted without explaining the search layout.

**Fix:** Label the current calculation a pessimistic cold-metadata scenario. Specify node layout, footprint, residency and mutation/allocation costs. Do not interpret a large-file size as the metadata working-set size.

### 12. Undo and tab-switch totals omit explicitly included work

**Location:** Summary rows 9/10; §9; §10  
**Severity: MAJOR**

**Claim:** Batched undo including one redraw costs 37/15/18 ms; tab switch equals §2 despite its formula including §11.

**Recomputation:**

| 10,000 undo steps | A | B | C |
|---|---:|---:|---:|
| Metadata operation model | 37.10 ms | 15.00 ms | 18.00 ms |
| Plus one CPU redraw | **41.51 ms** | **17.37 ms** | **20.80 ms** |
| Twice the complete operation | **83.02 ms** | **34.74 ms** | **41.60 ms** |

For tab switch, the literal §2 + §11 formula gives **4.596/2.444/2.920 ms**. Applying the gate rule yields **5.556/4.888/5.841 ms**.

**Fix:** Change undo targets to approximately **83/35/42 ms**. Either include minimap rebuilding in tab-switch G3 or explain that it is cached/included in the renderer and remove the extra term.

### 13. The mmap per-file gate excludes the editing state in its own formula

**Location:** G7 per-file gate; §6; §12  
**Severity: BLOCKER**

**Claim:** Mmap private memory is always at most \(0.001S+1\) MB.

**Recomputation:** The example 100,000 one-character edits allows:

- 200,000 pieces × 48 B = **9.6 MB**.
- 100,000 undo records × 32 B = **3.2 MB**.
- Typed bytes = **0.1 MB**.

Before internal nodes and allocator overhead, total private state including the sparse index is:

| Original file | Modelled private state | Published cap |
|---|---:|---:|
| 1 GB | **13.144 MB** | **2 MB** |
| 10 GB | **15.341 MB** | **11 MB** |

The published gate fails on the document’s own editing workload.

**Fix:** Restrict \(0.001S+1\) MB to unmodified originals. Add explicit terms for pieces, undo and retained inserted/deleted text, with allocation slack. Editing-state memory cannot be capped solely by original-file size.

### 14. “Private memory” and physical RAM occupancy are mixed

**Location:** §4 read path; §12 baseline; G7  
**Severity: MAJOR**

**Claim:** Double buffers, binary pages and driver estimates form a private-memory lower bound; a 10 GB mmap necessarily exceeds B’s 16 GB after also counting page cache.

**Counter-argument:** File-backed, shared and anonymous memory are different accounting categories. Linux explicitly reports file RSS, shared-memory RSS and page-table memory separately. [Linux `/proc` documentation](https://www.kernel.org/doc/html/latest/filesystems/proc.html).

Read-only mmap uses page-cache pages; counting the mapped data and those same cache pages as two physical copies is incorrect. A private copy plus cached original can require two copies; mmap does not. Therefore B’s 10 GB warm case is conditional on the remaining OS footprint, rather than intrinsically “no fit.”

The baseline also assumes two buffers and 8 MB driver-private memory even for the startup CPU/no-GPU path. Neither is established as an unavoidable private allocation. Conversely, the separate three-viewport minimap cache needs **5.184/1.555/1.106 MB**, omitted from the baseline formula.

**Fix:** Define the exact accounting metric. Report private allocations, shared render buffers, file residency and kernel/GPU allocations separately. Keep **79/41/29 MB** as budgets if desired, but stop calling the formula a private-memory lower bound.

### 15. First-text independence from file size requires an explicit bounded-prefix contract

**Location:** §4; G5  
**Severity: MAJOR**

**Claim:** One read of at most 64 KiB establishes first text for any \(S\).

**Recomputation:** The stated read-plus-redraw model, excluding `open`, mapping and metadata costs, gives:

- A: **4.538 ms**.
- B: **2.651 ms**.
- C: **3.734 ms**.

These support approximately **9.1/5.3/7.5 ms** after doubling.

However, a bounded read establishes a bounded prefix, not arbitrary complete layout. Very long lines, wrapping and long combining sequences require explicit clipping or work limits. Copy-mode loading must also avoid copying the entire threshold-sized file before presenting that prefix.

The 100 ms absolute ceiling is end-to-end, while the relative quantity is app-side; those cannot be compared directly without budgeting presentation.

**Fix:** Define first text as rendering a bounded byte/glyph prefix, with larger layout and loading continuing asynchronously. Add open/mapping costs and keep presentation as a separate metric. Size-independent first text is feasible under that contract.

### 16. Save arithmetic is valid for a warm, overlapped model, but not the stated general workload

**Location:** §8; storage archetype; Assumption 8  
**Severity: MAJOR**

**Claim:** Save is \(\max(S/R_{pc},S/R_{write})+2F\).

**Counter-argument:** `max` describes an ideal pipeline; it is not inherently an arithmetic error. But mmap sources can require disk reads, and simultaneous reads/writes on the same device need their own service model.

For illustration, serialized cold-source read plus write of 1 GB gives **1,404/4,520/26,707 ms**, versus the warm model’s **1,004/2,520/20,040 ms**.

Flush costs are also estimates without a percentile. The document acknowledges roughly 10× variability. If A’s chosen pessimistic flush were 20 ms instead of 2 ms, the 1 MB model would be **41 ms**, with a **82 ms** doubled target, rather than 5/10 ms.

**Fix:** Separate warm-source and cold-source saves. State the overlap assumption and measure flush distributions under the relevant battery/full-disk conditions. The resource binding the gate must include durability, not only sequential writing.

### 17. The native-toolkit prohibition does not follow from the measured workload

**Location:** Hardware profile toolkit measurement; §3; forced toolkit/linking decisions  
**Severity: MAJOR**

**Claim:** The 121 ms Python-GI GTK load is what taking a native toolkit costs, forcing raw XCB/Win32.

**Counter-argument:** That benchmark includes the Python/GI loading path. It does not isolate native GTK initialization or establish a lower bound for every toolkit. Likewise, fork/exec/wait of a trivial binary is a different measurement interval from process-start-to-interactive-frame.

The **121 ms result remains binding for that measured workload**, but it cannot be transferred wholesale to another startup path.

**Fix:** Retain the measurement with its exact scope. Measure a native minimal window before using it to mandate architecture. Raw APIs can remain a design choice; the presented arithmetic does not force them.

### 18. Historical calibration is not reproducible enough to set the slack multiplier

**Location:** §0.3; G2’s \(k=1.1\)  
**Severity: MAJOR**

**Claim:** WP 5.1 ran at 1.09× its hardware floor, establishing the modern multiplier.

**Recomputation:** \(12.3/11.2=\mathbf{1.0982}\), which rounds to **1.10×**, not 1.09×.

More substantially, the 11.2 ms floor is not derived from the supplied chain. Applying §1’s midpoint convention to the historical ranges gives:

\[
5+0.55+0.3+(1000/70)/2=\mathbf{12.993\ ms}.
\]

Different stage distributions could produce another mean, but those distributions are not supplied. Both historical timings are estimates, so the displayed ratio does not constitute measured calibration.

**Fix:** Supply the stage means or measured source and measurement boundary. Until then, label \(k=1.1\) an engineering aspiration, not a historically derived requirement.

### 19. Unit conventions and file-count precision need cleanup

**Location:** Copy threshold; §12 maximum files; G3 display precision  
**Severity: MINOR**

**Claim:** C’s threshold is 128 MB; maximum-file counts use the whole editor budget; A’s strict half-frame limit is 5.6 ms.

**Recomputation:**

- With decimal 4 GB RAM, RAM/32 is **125 MB**. With 4 GiB it is **128 MiB**. “128 MB” mixes conventions.
- Subtracting baseline gate budgets of 79/41/29 MB changes the 1 MB file counts to **6,028/3,012/738**, rather than 6,088/3,044/761.
- The corresponding 2 MB mmap-state examples become **3,960/1,979/485** files.
- A’s exact half-frame limit is **5.555556 ms**. A literal 5.6 ms threshold permits **44.44 µs** beyond it.

**Fix:** Declare units for installed RAM, budgets and thresholds. Subtract baseline allocations in capacity examples. Display approximate frame budgets, but enforce the exact formula.

## Arithmetic and distinctions that are right

These should not be “corrected” because of the findings above.

- **Channel widths and rates:** LPDDR5-6400 over 128 bits gives **102.4 GB/s**; single-channel DDR4-3200 gives **25.6 GB/s**, dual-channel **51.2 GB/s**; single-channel DDR3-1600 gives **12.8 GB/s**. B’s estimated 18 GB/s MT read is below its channel peak.
- **Memcpy accounting:** Dividing A’s r+w-counted memcpy results by two gives **9.4 GB/s ST** and **18.95 GB/s MT** copied payload. Rounding the latter to 19 is reasonable. The error is transferring this to a measured read-only scan.
- **Frame geometry and scanout arithmetic:**

  | Target | Framebuffer bytes | Nominal scanout traffic |
  |---|---:|---:|
  | A | **20,736,000** | **1.86624 GB/s** |
  | B 1080p | **8,294,400** | **0.497664 GB/s** |
  | B 1440p | **14,745,600** | **0.884736 GB/s** |
  | C | **4,196,352** | **0.251781 GB/s** |

  A’s reported physical dimensions give approximately **242 ppi**. These are consistent uncompressed framebuffer calculations.

- **Redraw conversions, conditional on the chosen rates:** CPU raster-plus-copy is **4.412/2.370/2.798 ms**; GPU framebuffer-write estimates are **0.829/0.691/0.699 ms**; the stipulated two-framebuffer compositor traffic gives **1.659/1.382/1.399 ms**. B’s 1440p CPU result is **4.213 ms**. The main objection is to the resource model and measurement boundary.
- **Input quantization:** A stipulated 125 Hz USB polling interval is **8 ms**, with **4 ms** mean phase wait under a uniform phase model. Changing it to 1000 Hz removes **3.5 ms** from that mean. An i8042 path has no USB polling quantum; it still has scan/debounce and transport latency.
- **Conditional display-phase accounting:** For vsynced mid-screen output, mean latch wait and scanout each contribute \(T/2\). For direct tearing updates, mean wait to the next beam pass is \(T/2\); an additional mid-screen \(T/2\) should not be added.
- **A’s best and worst chain sums:** Using exact \(T\), **12.161/41.833 ms** correctly round to **12.2/41.8**. B’s **39.733/96.867** and C’s **37.133/90.667** likewise support their displayed endpoints. Summing rounded stage-table entries can create harmless tenth-millisecond differences.
- **Bulk conversions:** For 1 GB, MT reads take **43.103/55.556/166.667 ms**; cold sequential reads take **400/2,000/6,666.667 ms**; cached copies take **238.095/400/666.667 ms**. The larger table’s size scaling is consistent with those inputs.
- **Sparse-index arithmetic:** Sixteen bytes per 64 KiB is **0.024414%**. Decimal 10 GB requires **152,588 chunks**, **18 binary-search levels**, and **2,441,408 B** of records. A per-line u64 array at 40 B/line costs **20% of file size**, or **2 GB** at 10 GB.
- **Gap-buffer and pointer arithmetic:** The stated gap timings and **2.35/1.75/0.75 MB** quarter-millisecond crossovers follow their rates. Ten misses cost **3.71/1.50/1.80 µs**; 35 A misses cost **12.985 µs**. The arithmetic is sound even though the miss model is not a proven bound. The crossover’s separate “1 ms app target” should be reconciled with G1’s 2 ms gate.
- **Warm save table:** All displayed save values follow the stated `max` formula and two flushes. Do not mechanically replace `max` with addition if overlap is genuinely implemented.
- **Minimap and small operations:** Minimap raster estimates are **0.18383/0.07406/0.12288 ms**; their **0.4/0.15/0.25 ms** targets reasonably round doubled unrounded values. Atlas sizes are **61,560/12,160 B**. A cursor cell is **2,592 B**, B/C **512 B**, so request overhead dominates.
- **Historical transfer arithmetic:** The supplied \(9+25n\) formula for 2,000 words gives **50,009 cycles**, or **10.484 ms** at 4.77 MHz. A 4,000 B transfer at 2–5 MB/s takes **2.0–0.8 ms**. This validates the arithmetic, not the historical instruction-timing premise.
- **Memory and idle arithmetic:** \(2fb+11\) MB gives **52.472/27.5888/19.392704 MB**. Multiplying by 1.5 supports the displayed baseline budgets approximately. Ten thousand 32 B undo records require **320 KB**. A 500–600 ms blink half-period gives **2.0–1.67 wakeups/s**; the blink slack multiplications and 5-second highlighting conversion are correct.

The ship-gate set is **not safe to build against as a validated hardware-derived contract**. For the explicitly fixed one-frame-compositor/one-frame-scaler scenarios, I would correct G2 to **29.7/75.1/70.3 ms**, G3 to **\(T/2\), 4.89, 5.84 ms** when tab switching rebuilds the minimap, and mean-at-mid-screen G4 to approximately **70/75 ms on A**, **100/100 ms on B**, and **100/100 ms on C only with a ≤640 KiB cold startup footprint**. If B truly includes the stated three-frame-scaler worst case, its corresponding model instead gives **111.8 ms G2** and approximately **221/239 ms G4** under the exception rule. I would retain G1’s **2 ms**, the baseline memory budgets and G8, narrow G5 to bounded-prefix presentation, narrow G6’s **86/111/333 ms** to a measured bounded-results workload, and replace the edited-file G7 cap with explicit history/piece/text terms. Those changes make the arithmetic and scope consistent; battery rendering, presentation, concurrent workloads and storage tails still need measurement before the estimates can justify ship gates.