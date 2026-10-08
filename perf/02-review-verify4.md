**No—the numerical limits can stay, but the measurement contract and C model still need close-out corrections.** All three requested files, including the hardware addenda, were read in full. Every arithmetic result below was recomputed with Python. No files were modified. The comparison uses the v3.2 → v3.3 changelog and referenced sections; no archived v3.2 was available.

**R1–R8 dispositions**

| Finding | Disposition |
|---|---|
| R1 | **Partial:** matching-frame device completion is now required, but accelerated XShm still lacks a specific per-frame T5 completion event; “validated by Present / MSC” does not supply that timestamp. |
| R2 | **Fixed:** G11 uses CPU running time across all app threads; per-blink ETW supplies the Windows percentile samples, while GetThreadTimes aggregation supplies only a mean. |
| R3 | **Fixed:** A’s minimap-inclusive GPU frame is propagated consistently; A/B-resolution MT4 minimap totals reproduce. |
| R4 | **Partial:** the tested-path qualifications and estimated attribution are corrected, but §5.2 still states categorically that the SSE2 kernel “is traffic-bound.” |
| R5 | **Partial:** C strip, app, optical and warm-frame propagation reproduce; C MT2 idle silently uses the A MT4 idle factor, contradicting the unqualified “MT2 = ST/2” rule. |
| R6 | **Fixed:** logical acknowledgement is distinguished from I/O completion; original-request buffers and handles survive until completion, and cleanup is tracked separately. |
| R7 | **Fixed:** bounded verification followed by deterministic Two-Way fallback removes the claimed Rabin–Karp worst-case guarantee. |
| R8 | **Fixed:** hard maxima, missing resources/consequences and A-only measurement provenance are supplied. |

**Changed-number verification**

Times are milliseconds. Extra digits below expose the arithmetic; they are not warranted measurement precision.

For raster calculations:

- Full-frame pixels A/B/C: **5,184,000 / 2,073,600 / 1,049,088**.
- Minimap pixels A/B/C: **432,000 / 129,600 / 92,160**.
- C full-frame scaling factor: `4 × 1,049,088 / 5,184,000 = 0.80948148`.
- C strip-width scaling factor: `4 × 1366 / 2880 = 1.89722222`.

| §2.2 CPU path | Minimap recomputation | Complete frame recomputation | Assessment |
|---|---:|---:|---|
| A MT4 warm | `3.12 × 432000/5184000 = 0.260000` | `3.12 + 1.4 + 0.26 = 4.780000` | **4.78 correct**; `(T/2)/PB = 1.16225`. |
| A MT4 idle | `5.39 × 432000/5184000 = 0.449167` | `5.39 + 6.3 + 0.449167 = 12.139167` | **12.14 correct**; exceeds exact T by **1.02806**. |
| B-resolution MT4 warm | `1.13 × 129600/2073600 = 0.070625` | `1.13 + 0.6 + 0.070625 = 1.800625` | **1.80 correct**. |
| B-resolution MT4 idle | `3.57 × 129600/2073600 = 0.223125` | `3.57 + 4.8 + 0.223125 = 8.593125` | **8.59 and 8.6 are both correct rounding**; G3i k = **1.16372 / 1.93953**. |
| A ST warm / idle | **0.485833 / 1.675000** | **7.715833 / 28.075000** | **7.72 / 28.1 correct**. |
| B-resolution ST warm / idle | **0.125000 / 0.681250** | **2.725000 / 16.381250** | **2.73 / 16.4 correct**; idle fits exact T, but exceeds G3i p50 = 10. |
| A MT8 warm | **0.263333** | **4.823333** | **4.82 correct**. |

The after-idle ratios reproduce as A **3.447684 ST / 1.727564 MT4 / 4.5 upload**, and B-resolution **5.45 / 3.159292 / 8**.

A’s GPU-frame base is `5,184,000 × 4 / 25,000,000 = 0.829440`; adding the chosen **0.18 (E)** minimap allowance gives **1.009440**.

| Operation and location | Python recomputation | Published result |
|---|---|---|
| A open warm, §2.4 | `1.00944 + 65536/4.2e6 = 1.02504381` | **1.025**, correct |
| A open cold, §2.4 | `1.02504381 + 4×0.1 + 65536/2.5e6 = 1.45125821` | **1.451 + APST**, correct |
| A paste, §2.6 | `1/9.4 + 1/16 + 0.007906 + 1.00944 = 1.18622898` | **1.186**, correct |
| A undo, §2.9 | `10000×10×371/1e6 + 1.00944 = 38.109440` | **38.11**, correct |
| A tab switch, §2.10 | complete GPU frame **1.009440**, or MT4 fallback **4.780000** | Correct |

Paste and undo already implied the 0.18 allowance in v3.2; their correction is chiefly consistent definition and propagation. Open acquires the previously missing allowance.

C’s strip and optical propagation now reproduce:

| C quantity, §2.1 | Python recomputation | Assessment |
|---|---:|---|
| Strip | `0.40 × 1366/2880 × 4 = 0.758889` | **0.76 correct** |
| Server copy | `0.64 × 1366/2880 × 4 = 1.214222` | **1.21 correct** |
| Warm app | `0.758889 + 0.31 = 1.068889` | **1.07 correct**; G1 p50 k **0.93555** |
| Idle app | `0.758889 × (20.1/5.83) + 0.31 = 2.926409` | **2.93 correct** |
| Idle server | `1.214222 × 4.5 = 5.464000` | **5.46 correct** |
| Active optical mean | **65.683111** | **65.7 correct** |
| Active optical minimum / maximum | **47.249778 / 84.116444** | **47.2 / 84.1 correct** |
| Idle optical mean / maximum | **72.290409 / 90.723743** | **72.3 / 90.7 correct** |
| G2a / G2b p50 k | **1.080948 / 1.037482** | Flagged **1.08 / 1.04 correct** |

A/B idle means and maxima also reproduce: **42.717407 / 51.997963** and **71.920120 / 92.153453**. A’s pessimistic idle mean charges the full estimated PSR allowance **T**; it does not assume a uniform PSR delay.

| §2.1 compositor-bypass scenario | A | B | C |
|---|---:|---:|---:|
| Remove compositor frame: `active mean − T` | **16.826111** | **52.353333** | **49.016444** |
| Also remove mean latch wait: `active mean − 1.5T` | **11.270556** | **44.020000** | **40.683111** |

Thus the published **16.8/11.3, 52.4/44.0, 49.0/40.7** rows are corrected.

C’s remaining propagation is:

| Operation | Python result | Assessment |
|---|---:|---|
| ST warm / idle frame | **6.267129 / 22.799644** | **6.27 / 22.8 correct** |
| MT2 warm raster / upload / minimap | **2.359639 / 1.133274 / 0.207289** | Total **3.700201**, correct |
| MT2 idle, **using the unstated MT4-factor transfer** | **4.076427 / 5.099733 / 0.358105** | Total **9.534265** reproduces **9.53**, conditionally |
| Startup warm / cold, using that idle raster | **34.076427 / 43.445493** | **34.1 / 43.4**, conditionally correct |
| Startup p50 k, warm / cold | **1.966169 / 1.979492** | Flagged **1.97 / 1.98 correct** |
| Open warm / cold | **3.743892 / 6.180799** | **3.74 / 6.18 correct** |
| Open warm p50 / p99 k | **1.335509 / 1.869712** | **1.34 / 1.87 correct** |
| Paste | `1/3 + 1/4 + 0.018 + 3.700201 = 4.301535` | **4.30 correct** |
| Undo | `18 + 3.700201 = 21.700201` | **21.7 correct**; k **1.428558 / 1.935466** |
| Tab switch | **3.700201** | **3.70 correct** |

The new-mapping addition also reproduces:

`ceil(1e9/65536) × 0.001 = 15.259 ms`.

Adding it to pure scans **62.5 / 100 / 250** gives **77.759 / 115.259 / 265.259**, correctly displayed as **77.8 / 115.3 / 265.3**. A’s G6 k values are **1.028820 / 1.607531**, correctly displayed as **1.03 / 1.61**.

**Remaining findings**

**1. R1 residual — MAJOR: accelerated XShm T5 remains underspecified.**

- **Location:** §2.2 backend table and “validated once” sentence; §4 T5; G3/G3i.
- **Claim:** ShmCompletion supplies source reuse, with accelerated readiness separately validated through Present/MSC.
- **Counter-argument:** this correctly distinguishes reuse from readiness, but never names the accelerated readiness timestamp used for each measured frame. Present completion/MSC identifies presentation timing; it cannot retrospectively establish the earlier device-completion time. A one-time displayed-frame identity check does not fill that gap. [Upstream Present protocol](https://sources.debian.org/src/xorgproto/2025.1-1/presentproto.txt/).
- **Concrete fix:** name and observe a rendering-completion fence for every gated frame, then define T5 as the later of that observation and presentation submission. An ordered X Sync fence is one possible conservative mechanism: TriggerFence becomes triggered after preceding rendering completes; merely issuing it is insufficient. Validate the actual accelerated path. Keep Present/MSC for T6. [X Sync fence semantics](https://xorg.freedesktop.org/archive/X11R7.7/doc/xextproto/sync.html).

**2. R5 residual — MAJOR: C MT2 idle uses an undeclared exception to “ST/2.”**

- **Location:** opening C-model definition; §2.2 C MT2 idle row and “MT2 = ST/2”; §2.3 C startup.
- **Claim:** MT2 = ST/2, with idle raster **4.08**, frame **9.53**, and startup **34.1/43.4**.
- **Recomputation:** **4.076427** comes from  
  `(5.83 × 0.80948148 / 2) × (5.39/3.12)`: warm ST/2 multiplied by the **MT4** idle ratio. Applying ST/2 to the stated idle ST raster instead gives **8.135289** raster, **0.714667** minimap and **13.949689** complete frame. Startup then becomes **38.135289/47.504356**. The difference is **4.058862 ms** in each startup PB.
- **Concrete fix:** explicitly state that ST/2 applies to **warm raster only**, and that C MT2 idle adopts the A MT4 factor **5.39/3.12 (E)**. That preserves the published numbers without pretending the transfer is measured. Alternatively, use ST/2 in both states and propagate the alternative PBs and k values. No gate change is required merely to repair the model.
- Also replace §2.3’s blanket “restore k ≥ 2” sentence with wording that acknowledges the explicitly retained C exceptions.

**3. NEW — MAJOR: the battery rescaling formula now incorrectly scales the fixed fault term.**

- **Location:** §5.1, following the new-mapping PB additions in §0.1/§0.2.
- **Claim:** if `R_bat < 16`, the PBs scale by `16/R_bat`.
- **Recomputation:** under the stated additive model, A’s mapped-file PB is  
  `1000/R_bat + 15.259`, not `77.759 × 16/R_bat`. At **8 GB/s**, these are **140.259** versus **155.518**: an excess of **15.259 ms**, or **10.879%**. Rescaling the enlarged PB and retaining k would unnecessarily loosen the re-derived gate.
- **Concrete fix:** scale only the scan component; update the fault-cost estimate separately when measured. In §2.5, label **62.5/100/250** explicitly as *pure scan time* and show the mapped-file totals alongside it.

**Minor evidence and precision corrections**

| Location | Claim | Counter / severity | Concrete fix |
|---|---|---|---|
| §5.2 B-fixture follow-up | SSE2 kernel “is traffic-bound” | **MINOR:** contradicts §2.2’s correctly estimated attribution pending counters. | Replace with “bottleneck attribution remains (E) pending counters.” |
| §2.1 B sensitivity note, newly added | Idle maximum **94.3** | **MINOR:** Python gives **94.384333**, rounding to **94.4**. Mean **74.151** correctly rounds to **74.2**. | Change maximum to **94.4**; retain the **100** p99 gate. |

**Gate-limit comparison**

**No numerical gate limit changed in the supplied v3.2 → v3.3 comparison.** The changes are PBs, k values, endpoint definitions, evidence labels and stated risks. In particular:

- A G3 remains **5 / (T/2)**; G3i remains **8 / T**.
- B G3 remains **4/5**; G3i remains **10/T**.
- C G1 remains **1/2**, G3 **5/8**, G3i **10/T**, and startup **67/90 warm, 86/120 cold**.
- G11 remains **0.1/0.2** on A/B.
- G3z/G10/G10f becoming explicit hard maxima clarifies enforcement without increasing their caps.

**Correct distinctions to retain**

- The **0.18 ms GPU minimap allowance** is explicitly **(E)**. Its coherent propagation is valid without claiming measured GPU feasibility.
- Independently rounded summands need not add to the displayed total: C’s **4.08 + 5.10 + 0.36** displays **9.54**, while the underlying calculation is **9.534265 → 9.53**. Clarify the model; do not “fix” this by adding rounded inputs.
- C’s **G1 PB exceeding its provisional limits**, and its optical/startup k falling below the former aspirations, are now disclosed. These are feasibility risks, not reasons to silently relax caps.
- Windows blink aggregation supplies a mean; the specified per-blink ETW data supplies the percentile distribution. Use that same distribution for both gate percentiles.
- Logical cancellation can finish before physical I/O cleanup. Keeping buffers alive until original-request completion is the necessary distinction.
- AC/proxy measurements remain provisional inputs. Missing battery, GPU and B-fixture measurements do not themselves justify changing chosen ship limits.

Residual items, grouped into three:

1. Specify accelerated XShm’s per-frame device-completion timestamp.
2. Declare the C MT2 idle-factor transfer and repair additive scan/fault rescaling.
3. Remove the remaining attribution, k-description and rounding inconsistencies.

**Verdict: No, the gate table is not yet a safe engineering contract, with feasibility explicitly excluded.** The remaining readiness ambiguity can produce inconsistent G3/G3i results across backends, and the C idle and battery-rescaling rules are not internally consistent as written. **I would change no ship-gate number.** Retain the existing limits, including exact refresh formulas **A T/2 = 5.555556 ms, A T = 11.111111 ms, B/C T = 16.666667 ms**, and close the three residual groups above.