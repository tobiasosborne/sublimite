# 01 — Performance target, v3.4

v3.3 = v3 (round-2 verification) + v3.1 / v3.2 (measured raster + upload, 00-profile Addenda 2–3) + v3.3 (round-3 verification `02-review-verify3.md`: endpoints, units, propagation; **no gate limit changed**). Tag `(M)[AC,A-hw]` = measured on **A hardware at B resolution, not the B fixture**. **C model (E):** every C CPU cost = the A-panel measurement scaled by C's pixel count (or strip width) × 4, with A-panel after-idle factors. **A** and **B** are *release* gates. **C** gates are **PROVISIONAL [est]**: tracked, but not enforced in CI until a real C SKU exists and has been measured. Scan-dependent A gates are **provisional [AC] until the battery rerun** (§5, first follow-up).

## 0. Method and evidence tags

| Tag | Meaning | Can prove impossibility? |
|---|---|---|
| **(P)** | physics: unavoidable traffic ÷ theoretical peak, or display geometry (refresh rate, scanout position) | yes |
| **(M)** | primitive measured on Target A. `[bat]` = battery/powersave; `[AC]` = charging/powersave | no; binding planning input |
| **(E)** | estimate: an unmeasured stage, rate, or a penalty transferred from another workload; pessimistic end of the range | no |
| **(G)** | gate or budget: an engineering choice | — |

- **Planning bound (PB)** = time computed from (M) and (E) inputs. It is tagged by its weakest input. It is not a lower bound.
- **Gate** = p50 and p99 limits. Each is a (G) budget, chosen as k × PB with k stated. Every composite tail limit (startup, save, open) is a (G) budget measured on the *whole-operation* distribution. Primitive percentiles are never added: summing two per-stage p99s guarantees only p98 of the sum.
- **Perceptual aspirations** (§0.3) are promoted to gates on a machine only after the reference renderer (§4) has been *measured* meeting them there.
- **Units:** MB/GB are decimal; KiB/MiB/GiB are binary; installed RAM is in GiB. T = exact frame interval (1000/90 or 1000/60 ms). Gates use exact formulas; displayed values are rounded (e.g. T/2 on A = 5.5556 ms, shown as 5.56).

## 0.1 Planning bounds

All values in ms unless stated.

| # | Operation (§) | (P) floor A / B / C | PB A | PB B | PB C [prov] | Weakest input |
|---|---|---|---|---|---|---|
| 1 | Keystroke→photon, mean, mid-screen, active (2.1) | beam wait T/2: 5.56 / 8.33 / 8.33 | 27.9 | 69.0 | 65.7 | (E) compositor, scaler, key scan |
| 1i | …first key after ≥ 15 s idle | same | 42.7 | 71.9 | 72.3 | (E) PSR, wake; (M)[AC] frame ratios 3.4× / 4.5× transferred to the strip (E) |
| 1s | …app stage, ingress → submit (+ server copy, not waited on) | — | 0.6 (+0.64) | 0.5 (+0.42) | 1.07 (+1.21) (E) | (M)[AC] strip raster 0.3–0.4; (E) rest |
| 2 | Full frame incl. minimap + upload, warm (2.2), SSE2 kernel | fb ÷ peak BW: 0.20 / 0.32 / 0.33 | GPU 1.009 (E: 0.829 + 0.18 minimap); CPU MT4 4.78, ST 7.72 (E from (M)[AC]) | CPU MT4 1.80, ST 2.73 (E from (M)[AC,A-hw]) | CPU MT2 3.70 (E) | A GPU unmeasured |
| 2i | …first full frame after ≥ 15 s idle | same | GPU ? (E); CPU MT4 12.14, ST 28.1 (E from (M)[AC]) | MT4 8.6, ST 16.4 (E from (M)[AC,A-hw]) | MT2 9.5 (E) | GPU wake unmeasured |
| 2b | One line: raster + server copy | — | 0.40 + 0.64 (M)[AC] | 0.38 + 0.42 (M)[AC,A-hw] | 0.76 + 1.21 (E) | (M) |
| 3a | Start → CPU back buffer complete + map requested, warm / cold (2.3) | — | 12.5 / 14.9 | 18.6 / 27.2 | 34.1 / 43.4 | (M)[AC] after-idle MT4 first frame; (E) rest |
| 4 | Open → correct first viewport submitted, warm / cold (2.4) | — | 1.025 / 1.451 + APST (GPU, (E)) | 1.8 / 2.6 | 3.7 / 6.2 | full frame (#2) |
| 5 | Index 1 GB, 1 worker, warm (cached file, new mapping: + 15.3 ms faults (E)) / cold (2.5) | 9.8 / 39 / 78 (warm) | 77.8 [AC] / 400 | 115.3 / 2 000 | 265.3 / 6 667 | A warm (M)[AC] |
| 5j | Jump to line 10⁷ with no index (400 MB), warm / cold | 3.9 / 16 / 31 | 25 [AC] / 160 | 40 / 800 | 100 / 2 667 | as #5 |
| 6 | Insert 1 char incl. split + allocation (2.6) | — | 0.008 | 0.008 | 0.018 | (M)/(E) |
| 6b | 1 MB paste → frame | — | 1.186 (GPU, (E)) | 2.05 | 4.30 | full frame (#2) |
| 7 | Find 1 GB, ordinary, 1 worker, new mapping (2.7) | 9.8 / 39 / 78 | 77.8 [AC] | 115.3 | 265.3 | as #5 |
| 8 | Save 1 MB (2.8) | — | 5 | 22.5 | 60 | (E) flush |
| 9 | Undo 10k steps + frame incl. minimap (2.9) | — | 38.11 | 16.8 | 21.7 | (M)[bat] latency, A only |
| 12 | Baseline memory, MB (2.12) | — | 57.7 | 29.1 | 20.5 | (E) driver |
| 13 | App CPU per blink (2.13) | — | 0.05 | 0.05 | 0.1 | (E) |

## 0.2 Ship gates (p50 / p99)

Rules:
- Each cell is p50 / p99, in ms unless stated.
- B applies to the B fixture (§1). C is [prov].
- Every foreground gate holds with one bulk worker active (index, find or save), and also with all three queued (§2.15).

| Gate | Metric: start → end | A | B | C [prov] | Binding resource; evidence; k | Design consequence |
|---|---|---|---|---|---|---|
| **G1** Typing | input ingress (evdev / raw-input timestamp) → present submitted containing the edit; any file ≤ 10 GB | **1.0 / 2** | **1.0 / 2** | 1.0 / 2 | UI-thread CPU queue + strip raster; (M)[AC] strip + (E); PB 0.6 / 0.5 / 1.07 (A after idle 1.53); A p99 slack 3.3× warm, 1.31× after idle. **C (E) PB exceeds the C p50 (k 0.94), and after idle (2.93) the C p99: flagged C risk, limit kept** | UI slices ≤ 0.5 ms with input checks; full-frame raster off the UI thread or GPU; C: damage-minimal strip (changed cells only) or GPU |
| **G2a** Optical, active typing | electrical key contact → photodiode 50 %, mid-screen | **31 / 50** | **76 / 100** | 71 / 95 | display chain; (E) + (M)[AC] strip; p50 ≈ 1.1 × PB (C: 1.08×, flagged), p99 ≥ mid-screen model max (37.2 / 89.3 / 84.1) | queue depth 1; render on input |
| **G2b** Optical, first key after ≥ 15 s idle | same | **47 / 65** | **80 / 100** | 75 / 95 | PSR / RC6 (E); after-idle strip factors 3.4× / 4.5× transferred from A frame measurements (E); p50 ≈ 1.1 × PB (C: 1.04×, flagged); p99 ≥ model max (52.0 / 92.2 / 90.7) | no idle-path work on keypress |
| **G2c** Added optical latency | **paired difference**: same injected key, same phase, editor − reference (§4) | **1 / 3** | **1 / 3** | 1 / 3 | UI / worker CPU queue and present queue (app-added only); (G) | no extra queued frame anywhere |
| **G3** Full frame, warm | ingress → compositor-ready (backend-specific, §2.2), incl. minimap; scroll / page-down / tab switch | **5.0 / 5.56** (= T/2) | **4.0 / 5.0** | 5.0 / 8.0 | SSE2 raster + XShm upload; (M)[AC] timings, composite totals (E); bottleneck attribution (E) pending perf counters; B: A hardware at B resolution, not the B fixture, so kept loose for single-channel DDR4 | A: GPU is the chosen path; measured MT4 XShm 4.78 is the warm fallback (slack 1.16×); ST 7.72 out |
| **G3z** Cadence | missed displayed refreshes in a 10 000-refresh scroll (displayed-frame IDs). **Hard maximum: applies to p50 and p99** | **0** | **0** | 0 | as G3; display refresh deadline | count by displayed-frame IDs; no frame queue; per-frame work ≤ T/2 |
| **G3i** First full frame after idle | as G3, first full frame after ≥ 15 s idle (isolated; a one-frame slip is allowed, a stutter is not) | **8 / 11.1** (= T) [prov: GPU wake unmeasured] | **10 / 16.7** (= T) | 10 / 16.7 | after-idle raster 3.4× ST / 1.7× MT4, upload 4.5× (M)[AC]; B-res factors 5.45× / 3.16× / 8× (M)[AC,A-hw]; B MT4 8.6 (k 1.16 / 1.94); C MT2 9.5 (E, k 1.05) | A: GPU is the chosen path; the measured MT4 XShm fallback misses this budget (12.1 > T); MT8 after idle not measured. B: MT4; B-res ST 16.4 fits the 16.7 p99 cap but not the 10 p50 |
| **G4a** Startup, software, warm | exec → CPU back buffer complete + map requested + input accepted | **25 / 40** | **38 / 55** | 67 / 90 | first frame = after-idle SSE2 MT4 raster (M)[AC] (A 5.4, B 3.6 A-hw); rest (M) + (E); k ≈ 2 (A 2.00 / 3.2; C 1.97 / 2.6, flagged) | no toolkit; embedded font; MT first frame; GPU init on worker |
| G4a cold | same, cache evicted and verified | **30 / 140** (+100 is an (E) APST allowance) | **55 / 80** | 86 / 120 | storage request latency; (E); k ≈ 2 (C 1.98) | ≤ 640 KiB touched before first frame |
| **G4c** First-install increment | **paired**: launch request → first present complete (PresentMon); editor − signed reference app, alternated, both first-run | — | **20 / 50** | — | Defender scan; (E) | small signed binary |
| **G5** Open, warm | open request → **correct** first viewport submitted; bounded prefix; any S and line length | **6 / 9** | **4 / 6** | 5 / 7 | full frame: A GPU frame 1.009 (E); B MT4 (E from (M)[AC,A-hw]); C 3.74 (E, k 1.34 / 1.87); ≈ 1.4× / 2× | publish before copy / index |
| G5 cold (also cold tab) | same, cold file | **10 / 110** (APST allowance) | **6 / 35** | 10 / 60 | 4 KiB random read tail; (E) | worker prefetch; no foreground major faults |
| **G6** Find, ordinary | request → exact count + first 4096 offsets published; 1 GB, cached file, new mapping (faults included); includes dense one-byte counts | **80 / 125** [AC, prov] | **125 / 200** | 315 / 500 | ST scan (M)[AC] 16 GB/s + new-mapping faults (E 15.3 ms/GB): PB 77.8 / 115.3 / 265.3; k A 1.03 / 1.61, chosen | SIMD compare + popcount; bounded output |
| G6v Find, adversarial verifier | same; periodic near-miss (needle `a`×31+`b` in 1 GB of `a`), and first/last-byte-defeating needles | **160 / 250** [AC, prov] | **250 / 400** | 630 / 1 000 | verifier compute; (E) ≤ 2× ordinary until measured; limits chosen | SIMD filter + verify with a **deterministic Two-Way fallback after bounded verification work**; Rabin–Karp alone is not worst-case linear |
| G6c Cancel | cancel → **logical** acknowledgement: generation invalidated, future publication suppressed, cancellation requested; the worker's next CPU slice ≤ 5 ms. A CPU-slice bound, not an I/O completion bound | **1 / 5** | **1 / 5** | 1 / 5 | worker CPU queue; (G) | buffers and handles stay alive until in-flight I/O completes; physical cleanup tracked separately (may exceed 5 ms, e.g. APST tail) |
| **G7** Index | open → index published (exact line numbers available), 1 GB; warm (cached file, new mapping) / cold | **80 / 125** [AC, prov] / **600 / 800** | **125 / 200** / **3 000 / 4 000** | 315 / 500 / 9 000 / 13 300 | ST scan + new-mapping faults / device; (M)[AC] + (E) / (E); PB as G6 | byte-offset scroll until published |
| G7j Unindexed jump | request → correct viewport at line 10⁷ submitted; warm / cold | **30 / 50** [AC, prov] / **250 / 350** | **50 / 80** / **1 200 / 1 600** | 125 / 200 / 3 500 / 5 300 | as G7 | cancellable worker scan |
| **G8s** Save ack | save request → snapshot taken + job enqueued + "saving" shown | **2 / 5** | **2 / 5** | 2 / 5 | UI-thread CPU queue; (G) | nothing synchronous on the UI thread |
| G8d Save complete | A: durable replace (fsync data + rename + fsync dir). B: **data-flushed atomic replacement acknowledged**. 1 MB / 1 GB warm-source / 1 GB cold-source | **10 / 50**, **1 500 / 2 500**, **2 200 / 3 000** | **45 / 250**, **3 800 / 6 000**, **6 800 / 9 500** | 120 / 400, 30 k / 40 k, 40 k / 55 k | flush + write; (E); ≈ 1.5–2.5× | B power-loss durability provisional (§2.8) |
| **G9** Edits | request → **correct** frame submitted: 1 MB paste; 10k-step undo | **5 / 15**; **63 / 84** | **5 / 15**; **26 / 35** | 8 / 20; 31 / 42 | memcpy + full frame (#2); tree latency (M)[bat], A only; ≥ 1.5× / 2× (C undo 21.7: 1.43× / 1.94×) | batch undo, render once |
| **G10** Memory, peak | deduplicated owned allocations: private anonymous + owned shared / graphics, counted once; incl. backend handoff (MB). **Hard maximum: applies to p50 and p99** | **87** | **44** | 31 | RAM; (E) driver; 1.5× | single CPU buffer at handoff |
| G10f Per file | unedited copy: ≤ 1.25 S + 64 KiB. Unedited mmap: ≤ 32 B·⌈S/64 KiB⌉ + 2 MB. Edits: ≤ 96 B/piece + 64 B/undo + 1.25 × (typed + deleted-original bytes) + live-snapshot path copies. **Hard maxima: apply to p50 and p99** | same | same | same | RAM; (E); ≈ 2× | inverse-op undo, not snapshots |
| **G11** Idle | CPU running time per blink, summed over all app threads (§4: Linux CLOCK_THREAD_CPUTIME_ID; Windows GetThreadTimes or ETW CSwitch). Wakeups ≤ 2/s blinking; 0/s after 10 s or unfocused | **0.1 / 0.2** | **0.1 / 0.2** | 0.2 / 0.4 | CPU running time; (E); 2–4× | event-driven loop; blink timeout |

**Tracked, not gated** (reported per run, with no limit):

| Item | Reported as |
|---|---|
| G4b startup on glass | photodiode. Provisional goal ≤ 100 ms (A/B) and ≤ 120 ms (C) with desktop effects OFF; effects ON also reported |
| Total first-install launch latency on B | whole launch time |
| Background completion times | when index + find + save run together |
| "B-slow" display class | optical results (§1) |
| Memory detail | clean mapped residency; page tables ≈ 1.95 MB per GB mapped; Windows private commit and working set, each reported separately |

## 0.3 Historical calibration and aspirations (illustrative)

| System | Keystroke→photon best / mean / worst (E) | Stated floor (E) | Ratio | Full-screen redraw |
|---|---|---|---|---|
| PC XT 4.77 MHz, CGA 60 Hz, WordStar | 3.5 / 16.0 / 35.7 | 12.6 | 1.27× | 4000 B; REP MOVSW 9+25n cyc = 50 009 cyc = 10.5 ms |
| 286 @ 12 MHz, VGA text 70 Hz, WP 5.1 | 2.4 / 12.3 / 24.5 | 11.2 | 1.10× | 4000 B over ISA at 2–5 MB/s = 0.8–2.0 ms |

- The historical floors are not derived from stated stages; §2.1's midpoint rule would give 12.99 ms for the 286.
- **Aspirations:** keystroke 13 / 25 ms (WP5.1 parity); startup and open 100 ms; find and index 1 s. These are not enforced (§0).
- **k = 1.1 on optical latency is an aspiration.** Its measurable form is G2c.

## 1. Targets

**A** is the measured laptop. **B** is a named fixture class. **C** is provisional.

| Item | A (00-profile) | B fixture (release) | C (provisional, (E)) |
|---|---|---|---|
| CPU | i7-1365U 2P+8E; 400 MHz floor; EPP balance_power; hwp_dynamic_boost 0 (M) | i5-12400 6P/12T, 4.4 GHz, 800 MHz min (spec) | 2C/2T Haswell/Broadwell Celeron or Atom-class, 1.5–2.0 GHz, no AVX/AVX2 |
| RAM | 32 GiB LPDDR5-6400, 102.4 GB/s peak | **1 × 16 GiB DDR4-3200 single-channel, 25.6 GB/s** | 4 GiB DDR3L-1600 single-channel, 12.8 GB/s |
| ST SIMD scan | **16 GB/s (M)[AC]** (SSE2 newline + ASCII 16.7–17.3; memchr 20.3–20.7); battery pending | 10 (E) | 4 (E) |
| MT scan | 23.2 (M)[bat], scalar | 18 (E) | 6 (E) |
| memcpy payload | 9.4 (M)[bat]; 12.5 warmed / 9.1 first burst after 2 s idle (M)[AC] | 7 (E) | 3 (E) |
| Raster / upload rate | memcpy payload as **(E) proxy** | 7 (E) | 3 (E) |
| Pointer chase | 1 GiB 371 ns, 8 MiB 115 ns, 1 MiB 16.7 ns (M)[bat] | 150 ns (E) | 180 ns (E) |
| Page cache → user | 4.2 GB/s (M)[bat] | 2.5 (E) | 1.5 (E) |
| Storage read / write | NVMe Gen4, 92 % full: 2.5 / 1.0 GB/s (E) | **2.5″ SATA DRAM-less TLC**: 0.50 / 0.40 (E) | eMMC 0.15 / 0.05 (E) |
| 4 KiB QD1 read; flush p50 / p99 | 100 µs; 2 / 20 ms (E). APST policy permits ≤ 100 ms exit (config) | 150 µs; 10 / 50 ms (E) | 500 µs; 20 / 100 ms (E) |
| GPU BW | Iris Xe 25 GB/s (E) | UHD 730 12 (E) | Gen7.5 6 (E); may be absent (VDI) |
| Display | 2880×1800 OLED 90 Hz, eDP, PSR | **23.8″ 1080p60 IPS, DP, standard preset (Dell P24xxH / HP E24 class); acceptance: processing ≤ 1 frame, measured** | 1366×768 60 Hz TN |
| Panel / scaler (E) | 0.1–1 ms / none | 5–15 ms / ≤ 1 frame | 2–8 ms / 1 frame |
| Keyboard | internal i8042 via EC; scan/debounce unmeasured, 0.5–6 ms (E) | **USB FS HID boot keyboard (KB216 class), 8 ms poll (verify bInterval)**; 1–6 ms (E) | as B |
| OS | Linux 6.8, Xorg, Muffin (Cinnamon, effects ON); HZ 1000; PREEMPT_DYNAMIC | Windows 11 24H2, DWM, Defender only | Windows 10 DWM or X11 |
| Editor RAM budget; copy threshold min(256 MiB, RAM/32) | 8 GiB; 256 MiB | 4 GiB; 256 MiB | 1 GiB; 128 MiB |

**B-slow:** a declared display class for monitors with processing > 1 frame (some reach 3). Its optical gates shift by +(n − 1)·T (+33.3 ms at n = 3); all other gates are unchanged.

**Layout:**

| | A | B | C |
|---|---|---|---|
| Cells (device px) | 18×36 | 8×16 | 8×16 |
| Text grid (cols × rows) | 142×47 | 220×64 | 150×45 |

Minimap: 120 logical px wide, 2 logical px per line.

## 2. Derivations

### 2.1 Keystroke → photon (G1, G2)

Stage model (E): key scan + transport + OS wake + **app** + latch wait + compositor (+1 frame, a *scenario*) + scanout to row + scaler + panel.
- The only (P) term is the beam wait: at a fixed refresh rate the mean wait for a fixed row is ≥ T/2.
- Means use range midpoints.

| Stage (ms) | A min / max | B min / max | C min / max | Basis |
|---|---|---|---|---|
| Key scan + debounce | 0.5 / 6 | 1 / 6 | 1 / 6 | (E) |
| Transport | EC→IRQ1 0.05 / 0.2 | USB 0 / 8 (mean 4) | 0 / 8 | (E) / (P) given bInterval |
| OS → app wake | 0.1 / 1.0 | 0.2 / 1.0 | 0.3 / 1.5 | (E); not bounded by HZ |
| **App** + server copy | 0.6 + 0.64 | 0.5 + 0.42 | 1.07 + 1.21 | (M)[AC] strip / copy; (E) rest and C |
| Latch wait + compositor + scanout to mid | T/2 + T + T/2 = 22.2 | 33.3 | 33.3 | (P) scanout; (E) compositor |
| Scaler | 0 | T | T | (E) |
| Panel | 0.1 / 1 | 5 / 15 | 2 / 8 | (E) |
| **Mean, mid-screen** | **27.9** | **69.0** | **65.7** | PB |
| Mid-screen min / max | 18.7 / 37.2 | 48.8 / 89.3 | 47.2 / 84.1 | PB |
| No compositor frame / no vsync wait | 16.8 / 11.3 | 52.4 / 44.0 | 49.0 / 40.7 | scenarios |

**First key after ≥ 15 s idle, all (E):**
- PSR exit: ≤ T, A only.
- Strip raster × 3.4 and server copy × 4.5: the measured first-frame-after-15 s ratios (M)[AC] (SSE2 ST 20.1/5.83; upload 6.3/1.4). Applying them to the strip (unmeasured after idle) and to B/C is a transfer, so it is (E). The measured 1080p *frame* factors are larger: 5.45× ST, 3.16× MT4, 8× upload. Applied to B's strip they would give an idle mean of 74.2 ms (p50 k 1.08 against 80; max 94.4 ≤ 100). They replace v3's 1.37 memcpy ratio. A: 0.40 × 3.45 + 0.15 + 0.64 × 4.5 = 4.41 ms, vs 1.24 warm.
- GPU / compositor wake: +0.5 (E).
- Result: 42.7 / 71.9 / 72.3. C: 0.76 × 3.45 + 0.31 = 2.93 ms app, plus 1.21 × 4.5 = 5.46 ms server.

**App PB A ≈ 0.6 ms app-side**, plus a 0.64 ms server stage:

| Item | Cost | Basis |
|---|---|---|
| Piece insert | 8 µs | (E) |
| Shape one line | 2 µs | (E) |
| **Raster line strip, 2880×36 px** (0.31–0.44 ms with both naive and SSE2 kernels: overhead-bound, so a better kernel does not help the strip) | **0.40 ms** | (M)[AC] |
| Request + flush (fire and forget) | ≈ 0.1 ms | (E) |
| **App-side PB** | **≈ 0.55, rounded to 0.6 ms** | |
| Server copy / round-trip of the strip | 0.64 ms | (M)[AC]; not waited on, but it precedes compositor-ready |

- v3's 0.3 ms (a 39 µs memcpy-proxy strip) was ~2× low.
- B: strip 0.38 + server 0.42 (M)[AC,A-hw]; app PB ≈ 0.5.
- C (E, C model): strip = 0.40 × 1366/2880 × 4 = 0.76 ms; server = 0.64 × 1366/2880 × 4 = 1.21 ms; app PB = 0.76 + 0.31 = 1.07 ms. This exceeds C's provisional G1 p50 (1.0), and after idle (2.93 ms) the p99 (2.0). That is a C design risk: C must raster only the changed cells, or use its GPU. The limit is kept.
- With the GPU path (chosen on A), the strip raster becomes a glyph-quad submission (µs) plus a GPU fence. That path is unmeasured.
- G1's 2 ms p99 now has **~3.3× slack warm and ~1.3× after idle** on A (app-side 1.5 ms after idle), not ~7×. That is why the ≤ 0.5 ms UI-slice rule matters.

**G1 starts at ingress,** so queueing counts.
- A single-threaded full redraw on the UI thread (5.8 ms SSE2 warm, 20.1 ms after idle on A, (M)[AC]) would hold queued keys for up to a frame or more, failing G1 outright.
- Hence: **wall-time UI slices ≤ 0.5 ms, with a pending-input check between slices.**
- Queue age (T1 − T0) is traced. Scheduler latency is not bounded by HZ or the preemption mode; G1's p99 is measured.

**G2c** owns the app-added part of the chain (§4 defines it as a paired difference). The WP5.1 aspiration is not met by the modeled default chain. Its reachability is measured with the reference renderer, including a `_NET_WM_BYPASS_COMPOSITOR` request, which is a request and not a guarantee.

### 2.2 Full frame and partial redraw (G3)

**Measured (M)[AC]** (00-profile Addenda 2 and 3). Setup: xcb + MIT-SHM; background fill plus glyph blend; upload timed to server copy completion; compositor not included.
- **Kernel:** the planned SSE2 4-px blend (16-bit math) with realistic 25 % atlas coverage.
- **Behaviour:** coverage-independent timings (100 % coverage: 5.59 vs 5.83 ms). The bottleneck attribution (memory traffic or overhead) is **(E) pending perf counters**: a branchless kernel would also give coverage-independent timings.
- **Naive scalar kernel:** v3.1's "compute-bound ≈ 10 cyc/px" (13.1–15.1 ms ST, Addendum 2) applies to the naive kernel only. It is not used.
- **Minimap rule (E):** the matching raster's ns/px × minimap pixels. A ST 0.49, MT4 0.260 warm / 0.449 idle; B-res MT4 0.071 / 0.223; C ST 0.41. GPU minimap allowance on A: **0.18 (E)**, giving a GPU frame of 0.829 + 0.18 = **1.009 (E)**. That one figure is used everywhere. All composite totals below are (E), derived from (M) parts.

| ms (raster + upload + minimap), totals (E) | A 2880×1800 | B-res 1920×1080 (A hardware) | C 1366×768 (C model, E) |
|---|---|---|---|
| ST warm (best) | 5.83 + 1.4 + 0.5 = **7.7** | 2.00 + 0.6 + 0.1 = **2.7** | 4.72 + 1.13 + 0.41 = 6.27 |
| ST first frame after 15 s idle | 20.1 + 6.3 + 1.7 = **28.1** | 10.9 + 4.8 + 0.7 = **16.4** | 16.3 + 5.10 + 1.43 = 22.8 |
| MT4 warm (best) | 3.12 + 1.4 + 0.26 = **4.78** | 1.13 + 0.6 + 0.07 = **1.80** | MT2: 2.36 + 1.13 + 0.21 = **3.70** |
| MT4 first frame after idle | 5.39 + 6.3 + 0.45 = **12.14** | 3.57 + 4.8 + 0.22 = **8.59** | MT2: 4.08 + 5.10 + 0.36 = **9.53** |
| MT8 warm | 3.16 + 1.4 + 0.26 = 4.82 | — | — |
| One line strip (raster + server) | 0.40 + 0.64 | 0.38 + 0.42 | 0.76 + 1.21 |
| GPU frame (unmeasured) | 0.829 + 0.18 minimap = 1.009 (E) | 0.69 (E) | 0.70 (E) |
| (P) fb write at peak BW | 0.20 | 0.32 | 0.33 |
| T/2 / T | 5.56 / 11.1 | 8.33 / 16.7 | 8.33 / 16.7 |

**After-idle penalty** for the first frame after ≥ 15 s idle, on the A panel: raster 3.4× ST and 1.7× MT4; server upload 4.5× (the server also wakes). At 1080p the measured factors are 5.45× / 3.16× / 8×. It replaces v3's 1.37× memcpy ratio and is the **dominant stated risk**.

**Decision on A: GPU is the CHOSEN path.** This is a design decision supported by AC results, not a proof that the CPU is impossible.
- **ST CPU is out.** SSE2 raster alone is 5.6–5.8 ms (= T/2); 7.72 ms with upload and minimap; 28.1 ms after idle.
- **Measured MT4 XShm fits warm:** 4.78 ms < 5.56, slack only **1.16×**. It is the warm fallback.
- **Measured MT4 XShm misses the idle budget:** 12.14 ms > T = 11.1 (G3i). Upload leaves 11.11 − 6.3 = 4.81 ms for raster plus minimap; MT4 needs 5.84. **MT8 after idle was not measured.**

**B-class 1080p:**
- ST fits warm (2.73 vs 8.33). After idle, 16.4 fits the G3i p99 cap (16.7) but not its 10 ms p50.
- **MT4** fits warm (1.80), and after idle 8.59 fits G3i (k 1.16 / 1.94). **The CPU path on B is MT4.**
- These measurements are A hardware at B resolution, not the B fixture. B has single-channel DDR4 (¼ of A's peak bandwidth), and the kernel's bottleneck is not yet attributed, so B's G3 is kept loose (4 / 5 ms) until the fixture is measured.

**C (E, C model):**
- "MT2 = ST/2" applies to **warm** raster only. Warm frame 3.70 (G3 k 1.35 / 2.16).
- After idle, the MT2 raster adopts A's MT4 idle factor 5.39 / 3.12 = 1.73 as a declared transfer (E): raster 2.36 × 1.73 = 4.08; frame 9.53 (G3i k 1.05 / 1.75); startup 34.1 / 43.4.
- Spread: applying ST/2 to the idle ST raster instead gives raster 8.14, frame 13.95, startup 38.1 / 47.5.
- ST: 6.27 warm / 22.8 after idle.

**T5, G3's endpoint:** render of the matching frame **complete on the device**, plus presentation submitted. Clocks are correlated, and the frame ID is carried from T0 to T6. T6 (displayed) is separate.

| Backend | T5 = both events, for the same frame ID |
|---|---|
| XShm | ShmCompletion marks source-buffer reuse only. **Device completion (accelerated / glamor):** issue an X Sync fence after the put (XSyncTriggerFence; it triggers only after the preceding rendering completes) and await it (XSyncAwaitFence / fence-notify event). T5 = the later of (fence observed triggered, presentation submitted). Present / MSC is T6 |
| GL / Vulkan | glClientWaitSync / vkWaitForFences on the frame's fence returned, then swap / present submitted |
| D3D11 / DXGI | D3D11_QUERY_EVENT for the frame signalled, then Present returned |
| GDI | GdiFlush() returned (explicit timestamp), plus the DWM frame ID |

Each backend is validated once against displayed-frame identity (X Present MSC; PresentMon displayed frames). The accelerated XShm path is validated *once* that way, and then *per frame* with the X Sync fence.

**Partial redraw.** One A line = 0.3–0.4 ms raster + 0.64 ms server (M)[AC]. A cursor cell is 2 592 B (A) or 512 B (B/C). Alternating buffers need buffer-age repair.

### 2.3 Startup (G4a)

**Endpoint:** CPU back buffer complete, map requested, input accepted. Upload and presentation are T4/T5 (§4), reported but not part of G4a. They wait for Expose, which with effects on is 57.8–67.5 ms (M)[AC]; that wait must not leak into G4a.

**A warm PB = 12.5 ms.** Startup *is* a first-frame-after-idle case, so the first frame uses the measured after-idle SSE2 MT4 raster. The 6.3 ms upload is excluded, per G4a's endpoint (it waits for Expose; T4/T5):

| Step | ms | Basis |
|---|---|---|
| exec | 0.83 | (M) |
| xcb_connect | 1.8 | (M) |
| 5 roundtrips | 1.5 | (E) |
| xkb keymap | 1.5 | (E) |
| Font raster | 1.5 | (E) |
| First full frame, SSE2 MT4 after idle | 5.39 | (M)[AC] |

**A cold PB = 14.9 ms:** warm + ⌈2 000 000 / 131 072⌉ = 16 requests × 100 µs + 2 MB ÷ 2.5 GB/s = +2.40 ms. The p99 gate also carries an **(E) APST allowance of +100 ms** taken from kernel policy. That allowance is not a measured bound.

**B:**
- Warm PB = CreateProcess 10 + window 3 + font 2 + MT4 after-idle raster 3.57 (M)[AC,A-hw] = **18.6 ms**.
- Cold PB = warm + 31 × 150 µs + 2 MB ÷ 0.5 GB/s = **27.2 ms**.

**C:**
- Warm PB = 20 + 6 + 4 + MT2 after-idle raster 4.08 (E, C model, A MT4 idle factor transferred; §2.2) = **34.1 ms**. With ST/2 of the idle ST raster instead: 38.1 / 47.5.
- Cold PB with **≤ 640 KiB touched** = warm + 10 × 500 µs + 655 360 B ÷ 0.15 GB/s = **43.4 ms**. Touching 2 MB would instead add 28.8 ms.

**Gates.** v3's A limits are kept (k 2.00 / 3.2). B was raised to k ≥ 2. C's limits are kept as provisional. **Retained C exceptions** under the C model: G1 0.94 / 0.68, G2a 1.08, G2b 1.04, G3i 1.05, G4a 1.97 / 1.98.

| Gate | A | B | C |
|---|---|---|---|
| Warm p50 / p99 | 25 / 40 (2.00× / 3.2×) | 38 / 55 | 67 / 90 (1.97× / 2.6×) |
| Cold p50 / p99 | 30 / 140 | 55 / 80 | 86 / 120 (1.98×) |

An ST first frame (20.1 ms after idle on A) would push A's PB to 27 ms; that is why the first frame is MT.

**GPU device creation** (E: 20 / 50 / 80 ms) runs on a worker. The first frame is drawn on the CPU.

**Toolkit (M)[AC].** Native GTK3: gtk_init 56.4–60.5 ms, first draw at 88–95 ms. gtk_init alone (56–60 ms) exceeds G4a's warm p50 of 25 ms, so the plan is no toolkit.

**Fonts.**
- `fc-match` costs 16–26 ms in a fresh process (M), so font discovery runs on a worker.
- The embedded font covers startup.
- A tofu/placeholder glyph is an *acknowledgement*, never a correct-content endpoint.

### 2.4 Open → first viewport (G5)

**Contract.** The first viewport is published from a bounded prefix read before any copy, index or validation runs. Copy mode and indexing are asynchronous. G5 ends at **correct** content: a placeholder does not stop the clock.

| | A | B | C |
|---|---|---|---|
| Warm PB = 64 KiB read + full frame (#2, chosen path) | **1.025** (GPU frame 1.009, (E)) | **1.83** (MT4, (E)) | **3.74** (MT2, (E)) |
| Cold PB, adds 1 data + ~3 metadata reads | **1.451** + (E) APST allowance | **2.56** | **6.18** |

**mmap states**, all benchmarked:
- **Cached file, new mapping:** about 15 259 fault-around faults per GB (≈ 15 ms at an (E) 1 µs each).
- **Populated mapping.**
- **Cold.**

Background gates specify their state (§0.2). There are no foreground major faults: the worker prefetches with read or madvise.

**Foreground slice contract** (fixes the v2 contradiction where 128 KiB × 5 ns = 0.655 ms exceeded the slice):
- Slices are wall-time ≤ 0.5 ms, with an input check between slices.
- ASCII decode ≤ **96 KiB per slice** (0.49 ms at 5 ns/B, (E)).
- Shaping ≤ **500 chars per slice** (at 1 µs/char, (E)).
- Lines longer than 64 KiB enter **long-line mode**: render from the nearest 4 KiB column checkpoint (built lazily by a worker), with approximate columns and no wrap shaping outside the window.
- A newline-free 10 GB file must pass G1, G3 and G5.

### 2.5 Index, scroll, jump (G7, G7j)

**Background pass = newline count + ASCII flag**, run single-threaded at the (M)[AC] 16 GB/s:

| | A | B | C |
|---|---|---|---|
| 1 GB warm, pure scan time | **62.5 [AC]** | 100 | 250 |
| 1 GB warm, mapped file (scan + 15.3 ms fault term, (E)) | **77.8** | 115.3 | 265.3 |
| 1 GB cold | 400 | 2 000 | 6 667 |

10 GB is linear: cold A 4 s, B 20 s, C 67 s.

**Full UTF-8 validation is lazy:**
- per displayed 64 KiB chunk, with a 3-byte carry across chunk boundaries;
- C at 0.5–1 GB/s (E) = 64–131 µs per chunk;
- ≤ 0.5 ms per frame on the UI thread;
- invalid bytes are kept byte-exact.

**Sparse index:** 16 B per 64 KiB (0.0244 % of S), so 10 GB needs 2.44 MB. A per-line u64 index at 40 B/line would need 2 GB.

**Lookup with the index:** ≤ 11 µs.
- 18 levels: 2.1 µs at the (M) L3 latency, or 6.7 µs pessimistic.
- Plus a ≤ 64 KiB scan: 2.39 µs resident (M) or 4.1 µs from DRAM.

**Without the index:** jumping to line N scans N × L̄ bytes. For 400 MB that is A 25 [AC] / 160, B 40 / 800, C 100 / 2 667 ms (warm / cold).
- Scroll position is byte-based until the index is published, and line numbers are marked approximate.
- Jump to end is O(1).

### 2.6 Insert / delete (inside G1)

**Node layout (assumed).** A 16-way B+ node is 384 B = 6 lines:
- byte counts: 16 × 8 B = 2 lines;
- newline counts: 2 lines;
- child pointers: 2 lines.

Fields are stored SoA, keys first. One descent level costs pessimistically **2 dependent misses** (byte-count lines, then the selected child-pointer line), with up to 4 more line fetches running in parallel.

**Insert PB:**
- Traversal: 5 levels × 2 × 371 ns = 3.7 µs. This is a cold-metadata scenario; 115 ns at L3.
- Split of an indexed piece: exact counts via a ≤ 64 KiB scan, 4.1 µs [AC].
- Split of an unindexed piece: counts marked lazy.
- Count updates use lines already fetched.
- Pool allocation: 0.1 µs (E).
- **A PB ≈ 8 µs; budget 50 µs.**

**Memory per piece** ≈ 24 B leaf entry + 384 B / 16 / 0.5 fill = 48 B amortized internal ≈ **72 B ≤ 96 B** (G10f).
- Persistent path copies exist only while a snapshot is live: 5 × 384 = 1 920 B per edit made during that time. They are released with the snapshot and counted in G10f.
- The add buffer is chunked into 64 KiB blocks and never reallocated.

**Gap buffer, for reference.** At a 0.5 ms edit budget, the gap can move 4.7 / 3.5 / 1.5 MB. The piece table is chosen because files can exceed RAM, snapshots are O(1), and one code path serves all sizes; the crossover is not the reason.

**1 MB paste** = memcpy 0.11 + count 0.06 + insert + full frame incl. minimap = **1.186 ms** on A with the 1.009 GPU frame (E); B 2.05 with MT4; C 4.30 with MT2 (E).

### 2.7 Find (G6, G6v, G6c)

**Definition.** Exact non-overlapping count plus the first 4096 offsets, streamed. Further pages are fetched lazily. Result memory ≤ 64 KiB.

**Ordinary case** (which includes dense one-byte counting):
- Algorithm: SIMD compare, then popcount for counting, and a candidate filter for longer needles.
- Cost: S ÷ R_scan on one worker = 62.5 [AC] / 100 / 250 ms per GB.
- An all-`a` file searched for `a` costs the same: once the first 4096 offsets are recorded, the remaining matches are counted by popcount, not enumerated.
- Gate 80 / 125 (chosen): 1.28× / 2× the pure scan PB of 62.5, or 1.03× / 1.61× the new-mapping PB of 77.8 (+15.3 ms of faults, (E)). A tighter 65 / 90 would *not* need MT; it was declined as too little slack for an [AC]-based PB.

**Adversarial verifier** (G6v). Periodic near-miss patterns have zero matches but heavy verification, e.g. `a`×31 + `b` in all-`a`, or needles that defeat a first/last-byte filter.
- Algorithm: a SIMD candidate filter plus verification, with a **deterministic linear fallback (Two-Way)** once verification work exceeds a bound (e.g. verified bytes > 2 × bytes scanned). SIMD Rabin–Karp plus verification is *not* worst-case linear: hash collisions give O(S × needle).
- Budget (E): ≤ 2× ordinary until measured; G6v limits are chosen.

**Find-as-you-type.** First match = d_first ÷ R + 1 frame. Each keystroke cancels the previous generation (G6c).

**Cancellation during cold I/O:**
- Bulk workers use io_uring or overlapped I/O in chunks whose *transfer* time is ≤ 5 ms: 1 MiB on A (0.42 ms) and B (2.1 ms); 512 KiB on C (3.5 ms). Transfer time does not bound wake or tail latency.
- **G6c is a logical acknowledgement:** the generation is invalidated, future publication is suppressed, and cancellation is requested (IORING_OP_ASYNC_CANCEL / CancelIoEx), all within the worker's next CPU slice of ≤ 5 ms.
- Buffers and handles stay alive until each in-flight request completes or reports cancelled. Some requests are not cancellable. Physical cleanup is tracked separately and may exceed 5 ms (e.g. an APST wake).

### 2.8 Save (G8s, G8d)

**Ack.** Take the root snapshot, enqueue the job, show "saving".

**PB formulas:**
- Warm source: max(S ÷ R_pc, S ÷ R_write) + flushes.
- Cold source: S ÷ R_read + S ÷ R_write + flushes.

| PB (ms) | A | B | C |
|---|---|---|---|
| 1 MB (p50 flush inputs) | 5 | 22.5 | 60 |
| 1 GB warm source | 1 004 | 2 520 | 20 040 |
| 1 GB cold source | 1 404 | 4 520 | 26 707 |

- The gate tails are (G) budgets. They are checked against the measured whole-save distribution, not built by adding per-flush p99s: two p99 inputs give only a p98 guarantee. Example: two disjoint 1 % tails of 100 ms give p99(sum) = 120 ms, not 40.
- **Linux:** temp file → fsync → rename → fsync(dir). This is "durable".
- **Windows (B):**
  1. Keep the original open with FILE_SHARE_READ | FILE_SHARE_DELETE.
  2. Write the temp file, then FlushFileBuffers.
  3. Call SetFileInformationByHandle(FileRenameInfoEx, POSIX_SEMANTICS | REPLACE_IF_EXISTS).

  The endpoint is "**data-flushed atomic replacement acknowledged**". Durability of the rename across power loss is **provisional**: there is no directory flush, and a volume flush needs admin rights. The UI says "saved" at that endpoint, and §5 states the gap.

### 2.9 Undo / redo (G9)

- **Single step:** an inverse piece op, 4–8 µs, plus a partial redraw.
- **10k steps:** batch-apply, then render once. 10⁴ × 10 misses × latency = 37.1 / 15.0 / 18.0 ms, plus a full frame including the minimap (A GPU 1.009 (E); B MT4 1.80; C MT2 3.70) = **38.11 / 16.8 / 21.7** ms. Gates: A 63/84 (1.65× / 2.2×); B 26/35 (1.55× / 2.08×); C 31/42 (1.43× / 1.94×).
- B is faster than A because of A's measured 371 ns latency.

### 2.10 Tab switch (G3)

- Cost: pointer swap + full frame + minimap (#2): A GPU frame 1.009 (E), CPU MT4 4.78 fallback; B MT4 1.80; C MT2 3.70 (E). No per-tab bitmaps are kept.
- An evicted viewport falls under G5 cold.
- The test corpus includes 100 tabs.

### 2.11 Minimap

| | A | B | C |
|---|---|---|---|
| Lines shown | 450 | 540 | 384 |
| Full re-raster (E, ns/px rule) | SSE2 ST 0.49; MT4 0.26; GPU allowance 0.18 | MT4 0.071 | ST 0.41 |
| Cache, 3 viewports tall | 5.18 MB | 1.56 MB | 1.11 MB |

- Each edit re-rasters one strip of ≤ 3.8 KB.
- The whole-file density view is built from the sparse index.
- A stale or blank sidebar fails G3.

### 2.12 Memory (G10)

**Baseline PB (E)** = 2·fb + minimap cache + 2 MB binary + 1 MB atlas + 8 MB driver = 57.7 / 29.1 / 20.5 MB. Caps are 1.5× rounded up: **87 / 44 / 31 MB**.

**Handoff peak:** with a single CPU buffer it is 78.4 / 37.4 / 24.7 MB.

**What is counted.** One deduplicated owned-allocation total: private anonymous memory plus owned shared or graphics allocations, each counted exactly once.
- Windows private commit and working set are *reported separately*, never summed (summing double-counts resident private pages).
- Clean mapped residency is page cache, not a second copy. It is reported, not gated.

**G10f fixture:** 10⁵ edits, 2×10⁵ pieces and 10⁵ undo records ≤ 25.7 MB, plus any live-snapshot path copies. Unedited mmap overhead: 1 GB → 2.49 MB; 10 GB → 6.88 MB.

**Capacity** within the RAM budget minus the baseline cap:

| | A | B | C |
|---|---|---|---|
| 1 MB files | 6 463 | 3 231 | 792 |
| 100 MB files | 67 | 33 | 8 |
| 1 GB files, mmap | 3 417 | 1 708 | 419 |

In practice the limit is RLIMIT_NOFILE / handles. Copy-mode files do not keep an fd open.

### 2.13 Idle (G11)

- Event-driven only: poll / MsgWaitForMultipleObjects, plus inotify / ReadDirectoryChangesW.
- Blink ≤ 2 wakeups/s. It stops after 10 s, or when the window is unfocused.
- Per-blink PB ≈ 0.05 / 0.05 / 0.1 ms of thread CPU time (E).
- Compositor work and PSR exit are outside the app; they are the reason for the timeout.

### 2.14 Original-file consistency

**(a) Files below the copy threshold.** The private copy *is* the snapshot, so external writers cannot affect it.

**(b) mmapped originals have no frozen snapshot.** Overwritten bytes are gone before any notification arrives, so v2's "freeze by copying after notification" remedy is withdrawn.

Policy for (b):
- **Detection:** inotify (MODIFY, ATTRIB, CLOSE_WRITE, DELETE_SELF, MOVE_SELF) or ReadDirectoryChangesW, plus a size/mtime/inode check on save, on focus, and before any bulk operation.
- **On a detected change:**
  - the buffer is marked "source changed on disk";
  - in-flight find, save and index over the original are cancelled (G6c);
  - the user chooses: reload, or keep the edits over the new original.
- **Undo of deleted original bytes** stays byte-exact because deleted ranges are copied into the add buffer at delete time: O(deleted bytes), 1 MB = 0.11 ms on A; larger deletes run on a worker.
- **Truncation:** a SIGBUS handler converts it into the same "source changed" state.
- **Windows** can deny write-sharing while the file is mapped. That is a product choice, not the default.

**Residual gap:** a change landing between the last check and a read is undetected until the next check (§5).

### 2.15 Concurrency

A's CPU path at 90 Hz already moves 13.06 GB/s (E) before any search: raster + upload 7.46, compositor 3.73, scanout 1.87.

- One bulk worker is the plan; MT scan is an option to be measured.
- Worker priority does not reserve DRAM bandwidth.
- G1, G3 and G2c **must be verified** with index, find and save all queued and one active (each progressing in ≤ 5 ms chunks). Background completion times in that mix are tracked, not gated.

## 3. Chosen architecture (motivating number)

- **Rendering:**
  - A: **GPU is the chosen path**, because the measured MT4 XShm fallback misses the idle budget (12.14 > T = 11.1; MT8 after idle not measured). MT4 CPU (4.78 warm, slack 1.16×) is the warm fallback; ST CPU (7.72) is out.
  - B: MT4 CPU path (1.80 warm / 8.59 after idle, A hardware at B resolution). ST after idle (16.4) fits only G3i's p99. GPU where present.
  - C: MT2 CPU fits G3 / G3i only marginally (E, C model). Typing on C needs damage-minimal strips or the GPU (G1 risk). A CPU path is required to exist for VDI.
  - (P) does not force either path. The dominant risk is the power-state wake (upload 4.5× A, 8× at 1080p); the kernel's bottleneck attribution is (E).
- **UI thread:** wall-time slices ≤ 0.5 ms with input checks; ≤ 96 KiB decode or ≤ 500 shaped chars per slice (G1).
- **Present:** render on input; queue depth 1; waitable DXGI swapchain with max latency 1; the GDI/XShm queue measured via G2c.
- **Buffer:** piece B+ tree (SoA 384 B nodes), chunked add buffer, lazy counts, pooled nodes. Chosen for files larger than RAM and O(1) snapshots.
- **Files:**
  - bounded-prefix first viewport;
  - async copy below min(256 MiB, RAM/32);
  - mmap + worker prefetch above that;
  - no foreground major faults;
  - §2.14 policy.
- **Indexing:** sparse 16 B / 64 KiB; newline + ASCII pass at (M)[AC] 16 GB/s; lazy validation; byte-offset scroll until published.
- **SIMD:** SSE2 baseline. SSE2 16.7–17.3 GB/s ≥ AVX2 15.5–16.2 GB/s (M)[AC] on the scan; AVX2 is optional.
- **Search:** popcount counting, bounded output, linear-time verifier.
- **Toolkit:** none (GTK3 init 56–60 ms (M) > G4a p50).
- **Linking:** static linking; dlopen GL on a worker; binary ≤ 2 MB; ≤ 640 KiB touched before the first frame.
- **Fonts:** embedded font + pre-baked ASCII atlas (61.6 KB on A, 12.2 KB on B/C); discovery runs async.
- **Threads:** UI + 1 bulk worker (async chunked I/O) + raster pool on A.
- **Memory:** 87 / 44 / 31 MB peak; no per-tab framebuffers; undo as inverse ops.
- **Idle:** event-driven; blink timeout.

## 4. How each gate is measured

**Timestamps** use CLOCK_MONOTONIC on Linux and QueryPerformanceCounter on Windows, written to a preallocated ring:

| # | Event | Notes |
|---|---|---|
| T0 | ingress | evdev timestamp via a tracer with EVIOCSCLOCKID = MONOTONIC, correlated by key sequence (X event time is ms-resolution); Windows: raw-input / ETW Win32k |
| T1 | dequeue | |
| T2 | mutation done | |
| T3 | CPU / GPU render done | |
| T4 | present submitted | |
| T5 | render of the matching frame complete on the device + presentation submitted | §2.2 per backend (XShm: later of X Sync fence triggered and presentation submitted); correlated clocks, frame ID |
| T6 | present complete | X Present CompleteNotify UST/MSC; PresentMon displayed frames and presentation mode |

Gate mappings: G1 = T4 − T0; G3 = T5 − T0; G4a ends before T4.

**Optical measurement:**
- Photodiode at the mid-screen cursor row, sampled at ≥ 20 kHz, threshold at 50 %, both dark→light and light→dark.
- Start = electrical key contact (switch tap, or a solenoid with measured contact time). Real keyboards only; A uses its internal keyboard.

**Paired differences (G2c, G4c).**
- Subtracting marginal percentiles is invalid. Counterexample: the reference is 60 % at 20 ms and 40 % at 40 ms; the editor adds 11.1 ms to 5 % of the fast cases. Both distributions keep p50 = 20 and p99 = 40, so the subtraction shows 0, while the true added-latency p99 is 11.1 ms.
- Method: each injected key event (or launch) is run against the editor and the reference window alternately, under matched phase. Injection happens at a fixed offset after vblank, taken from the Present MSC/UST or PresentMon timestamps, and the offset is randomized across the frame between pairs.
- The gate applies to p50 / p99 of the per-pair differences, with N ≥ 10 000 pairs (G4c: 1 000 pairs).

**Reference renderer:** a bare xcb (A) or Win32 (B) window that draws the typed glyph through the same present path and on the same display.

**Statistics:**
- ≥ 10 000 samples per interaction scenario; 1 000 launches per startup condition.
- Nearest-rank p50 / p99, with n and the confidence interval reported. Failures and timeouts are never excluded.
- Zero misses out of 10 000 refreshes gives a one-sided 95 % upper bound of 0.03 %.

**Conditions:**
- **A:** on battery, stock powersave governor, EPP and PSR enabled, Cinnamon defaults, disk at its real fill level.
- **B:** the fixture.
- **Cold:** caches evicted. Linux: `drop_caches`, verified with fincore/mincore and I/O counters. Windows: RAMMap empty standby or reboot, verified with ETW hard faults.
- **Idle:** ≥ 15 s with no input.
- **Concurrency:** as in §2.15.

**Memory:**
- Linux: smaps classes (anonymous private, owned SHM, file-clean) plus DRM fdinfo.
- Windows: private commit, working set and GPU allocations from ETW, reported separately. The gate uses the deduplicated owned total.
- Peak taken over startup and backend handoff.

**Idle CPU (G11)** is CPU running time in time units, summed across **all app threads** and attributed per blink.
- Linux: CLOCK_THREAD_CPUTIME_ID (ns), read per thread around each blink.
- Windows: GetThreadTimes user + kernel durations. Its effective resolution is ~15.6 ms, so aggregate ≥ 1 000 blinks and divide for the mean. Per-blink p99 needs ETW CSwitch tracing to attribute running time to each blink across threads.
- QueryThreadCycleTime is a secondary indicator only, and is never converted to ms.

**Corpus.** Every result is checked for correctness; a placeholder or stale minimap fails.
- ASCII code
- Unicode / bidi
- Malformed UTF-8
- Dense short lines
- A 10 GB single-line file
- 10⁵-edit history
- Dense and periodic search patterns
- 100 tabs

## 5. Assumptions and known gaps

1. **First follow-up: the battery rerun of the A SIMD scan.** It is queued; the machine is currently charging. A's G6, G7 warm and G7j warm gates stay provisional [AC] until then. If R_bat < 16 GB/s, only the **scan component** is rescaled. For a mapped 1 GB file on A the PB becomes 1000 / R_bat + 15.259 ms (not 77.759 × 16 / R_bat), and the gates are re-derived. The fault term is re-estimated separately once measured.
2. **Raster follow-ups** (Addenda 2–3 were measured on AC):
   - battery rerun of all raster and upload numbers, including the after-idle ones;
   - the GPU path's first frame after idle, including device/RC6 wake and fence, which is unmeasured and is the basis of A's G3, G3i and G5;
   - the same measurements on the B fixture itself, since the 1080p numbers are A-hardware proxies and the kernel's bottleneck attribution remains (E) pending counters;
   - perf counters on the SSE2 kernel to attribute its bottleneck (memory traffic vs overhead), currently (E);
   - **upload wake (4.5× A, 8× at 1080p; the dominant after-idle risk):** test whether a persistent-mapped GPU buffer or DRI3 present avoids the server-side copy wake.
   - The SSE2 kernel and realistic coverage are measured (Addendum 3) and are no longer follow-ups.
3. **Presentation stages are (E):** Muffin, DWM mode, scaler, panel, PSR, RC6. The 3.4× (ST) / 1.7× (MT4) / 4.5× (upload) after-idle ratios are (M)[AC] on A; transferring them to B and C is (E).
4. **Key scan / debounce is unmeasured** on every target.
5. **The APST +100 ms is an (E) allowance** taken from kernel policy. It is not measured I/O latency.
6. **B fixture SKUs** are to be recorded at purchase, and the monitor's ≤ 1 frame processing verified.
7. **Windows rename durability** across power loss is unverified: no directory flush, and a volume flush needs admin rights (§2.8).
8. **mmap:** the window between check and read (§2.14).
9. **C is a design constraint only.** VDI/RDP latency is outside this model.
10. **Syntax highlighting is out of scope.** If it ships, it must fit G1, G3 and G9 (visible-only ≈ 70 µs (E)).

## 6. Rejected review points

| Point | Decision |
|---|---|
| Round 1: bind A's ST scan at 6.3 GB/s | **Rejected.** Superseded by the SSE2 measurement of 16.7–17.3 GB/s; 6.3 was a compute-bound scalar loop. |
| Round 1: one-worker find at 317 ms/GB | **Rejected**, same reason. |
| Round 1: remove C | **Partly rejected.** C is kept as provisional (Tobias's scope). |
| Round 1: on-glass startup gates on A | **Rejected as gates.** The WM map takes 58–68 ms (M). On-glass startup is now tracked, not gated. |
| Round 1: G2 as "+2 ms mean, +1 refresh p99" | **Superseded** by the stricter paired G2c. |
| Round 1: highlighting inside the derivations | **Rejected** on scope (§5.10). |
| Round 1: L3 latency instead of 371 ns | **Kept pessimistic.** The figure is labelled a cold-metadata scenario. |
| **Round 2 F1: interim 200 / 320 ms per GB (6.3 GB/s) budgets** | **Rejected.** The reviewer concedes 6.3 is not a ceiling for SIMD; a 2.5× pessimism factor is not evidence. Accepted instead: [AC] tags in the gate table, provisional status, and the battery rerun as the first follow-up. |
| Round 2 F8: "65 / 90 necessarily assumes MT" (v2's wording) | **Conceded.** 65 / 90 is single-thread feasible; 80 / 125 is a chosen 1.25× / 2× slack. |

## 7. Changelog

**v1 → v2**

1. Evidence tags (P)/(M)/(E)/(G), planning bounds, and removal of impossibility claims resting on (E).
2. Gates given p50 / p99; A and B made release gates; the B fixture and B-slow class defined; C made provisional.
3. Measurement method section added.
4. Arithmetic: A 27.0, B 68.3; 286 ratio 1.10.
5. Arithmetic: undo includes the redraw.
6. Arithmetic: tab switch includes the minimap.
7. Arithmetic: request counts use ceilings; C startup reads capped at 640 KiB.
8. Arithmetic: units and capacity.
9. G1 measured from ingress.
10. G2 scenarios, the idle row and G2c added.
11. G3: 1.21× slack, 400 MHz case, endpoints.
12. G4 split; WM map measurement; native GTK3 numbers.
13. G5: bounded prefix, long lines, APST, mmap states.
14. Scan rate 16 GB/s.
15. UTF-8: the background pass is newline + ASCII only.
16. G6: bounded output and cancellation.
17. G10 split into its components.
18. mmap consistency section.
19. Save: ack / durable split; Windows recipe.
20. Piece-tree split cost and gap-buffer crossover.
21. Concurrency section.
22. Fontconfig moved to a worker.
23. Keyboard mean penalty and start point.

**v2 → v3**

- **F1:** [AC] tag on the A G6 / G7 / G7j cells, which are provisional until the battery rerun (first follow-up). 16 GB/s is kept; the 6.3-based interim budget is rejected (§6).
- **F2:** transferred 1.37 ratio tagged (E). The A rendering decision is now conditional on the after-idle battery measurement, with GPU the default on A. The idle-mean ramp applies to all targets. The raster addendum hook is noted (not yet present).
- **F3:** G2c and G4c redefined as paired differences under matched phase, with N ≥ 10k (1k launches). The percentile-subtraction counterexample is stated.
- **F4:** G4a ends at the CPU back buffer + map request + input accepted. PBs are 9.34 / 16.18 / 31.40 warm and 11.74 / 24.83 / 40.77 cold. The limits are kept (k ≥ 2.0).
- **F5:** the post-notification copy remedy is removed. Policy: (a) the copy is the snapshot; (b) for mmap, detect, cancel and let the user choose; the residual window is listed as a gap.
- **F6:** B's G8d relabelled "data-flushed atomic replacement acknowledged"; power-loss durability is provisional.
- **F7:** composite tails are (G) budgets on whole-operation distributions; the p98 union-bound note is added; APST is an (E) allowance.
- **F8:** dense one-byte counting falls under the ordinary G6 (the 4 200 ms row is dropped); a G6v adversarial-verifier row is added; 65 / 90 is conceded as ST-feasible.
- **F9:** wall-time slices; 96 KiB decode or 500 shaped chars per slice; placeholders never stop the clocks; acknowledgement vs correct-content endpoints.
- **F10:** binding-resource / evidence and design-consequence columns added; G6v has a p50; tracked-not-gated table added; endpoints defined for G4c, G7, G7j and G9; mapping state given per background gate; async chunked I/O for cancellation; blink measured as thread CPU time.
- **F11:** Windows commit and working set are reported separately; the gate is a deduplicated owned total.
- **F12:** undo gates 63/84, 26/35, 31/42; paste 4.77 ms; G10 A cap 87 MB (capacity counts consistent); RAM in GiB; 384 B SoA node with 2 dependent + 4 parallel fetches; path copies counted; §2.15 reference fixed; "must be verified" wording; C cold jump p99 5 300 (2×).

**v3 → v3.1** (naive scalar raster + XShm upload, Addendum 2)

- §2.2: memcpy proxy replaced by measured naive-kernel numbers (compute-bound, ≈ 10 cyc/px); GPU required on A.
- New G3i.
- After-idle 1.37× replaced by 2.6× raster / 4.5× upload.
- App PB 0.3 → 0.6 + 0.64 ms server stage; G1 p50 → 1.0.
- G2a / G2b p50 raised.
- G4a limits raised: A 44/65; B 41/60; C 88/120.
- G5 / G9 on B/C loosened.

**v3.1 → v3.2** (SSE2 blend kernel, 25 % coverage, Addendum 3)

- §2.2 rebuilt on the SSE2 kernel. It is coverage-independent and memory-traffic/overhead bound: A ST 5.6–5.8 ms warm (= T/2), 20.1 after idle; MT4 3.1 / 5.4; MT8 3.1. "Compute-bound ≈ 10 cyc/px" now applies to the naive kernel only.
  - A's decision stands on the better kernel: GPU required (MT4 + upload after idle 11.9 > T); MT4 is the warm fallback (4.6 < T/2); ST is out.
  - The after-idle penalty (3.4× ST / 1.7× MT4 raster, 4.5× upload) is the dominant risk.
- G3 B back to 4 / 5 (MT4 PB 1.8; kept loose for single-channel DDR4). G3 C back to 5 / 8. G3i B 13 → 10 / 16.7 (PB 8.5); G3i C 10 / 16.7; A's G3i stays provisional on GPU, and the CPU fallback misses it.
- G4a PBs use the SSE2 MT4 after-idle first frame, upload excluded per the endpoint: A 12.5 / 14.9, B 18.6 / 27.2, C 33.5 / 42.9. Limits back to ≈ 2 × PB: A 25/40, 30/140 (v3 values); B 38/55, 55/80; C 67/90, 86/120. No-toolkit holds (gtk_init 56–60 > 25).
- G1 unchanged at 1.0 / 2: the strip (0.3–0.4 ms) is overhead-bound and independent of kernel. After-idle app-side becomes 1.5 ms (3.4× strip ratio), so the p99 slack after idle is 1.3×.
- G2a unchanged (31 / 76 / 71). G2b → 47 / 80 / 75 (after-idle strip ratio 3.4×).
- G5 and G9 restored to v3 values on B/C (B 4/6, 6/35; 5/15, 26/35. C 5/7, 10/60; 8/20, 31/42).
- §5: kernel and coverage follow-ups removed (measured). Added: the upload-wake test (persistent-mapped GPU buffer / DRI3 present). Battery rerun and GPU first-frame-after-idle kept.

**v3.2 → v3.3** (round-3 verification `02-review-verify3.md`; no gate limit changed)

- **R1:** T5 = matching-frame render complete on the device + presentation submitted, with correlated clocks and frame IDs. Per-backend events: XShm ShmCompletion (glamor readiness via Present / MSC); GL / Vulkan fence wait; D3D11 event query; GDI GdiFlush. T6 stays separate.
- **R2:** G11 measured as CPU running time across all app threads. Linux CLOCK_THREAD_CPUTIME_ID; Windows GetThreadTimes aggregated over ≥ 1 000 blinks, plus ETW CSwitch for the per-blink p99. Cycle counts are not converted to ms.
- **R3:** one GPU frame PB for A, 1.009 (E) (0.829 + 0.18 minimap), used everywhere: open 1.025 / 1.451, paste 1.186, undo 38.11. MT minimap by the ns/px rule: A MT4 4.78 warm (slack 1.16×) / 12.14 idle; B-res MT4 8.59 idle. Composite totals labelled (E).
- **R4:** "GPU required" became "GPU is the chosen A path". The measured MT4 XShm misses G3i; MT8 after idle is unmeasured; B-res ST after idle fits the p99 cap but not the p50. Bottleneck attribution is (E), with the branchless-kernel caveat.
- **R5:** one C model (A-panel measurement × pixel or width ratio × 4, A-panel idle factors). C strip 0.19 → 0.76, C app 0.5 → 1.07 (+1.21 server), C mean 64.3 → 65.7, C idle 67.4 → 72.3, C MT2 2.8 / 6.6 → 3.70 / 9.53, C startup 33.5 / 42.9 → 34.1 / 43.4, C open 2.8 / 5.2 → 3.74 / 6.18, C paste 3.4 → 4.30, C undo 20.8 → 21.7. Compositor-bypass rows corrected (A 16.8 / 11.3, B 52.4 / 44.0, C 49.0 / 40.7). 1080p frame factors (5.45 / 3.16 / 8) stated, with a B sensitivity note. **Flagged C risks, limits kept:** G1 p50 (k 0.94) and after-idle p99 (2.93 > 2); G2a k 1.08; G2b k 1.04; G4a k 1.97.
- **R6:** G6c is a logical acknowledgement within one ≤ 5 ms worker CPU slice. It is not an I/O completion bound; buffers live until I/O completes; cleanup is tracked separately.
- **R7:** G6v requires a deterministic Two-Way fallback after bounded verification work. Rabin–Karp alone is not worst-case linear.
- **R8:** G3z, G10 and G10f marked as hard maxima for both percentiles. G3z consequence filled. Resources named for G2c / G6c / G8s (CPU queues, (G)) and G10f / G11 (RAM, CPU running time). (M)[bat] tree latency scoped to A; (M)[AC,A-hw] defined as A hardware at B resolution.
- **Also (verifier table):** find and index PBs include the new-mapping fault model (+15.3 ms/GB): 77.8 / 115.3 / 265.3. A G6 k 1.03 / 1.61; the limits are kept.

**v3.3 → v3.4** (round-4 verification `02-review-verify4.md`; no gate limit changed)

- **R1 residual:** accelerated XShm device completion is now an X Sync fence after the put (XSyncTriggerFence, then XSyncAwaitFence or fence-notify). T5 = the later of fence triggered and presentation submitted. ShmCompletion marks source reuse only; Present / MSC is T6. The path is validated once against displayed-frame identity, then per frame with the fence (§2.2, §4).
- **R5 residual:** C's "MT2 = ST/2" applies to warm raster only. The after-idle MT2 raster uses the transferred A MT4 factor 5.39 / 3.12 (E), which keeps 4.08 / 9.53 / 34.1 / 43.4. The alternative spread (8.14 / 13.95 / 38.1 / 47.5) is shown. §2.3 now names the retained C k exceptions.
- **Battery rescaling:** only the scan component rescales (1000 / R_bat + 15.259 for a mapped 1 GB file on A). §2.5 shows pure-scan 62.5 / 100 / 250 beside mapped-file 77.8 / 115.3 / 265.3.
- **Minor:** §5.2 now says bottleneck attribution remains (E) pending counters; §2.1 B sensitivity max 94.3 → 94.4.
