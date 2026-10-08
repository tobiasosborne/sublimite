# 01 — Performance target, v2

v2 replaces v1 (archived as `01-perf-target-v1.md`) after three adversarial reviews (`02-review-*.md`) and the measured addendum in `00-hardware-profile.md`.
- Gates for **A** and **B** are *release* gates.
- Gates for **C** are **PROVISIONAL [est]** design constraints. They are not enforced in CI until a real C SKU exists and has been measured.

## 0. Method and evidence tags

Every number carries one of these tags:

| Tag | Meaning | Can it prove impossibility? |
|---|---|---|
| **(P)** physics bound | unavoidable traffic ÷ theoretical peak, or display geometry (refresh, scanout position) | yes |
| **(M)** measured primitive | measured on Target A (00-hardware-profile). `[bat]` = battery/powersave, `[AC]` = charging/powersave | no, but binding as a planning input |
| **(E)** estimate | pipeline stage or rate not measured; the pessimistic end of a plausible range | no |
| **(G)** gate / budget | an engineering choice: a multiple of a planning bound, or a perceptual aspiration | — |

- **Planning bound (PB):** a time computed from (M) and (E) inputs. It takes the tag of its weakest input. A PB is *not* a lower bound. It is the expected cost of a competent implementation on the worst configuration we commit to.
- **Gates:** each gate has p50 and p99 limits, and each gate is k × PB with k stated.
- **Perceptual aspirations:** these are listed beside the gates. An aspiration is promoted to a gate on a machine once the **minimal reference renderer** (§4) has been *measured* to meet it there. Reachability is decided by measurement, never by an (E) chain.
- **Units:** MB/GB are decimal; KiB/MiB/GiB are binary. T = frame interval, exact (1000/90 ms or 1000/60 ms). Displayed values are rounded; gates use the exact formula (e.g. T/2 on A = 5.5556 ms, shown as 5.56).

## 0.1 Planning bounds per operation

All values in ms unless stated. For A, the derivation (§2) says which inputs are [bat] and which are [AC].

| # | Operation (definition in §2) | (P) floor A / B / C | PB A | PB B | PB C [prov] | Weakest input |
|---|---|---|---|---|---|---|
| 1 | Keystroke→photon, mean, mid-screen, active typing | beam wait T/2: 5.56 / 8.33 / 8.33 | 27.0 | 68.3 | 63.9 | (E) compositor, scaler, key scan |
| 1i | …first key after ≥ 15 s idle | same | 38.7 | 68.9 | 64.6 | (E) PSR exit, wake |
| 1s | …app stage only: ingress → present submitted | — | 0.3 | 0.2 | 0.5 | (E) |
| 2 | Full frame, CPU path: raster + upload + minimap | fb ÷ peak: 0.20 / 0.32 / 0.33 | 4.60 | 2.44 | 2.92 | (E) raster rate ≈ memcpy (M) |
| 2g | Full frame, GPU raster (fb ÷ GPU BW) | same | 0.83 | 0.69 | 0.70 | (E) GPU BW |
| 3a | Start → app-ready-to-present, warm / cold | — | 11.5 / 13.9 | 17.4 / 26.0 | 32.8 / 42.2 | (M) exec, xcb; (E) rest |
| 3b | Start → on glass, warm, effects off / on | — | 39 / 102 | 82 | 93 | (M) map 58–68 ms with effects on |
| 4 | Open → first viewport submitted, warm / cold | — | 4.4 / 4.8 (+APST) | 2.4 / 3.1 | 2.8 / 5.2 | (E) redraw |
| 5 | Index 1 GB (newline + ASCII flag), 1 worker, warm / cold | 9.8 / 39 / 78 (warm) | 62.5 / 400 | 100 / 2000 | 250 / 6667 | A warm (M)[AC]; cold (E) |
| 5j | Jump to line 10 M with no index (400 MB scan), warm / cold | 3.9 / 16 / 31 | 25 / 160 | 40 / 800 | 100 / 2667 | as #5 |
| 5k | Jump with index (lookup only) | — | ≤ 0.011 | ≤ 0.010 | ≤ 0.020 | (M) latency |
| 6 | Insert 1 char incl. split + allocation | — | 0.008 | 0.008 | 0.018 | (M)/(E) |
| 6b | 1 MB paste / selection delete → frame | — | 4.58 | 2.7 | 3.5 | (E) |
| 7 | Find 1 GB, typical density, 1 worker | 9.8 / 39 / 78 | 62.5 | 100 | 250 | as #5 |
| 8 | Save 1 MB durable, flush p50 / p99 | — | 5 / 41 | 22.5 / 102.5 | 60 / 220 | (E) flush |
| 9 | Undo 10k steps + one redraw | — | 41.5 | 17.4 | 20.8 | (M) A latency |
| 10 | Tab switch (= #2 incl. minimap) | — | 4.60 | 2.44 | 2.92 | as #2 |
| 11 | Minimap full re-raster | — | 0.18 | 0.07 | 0.12 | (E) |
| 12 | Baseline memory incl. minimap cache (MB) | — | 57.7 | 29.1 | 20.5 | (E) driver |
| 13 | App CPU per cursor blink | — | 0.05 | 0.05 | 0.1 | (E) |

## 0.2 Ship gates

Rules:
- Each gate holds with index, find and save running concurrently (§2.17), unless the row is itself a background job.
- B rows apply to the **B fixture** (§1). A slower display class is declared separately in §1.
- C columns are **[prov]**: tracked, not enforced.
- The perceptual aspiration (Asp.) is not enforced until the reference renderer meets it (§0).

| Gate | Metric (start → end) | Asp. | A p50 | A p99 | B p50 | B p99 | C p50 [prov] | C p99 [prov] | k vs PB |
|---|---|---|---|---|---|---|---|---|---|
| **G1** Typing | input ingress (evdev / raw-input timestamp) → present submitted with the edit; any file ≤ 10 GB | — | **0.5** | **2** | **0.5** | **2** | 0.5 | 2 | p99 ≈ 4–10×; absolute cap 2 ms |
| **G2a** Optical, active typing | electrical key contact → photodiode 50 % at mid-screen | 13 / 25 (WP5.1, §0.3) | **30** | **50** | **75** | **100** | 70 | 95 | p50 ≈ 1.1×; p99 ≥ mid-screen model max (36.3 / 88.5 / 82.3) |
| **G2b** Optical, first key after ≥ 15 s idle | same | — | **45** | **65** | **75** | **100** | 70 | 95 | as G2a, plus idle scenario |
| **G2c** Incremental optical | G2a(editor) − G2a(minimal reference renderer), same machine, path and display | — | **1** | **3** | **1** | **3** | 1 | 3 | measurable form of "adds ≤ 10 %" |
| **G3** Full frame | ingress → compositor-ready (XShm completion / GPU fence), incl. minimap; scroll, page-down, tab switch | ≤ T/2 | **5.0** | **5.56** (= T/2) | **4.0** | **5.0** | 5.0 | 8.0 | A: capped by T/2; B/C ≈ 2× |
| **G3z** Cadence | missed demanded refreshes in a 10 000-refresh continuous-scroll run | 0 | **0** | **0** | **0** | **0** | 0 | 0 | — |
| **G4a** Startup, software | exec → first frame complete in back buffer, map requested, input accepted; warm | 100 | **25** | **40** | **35** | **50** | 65 | 90 | ≈ 2× / 3× |
| G4a cold | same, cache evicted and verified | 100 | **30** | **140** (incl. ≤ 100 APST, §2.3) | **50** | **70** | 85 | 120 | ≈ 2× |
| **G4b** Startup, on glass | exec → readable frame, photodiode; effects OFF (effects ON reported) | 100 | ≤ 100 (prov) | report | ≤ 100 (prov) | report | ≤ 120 | report | provisional goal |
| **G4c** First-install increment | first launch − reference app's first launch, both signed, B fixture with Defender | — | — | — | **20** | **50** | — | — | — |
| **G5** Open, warm | open request → first viewport submitted; bounded prefix, any S, any line length | 100 | **6** | **9** | **4** | **6** | 5 | 7 | ≈ 1.4× / 2× |
| G5 cold (also cold tab) | same, cold file | 100 | **10** | **110** (APST) | **6** | **35** | 10 | 60 | storage tails |
| **G6** Find | 1 GB warm, ≤ 1 match/KiB: exact count + first 4096 offsets | 1 000 | **80** | **125** | **125** | **200** | 315 | 500 | 1.25× / 2×, 1 worker |
| G6 adversarial | 1 GB of `a`, search `a` (10⁹ matches); results memory ≤ 64 KiB | — | — | ≤ 2·(PB) = **4 200** | — | **4 200** | — | 10 500 | linear time |
| G6c Cancel | cancel request → worker acknowledged; stale generation dropped | — | **1** | **5** | **1** | **5** | 1 | 5 | — |
| **G7** Index | 1 GB warm, 1 worker / cold | 1 000 | **80 / 600** | **125 / 800** | **125 / 3 000** | **200 / 4 000** | 315 / 9 000 | 500 / 13 300 | 1.25–1.5× / 2× |
| G7j Unindexed jump | to line 10 M, warm / cold | 1 000 | **30 / 250** | **50 / 350** | **50 / 1 200** | **80 / 1 600** | 125 / 3 500 | 200 / 5 000 | ≈ 1.25–1.5× / 2× |
| **G8s** Save | ack: snapshot + enqueue, UI thread | 100 | **2** | **5** | **2** | **5** | 2 | 5 | — |
| G8d Save durable | 1 MB / 1 GB warm-source / 1 GB cold-source | — | **10 / 1 500 / 2 200** | **50 / 2 500 / 3 000** | **45 / 3 800 / 6 800** | **250 / 6 000 / 9 500** | 120 / 30 k / 40 k | 400 / 40 k / 55 k | ≈ 1.5–2.5× |
| **G9** Edit ops | 1 MB paste / delete → frame; 10k-step undo → frame | 100 | **5 / 62** | **15 / 83** | **5 / 26** | **15 / 35** | 8 / 31 | 20 / 42 | 2× |
| **G10** Memory (peak) | baseline incl. backend handoff and owned graphics (MB) | — | **86** | **86** | **44** | **44** | 31 | 31 | 1.5× |
| G10f per-file | unedited copy ≤ 1.25 S + 64 KiB; unedited mmap ≤ 32 B·⌈S/64 KiB⌉ + 2 MB; edits ≤ 96 B/piece + 64 B/undo + 1.25 × (typed + deleted-original bytes) | — | same | same | same | same | same | same | ≈ 2× |
| **G11** Idle | app CPU per blink (ms); wakeups: ≤ 2/s while blinking, 0/s after 10 s or when unfocused | — | **0.1** | **0.2** | **0.1** | **0.2** | 0.2 | 0.4 | 2–4× |

## 0.3 Historical calibration (illustrative only)

These figures are estimates, built from 8088/286 instruction timings plus guesses for keyboard and ISR time. They illustrate a point; they do not calibrate k.

| System | Keystroke→photon best / mean / worst (E) | Stated floor, mean (E) | Ratio | Full-screen redraw |
|---|---|---|---|---|
| PC XT 8088 @ 4.77 MHz, CGA 60 Hz, WordStar | 3.5 / 16.0 / 35.7 | 12.6 | 1.27× | 4000 B; REP MOVSW 9+25n cyc = 50 009 cyc = 10.5 ms (+ wait states; 2–4 frames if retrace-synced) |
| 286 @ 12 MHz, VGA text 70 Hz, WP 5.1 | 2.4 / 12.3 / 24.5 | 11.2 | 1.10× | 4000 B over ISA at 2–5 MB/s = 0.8–2.0 ms |
| This editor at gate (A / B) | p50 30 / 75 | PB 27.0 / 68.3 | ≤ 1.1× aspiration | 5.0 / 4.0 ms p50 |

- The "floors" above are not derived from the stated stages. Applying §2.1's midpoint convention to the 286 ranges gives 12.99 ms, not 11.2.
- What the comparison does show: the 286 had three stages and no buffering. Our A–C chains carry an (E) compositor frame plus, on B/C, a scaler frame, LCD response and 125 Hz USB. Their modeled means are 27–68 ms.
- **k = 1.1 is an aspiration, not a derived requirement.** G2c is its measurable form: ≤ 1/3 ms added over a bare window on the same path.
- Luu 2017 (camera-measured, including key travel) [lit, verify]: 60–170 ms on modern systems vs ~30 ms on an Apple IIe.

## 1. Targets

**A** is the measured laptop. **B** is a named fixture class, not "the worst desktop". **C** is provisional.

| Item | A (00-profile) | B fixture (release) | C (provisional, [est]) |
|---|---|---|---|
| CPU | i7-1365U 2P+8E; 400 MHz floor; EPP balance_power; hwp_dynamic_boost = 0 (M) | i5-12400 6P/12T, 4.4 GHz turbo, 800 MHz min (spec) | 2C/2T Haswell/Broadwell Celeron or Atom-class, 1.5–2.0 GHz, no AVX/AVX2 (spec-class) |
| RAM | 32 GB LPDDR5-6400, 102.4 GB/s peak (spec) | **1 × 16 GB DDR4-3200, single channel, 25.6 GB/s peak** (spec) | 4 GB DDR3L-1600, single channel, 12.8 GB/s (spec) |
| ST SIMD scan | **16 GB/s** (M)[AC]: SSE2 newline + ASCII flag 16.7–17.3; memchr 20.3–20.7; battery rerun pending | 10 GB/s (E) | 4 GB/s (E) |
| MT scan | 23.2 GB/s (M)[bat], scalar 8 threads; SIMD MT unmeasured | 18 GB/s (E) | 6 GB/s (E) |
| memcpy payload ST | 9.4 (M)[bat]; 12.5 warmed / 9.1 first-burst after 2 s idle (M)[AC] | 7 GB/s (E) | 3 GB/s (E) |
| Raster write / upload rate | = memcpy payload, **(E) proxy** until the blend kernel is measured | 7 GB/s (E) | 3 GB/s (E) |
| Pointer chase, 1 GiB set (TLB-incl.) | 371 ns (M)[bat]; 8 MiB: 115 ns; 1 MiB: 16.7 ns | 150 ns (E) | 180 ns (E) |
| 64 KiB chunk scan, resident | 2.39 µs (M)[AC] | — | — |
| Page-cache → user read | 4.2 GB/s (M)[bat] | 2.5 GB/s (E) | 1.5 GB/s (E) |
| Storage seq read / write | NVMe Gen4, 92 % full: 2.5 / 1.0 GB/s (E; spec 7) | **2.5″ SATA DRAM-less TLC SSD**: 0.50 / 0.40 GB/s (E; spec 0.55) | eMMC 0.15 / 0.05 GB/s (E) |
| 4 KiB QD1 read / flush | 100 µs / 2 ms p50, 20 ms p99 (E). APST policy allows ≤ 100 ms exit (M config) | 150 µs / 10 ms p50, 50 ms p99 (E) | 500 µs / 20 ms p50, 100 ms p99 (E) |
| GPU BW | Iris Xe, 25 GB/s (E) | UHD 730, 12 GB/s (E) | Gen7.5, 6 GB/s (E); may be absent (VDI) |
| Display | 2880×1800 OLED 90 Hz, eDP, PSR-capable | **23.8″ 1920×1080 60 Hz IPS office monitor, DP, standard preset (Dell P24xxH / HP E24-class)**. Acceptance: measured processing latency ≤ 1 frame | 18.5″ 1366×768 60 Hz TN |
| Panel response / scaler (E) | 0.1–1 ms / none | 5–15 ms / ≤ 1 frame (fixture acceptance) | 2–8 ms / 1 frame |
| Keyboard | internal i8042 via EC; scan/debounce **unmeasured**, 0.5–6 ms (E) | **USB full-speed HID boot keyboard (Dell KB216-class), polling 8 ms** (verify bInterval); scan/debounce 1–6 ms (E) | as B |
| OS / presentation | Linux 6.8, Xorg, Muffin (Cinnamon), effects ON by default; HZ = 1000, PREEMPT_DYNAMIC (runtime mode unverified) | Windows 11 24H2, DWM, Defender default, no third-party EDR | Windows 10 DWM or X11 |
| Editor RAM budget (25 %) | 8 GiB | 4 GiB | 1 GiB |
| Copy-mode threshold min(256 MiB, RAM/32) | 256 MiB | 256 MiB | 128 MiB |

**Declared display class "B-slow".** A monitor whose measured processing latency exceeds 1 frame (some office units reach 3) is not silently covered by B.
- Its optical gates (G2a/b, G4b) shift by +(n − 1)·T: +33.3 ms at 3 frames.
- G1, G2c and every other gate are unchanged.

Layout used for counts:

| | A | B / C |
|---|---|---|
| Cell (device px) | 18×36 | 8×16 |
| Text cells (cols × rows) | 142×47 | B 220×64, C 150×45 |
| Minimap | 120 logical px wide, 2 logical px per line | same |

## 2. Derivations

### 2.1 Keystroke → photon (G1, G2)

Stage model (E): L = key scan + transport + OS delivery + **app** + latch wait w + compositor (+1 frame, a **scenario**) + scanout to row + scaler + panel.
- Mean uses the midpoint of each range, w = T/2, and row = mid-screen (T/2).
- The +1 compositor frame is a modeling scenario. Muffin scheduling, DWM independent flip and MPO can remove it, and a CPU fallback using GDI/XShm has its own queue.
- The physics part (P) is only the beam wait: at a fixed refresh rate, the mean wait for a fixed row is ≥ T/2.

| Stage (ms) | A min / max | B min / max | C min / max | Basis |
|---|---|---|---|---|
| Key scan + debounce | 0.5 / 6 | 1 / 6 | 1 / 6 | (E), unmeasured |
| Transport | EC→IRQ1 0.05 / 0.2 | USB poll 0 / 8 (mean 4) | 0 / 8 | (E) / (P) given bInterval |
| OS → app wake | 0.1 / 1.0 | 0.2 / 1.0 | 0.3 / 1.5 | (E); not bounded by HZ |
| **App** | 0.3 | 0.2 | 0.5 | (E), §below |
| Latch wait, compositor, scanout to mid | T/2, T, T/2 (mean 2T = 22.2) | 2T = 33.3 | 2T = 33.3 | (P) scanout; (E) compositor |
| Scaler | 0 | T = 16.7 | T | (E) fixture |
| Panel | 0.1 / 1 | 5 / 15 | 2 / 8 | (E) |
| **Mean, mid-screen** | **27.0** | **68.3** | **63.9** | PB |
| Mid-screen min / max | 17.7 / 36.3 | 48.1 / 88.5 | 45.5 / 82.3 | PB |
| Mean, no compositor frame / no vsync wait | 15.9 / 10.3 | 51.6 / 43.3 | 47.2 / 38.9 | scenarios |

**First key after ≥ 15 s idle (E):**

| Term added to the active-typing mean | ms |
|---|---|
| PSR exit, A only | ≤ +T = 11.1 |
| App clock-ramp penalty, from the measured memcpy first-burst ratio 1.37× (M)[AC] | +0.37 × app = 0.11 (A) |
| GPU RC6 / compositor wake | +0.5 |

Result: A 38.7, B 68.9, C 64.6. Mid-screen max: A 48.0, B 89.1.

**App stage PB, A = 0.3 ms (E).** It covers:
- piece insert (§2.6), ≈ 8 µs
- shaping one ≤ 200-char line, ≈ 2 µs
- rastering one line strip: 2560×36×4 = 368 640 B at 9.4 GB/s = 39 µs
- server-side copy of that strip, 39 µs
- request/flush and context switches, ≈ 0.1 ms
- remaining margin

B: strip 112 640 B = 16 µs. C: 77 184 B = 26 µs, plus slower syscalls.

**G1 starts at ingress, not at dequeue.** Queueing behind UI-thread work therefore counts.
- Example: a single-threaded CPU full redraw on A blocks the UI thread for 4.41 ms (E). A key arriving at a uniform phase during continuous 90 Hz redraw has a p99 residual wait of 4.41 − 0.01·T = 4.30 ms, and G1 fails.
- Hence the requirement: **no UI-thread work slice > 0.5 ms while input may be pending**. Full-frame raster runs on workers (banded and interruptible) or on the GPU.
- Queue age (ingress → dequeue) is instrumented separately.
- Scheduler latency under reclaim or writeback is not bounded by HZ = 1000 or the preemption mode. G1's p99 is measured, not derived.

**G2 gates.**
- p50 ≈ 1.1 × PB mean, as an aspiration with an (E) base.
- p99 sits above the mid-screen model maximum: 50 vs 36.3 (A), 100 vs 88.5 (B).
- **G2c (incremental ≤ 1 / 3 ms)** is the gate the app actually owns. It cancels every (E) stage that the reference renderer shares.
- The WP5.1 aspiration (13 / 25 ms) is not met by the modeled default chain on any target. Whether it is reachable is decided by measuring the reference renderer, including with a compositor-bypass request (`_NET_WM_BYPASS_COMPOSITOR` is a request, not a guarantee).
- USB 125 Hz vs i8042: the mean poll penalty is 4 ms (8 ms only at worst phase). A 1000 Hz keyboard saves 3.5 ms mean on B.

### 2.2 Full-frame and partial redraw (G3)

**CPU path.**
- Raster: fb ÷ R_raster. Then upload (XShmPutImage → Xorg/glamor pixmap, or BitBlt → DWM surface): another fb ÷ R_raster.
- R_raster = memcpy payload rate, an **(E) proxy**. Blend kernel and upload are both unmeasured.
- Compositor (not app): 2·fb ÷ R_gpu.

| ms | A | B | C |
|---|---|---|---|
| fb (B) | 20 736 000 | 8 294 400 | 4 196 352 |
| Raster | 2.21 | 1.18 | 1.40 |
| Raster + upload | 4.41 | 2.37 | 2.80 |
| + minimap (§2.11) | **4.60** | **2.44** | **2.92** |
| Slack vs T/2 | 1.21× | 3.41× | 2.85× |
| After-idle (× 1.37, (M) ratio) | 6.30 > T/2 | 3.35 | 4.00 |
| Clock below which compute binds (0.5 cyc/px) | 1.175 GHz | 0.875 GHz | 0.375 GHz |
| GPU raster fb ÷ R_gpu | 0.83 | 0.69 | 0.70 |
| (P) fb write at peak BW | 0.20 | 0.32 | 0.33 |

- A at 400 MHz: 2 592 000 cyc ÷ 0.4 GHz = 6.48 ms, plus 2.21 ms upload, = 8.69 ms > T/2.
- B at 1440p (note only): CPU path 4.21 ms.

**Consequence (chosen, motivated by (M) memcpy rate, (M) 400 MHz floor and (M) first-burst ratio).** On A, **GPU or multi-threaded banded raster is required**. Single-threaded CPU raster has 1.21× slack when warm and fails after idle. Single-threaded CPU raster remains the fallback for B and C, and must pass G3 *measured after idle*.

**Endpoints, measured separately:**
1. CPU raster done.
2. Buffer safe to reuse: XShm completion event, after the server copy.
3. Compositor-ready: server copy done or GPU fence signaled. **This is G3's endpoint.**
4. Optical.

**Partial redraw.** One line on A = 39 µs raster + 39 µs upload + 20–50 µs request = 0.10–0.13 ms (E). A cursor cell is 2 592 B (A) or 512 B (B/C), so overhead dominates. Partial updates across alternating buffers need buffer-age repair.

**Layout.** Shaping 14 k ASCII cells at about 5 ns each = 70 µs (E). HarfBuzz-class shaping at ~1 µs/char would be 14 ms, so shaped lines are cached.

### 2.3 Startup (G4)

**G4a, app-ready-to-present.** It ends when the first frame is in the back buffer and the map has been requested. The present itself waits for Expose.

A warm PB = **11.5 ms**:

| Step | ms | Basis |
|---|---|---|
| Exec, static | 0.83 | (M) |
| xcb_connect, max of measured range | 1.8 | (M) |
| 5 roundtrips × 0.3 ms: atoms (pipelined), SHM/DRI3, 3 for XKB | 1.5 | (E) |
| xkbcommon keymap | 1.5 | (E) |
| Embedded font, 95-glyph raster | 1.5 | (E) |
| First frame, raster + upload | 4.41 | (E) |

- A cold: add ⌈2 000 000 / 131 072⌉ = 16 × 100 µs + 2 MB ÷ 2.5 GB/s = 2.4 ms, giving 13.9 ms. Then add NVMe APST exit: policy permits ≤ 100 ms and it is unmeasured. The cold p99 gate therefore carries +100 ms until APST exit is measured.
- B warm: CreateProcess 10 + window 3 + font 2 + 2.37 = 17.4 ms (E). B cold: + 31 × 150 µs + 2 MB ÷ 0.5 GB/s = 8.65 ms, giving 26.0 ms.
- C warm: 20 + 6 + 4 + 2.80 = 32.8 ms (E). C cold: the startup footprint is capped at **≤ 640 KiB touched**, i.e. 10 × 64 KiB requests at 500 µs + 655 360 B ÷ 0.15 GB/s = 9.37 ms, giving 42.2 ms. A 2 MB touched footprint would cost 28.8 ms.
- GPU device creation (E: 20 / 50 / 80 ms) runs on a worker. The first frame is CPU-rastered, and the swapchain is allocated after the CPU first-frame buffer is released, or within G10's peak (§2.12).

**G4b, on glass.** PB = app-ready + WM map + latch T/2 + compositor T + scanout T/2 + scaler + panel.
- **Measured on A: map → first Expose = 57.8–67.5 ms with Cinnamon effects ON** (M)[AC]. Whole-process median for a raw xcb window is 57.5 ms. This is outside the app's control.
- With effects on, A ≈ 11.5 + 68 + 22.2 + 0.55 ≈ 102 ms.
- With effects off, using an (E) 5 ms map: A ≈ 39, B ≈ 82, C ≈ 93 ms.
- Goal: ≤ 100 ms with effects OFF on A and B (provisional until measured). Effects-ON is reported, not gated.

**Toolkit.** Native GTK3 measured (M)[AC]: gtk_init 56.4–60.5 ms; first draw 88–95 ms (~40 ms over raw xcb to the same frame). gtk_init alone exceeds the whole G4a warm p50 of 25 ms, so the plan is no toolkit, chosen on these numbers. (v1's 121 ms was Python-GI and is withdrawn.)

**Fonts.**
- `fc-match` in a fresh process takes 16–26 ms (M), so fontconfig discovery runs asynchronously on a worker. It must never run synchronously on the first non-ASCII glyph: tofu/placeholder first, glyph on a later frame.
- The embedded startup font stays.

### 2.4 Open file → first viewport (G5)

Contract: **publish the first viewport from a bounded prefix read before any copy, index or validation completes.** The copy into private memory (copy mode) and indexing run asynchronously.

**Warm PB** = open + ≤ 64 KiB read at the target offset + full frame:

| | A | B | C |
|---|---|---|---|
| 64 KiB read (µs) | 15.6 | 26 | 44 |
| + full frame | 4.41 | 2.37 | 2.80 |
| **Warm PB (ms)** | **4.43** | **2.40** | **2.84** |

**Cold PB** adds one 64 KiB read plus ~3 metadata reads (E):

| | A | B | C |
|---|---|---|---|
| Cold PB (ms) | 4.84 + APST | 3.10 | 5.23 |

- The cold row (also used for cold tab switch and evicted viewports) is gated separately. A's cold p99 includes the APST allowance.
- **mmap has three states**, each benchmarked: cached-file / new mapping, populated mapping, and cold.
  - Fault-around is 64 KiB, so a fresh 1 GB mapping takes ~15 259 minor faults (≈ 15 ms at an (E) 1 µs each).
  - The plan: an explicit worker prefetch (read or madvise), and **no foreground major faults**. The UI thread renders only from resident data and otherwise shows a placeholder.
- **Long-line contract.** Foreground decode and layout are capped at **128 KiB of source per frame** (≈ 0.64 ms at 5 ns/B (E)).
  - A line longer than 64 KiB enters long-line mode: rendering from the nearest column checkpoint (every 4 KiB, built lazily by a worker), approximate column display, and no wrap shaping beyond the window.
  - A newline-free 10 GB file must pass G1, G3 and G5.
  - Context: v1's whole-line model would cost 5 s per 1 GB line at 5 ns/char.

### 2.5 Index, scroll, jump (G7, G7j)

**Background pass = newline count + ASCII flag only.** It is memory-bound at the (M) rate: SSE2 runs at 16.7–17.3 GB/s single-threaded on A.

| 1 GB | A | B | C |
|---|---|---|---|
| Warm, 1 worker | **62.5** [AC] | 100 | 250 |
| Cold (device-bound, (E)) | 400 | 2 000 | 6 667 |
| (P) warm floor | 9.8 | 39 | 78 |

10 GB scales linearly: A cold 4 s, B 20 s, C 67 s.

**Full UTF-8 validation is lazy,** per displayed 64 KiB chunk, carrying up to 3 bytes across chunk boundaries.
- It has its own compute-bound budget. On C, SSE2 runs at 0.5–1 GB/s (E), so 64–131 µs per chunk.
- At most 0.5 ms of validation per frame on the UI thread.
- Invalid bytes are preserved byte-exact.

**Sparse index: 16 B per 64 KiB = 0.0244 % of S.** 10 GB → 152 588 chunks → 2.44 MB. A per-line u64 at 40 B/line would be 20 % of S, i.e. 2 GB.

**Lookup with index:**
- Binary search over 18 levels. The index is L3-resident on A, so 18 × 115 ns = 2.1 µs (M). Pessimistic 18 × 371 ns = 6.7 µs.
- Plus a scan of ≤ 64 KiB: 2.39 µs resident (M) or 4.1 µs at 16 GB/s.
- Total ≤ 11 µs. Page-down cost is therefore G3.

**Without index:** jump to line N scans N × L̄ bytes. For N = 10⁷ and L̄ = 40, that is 400 MB: A 25 / 160 ms warm / cold, B 40 / 800, C 100 / 2667.
- The work is a cancellable worker job.
- Scroll position and scrollbar are **byte-offset based** until the index completes, and line numbers are marked approximate.
- Jump to end or to a percentage is O(1) via one read.

### 2.6 Insert / delete one character (inside G1)

**Piece tree (B+ tree, fanout ≥ 16, per-node byte + newline counts):**
- Traversal: ≤ 5 levels × 2 misses × 371 ns = 3.7 µs. This is a cold-metadata scenario; 2×10⁵ pieces × 96 B = 19 MB actually sits between L3 and DRAM.
- Splitting a piece inside an indexed chunk needs exact newline counts for the two halves: scan ≤ 64 KiB, 4.1 µs.
- Splitting an *unindexed* piece marks the counts as unknown (lazy) and leaves them for the background pass.
- Node allocation comes from a pool (≈ 0.1 µs, (E)).
- The add buffer is **chunked** (64 KiB blocks, never reallocated). Growth therefore never memmoves.
- PB A ≈ 8 µs; resident-edit budget 50 µs.

**Gap buffer, for comparison.** A gap move of d bytes costs d ÷ R_move. With an explicit **0.5 ms edit budget** (¼ of G1 p99), the movable distance is **4.7 / 3.5 / 1.5 MB** (A / B / C).
- This crossover is *not* the reason for the choice.
- The piece table is chosen because files can be larger than RAM, because O(1) snapshots support save, find and index, and because there is one code path for all sizes.

**Bulk edits (G9).** 1 MB paste = 1 MB memcpy into the add buffer (0.11 ms) + newline count (0.06 ms) + piece insert + full frame (4.41 ms) = 4.58 ms (A). B ≈ 2.7, C ≈ 3.5. Deletions of original bytes copy those bytes (§2.14). Above 1 MB the copy is chunked on a worker, and the edit is visible immediately.

### 2.7 Find (G6, G6c)

**Definition.** Find returns the **exact non-overlapping match count plus the first 4096 match offsets**, streamed. Further pages are fetched lazily, and results memory stays ≤ 64 KiB.

**Algorithm.** A memchr-class SIMD candidate filter, with a linear-time verifier (Two-Way) as the fallback for periodic or near-match patterns. That gives O(S + N_match) worst case.

**Typical case** (≤ 1 match/KiB): S ÷ R_scan with **one worker**, i.e. A 62.5 ms (M)[AC], B 100, C 250 per GB. Multi-threaded scanning is a bonus; its throughput must be measured *concurrently with typing* before any gate assumes it.

**Adversarial case** (1 GB of `a`, search `a`): PB = S ÷ R + N × 2 ns (E, per match).
- A: 62.5 + 2 000 = 2 063 ms; B ≈ 2 100; C ≈ 5 250 (5 ns/match).
- Gate ≤ 2 × PB.
- v1's offset-array model would have materialized 8 GB, which is why output is bounded.

**Find-as-you-type.** First match time = d_first ÷ R + one frame. Each keystroke cancels the previous generation (G6c: ack ≤ 1 / 5 ms).

### 2.8 Save (G8s, G8d)

**Ack (UI thread):** take a snapshot (tree root, O(1)), enqueue, show "saving". ≤ 2 / 5 ms. Nothing synchronous happens on the UI thread.

**Durable save model (E):**
- Warm source: max(S ÷ R_pc, S ÷ R_write) + 2 flushes.
- Cold source (serial): S ÷ R_read + S ÷ R_write + 2 flushes.
- Flush is a distribution: A 2 ms p50 / 20 ms p99; B 10 / 50; C 20 / 100.

| PB (ms) | A | B | C |
|---|---|---|---|
| 1 MB, p50 / p99 flush | 5 / 41 | 22.5 / 102.5 | 60 / 220 |
| 1 GB warm-source | 1 004 | 2 520 | 20 040 |
| 1 GB cold-source | 1 404 | 4 520 | 26 707 |

**Linux recipe:** write a temp file in the same directory → fsync → rename() → fsync(dir).

**Windows recipe:**
1. Hold the original open with FILE_SHARE_READ | FILE_SHARE_DELETE.
2. Write the temp file, then FlushFileBuffers.
3. Rename it over the original with SetFileInformationByHandle(FileRenameInfoEx, FILE_RENAME_FLAG_POSIX_SEMANTICS | FILE_RENAME_FLAG_REPLACE_IF_EXISTS).

There is no supported directory flush on Windows. **Residual gap:** rename durability across power loss is filesystem-dependent and not guaranteed by this recipe. (REPLACEFILE_WRITE_THROUGH is unsupported; v1's recipe is withdrawn.)

The "saved" indicator appears only at the durable endpoint.

### 2.9 Undo / redo (G9)

- **Single step:** inverse piece operation, O(log n) ≈ 4–8 µs, plus a partial redraw.
- **10k steps:** batch-apply, then render once. 10⁴ × 10 misses × latency gives A 37.1, B 15.0, C 18.0 ms. Adding one full frame gives **41.5 / 17.4 / 20.8** ms, and the gates are 1.5× / 2× of that.
  - B is faster than A because of A's measured 371 ns LPDDR5 + TLB latency.
  - Per-step rendering would cost 10⁴ × 4.4 ms = 44 s.
- **Memory:** ≤ 64 B per record (G10f).

### 2.10 Tab switch (G3)

A tab switch is a pointer swap plus a full frame plus a minimap re-raster. Per-tab bitmaps are not kept. PB = **4.60 / 2.44 / 2.92 ms**.

A tab whose viewport has been evicted from the page cache falls under G5 cold. A 100-tab mix (including 10 GB files) is in the test corpus.

### 2.11 Minimap

**Viewport window:**

| | A | B | C |
|---|---|---|---|
| Lines shown | 450 | 540 | 384 |
| Bitmap (B) | 240×1800×4 = 1 728 000 | 518 400 | 368 640 |
| Re-raster | 0.18 ms | 0.07 ms | 0.12 ms |
| Cache, 3 viewports tall (MB) | 5.18 | 1.56 | 1.11 |

- Per edit: one strip of ≤ 3.8 KB. Scrolling is an offset into the cache.
- Whole-file density comes from the sparse index, never from rastering the file.
- A blank or stale sidebar fails G3.

### 2.12 Memory (G10)

**Baseline PB (E)** = 2·fb + minimap cache + 2 MB binary + 1 MB atlas/font + 8 MB driver:

| | A | B | C |
|---|---|---|---|
| Baseline PB (MB) | 57.7 | 29.1 | 20.5 |
| Gate, × 1.5 (MB) | **86** | **44** | **31** |

- The gate is a **peak** and includes owned graphics and shared buffers.
- **Backend handoff:** the CPU path uses a single buffer, so at handoff the GPU's 2·fb + CPU's 1·fb + rest = 78.4 / 37.4 / 24.7 MB, inside the gate.

**Per file (G10f):**
- Unedited, copy mode: ≤ 1.25 S + 64 KiB.
- Unedited, mmap mode: ≤ 32 B·⌈S / 64 KiB⌉ + 2 MB. 1 GB → 2.49 MB; 10 GB → 6.88 MB.
- Edits and history: ≤ 96 B/piece + 64 B/undo record + 1.25 × (typed + deleted-original bytes). The fixture of 10⁵ scattered edits, 2×10⁵ pieces and 10⁵ undo records gives ≤ 25.7 MB.

**Not in the per-file gate, reported separately:**
- Clean mapped residency (it is page cache, not a second copy, so a 10 GB file *can* be resident on B if free RAM allows).
- Page tables: about 1.95 MB per GB mapped, system memory.

**Capacity** within the budget (8 / 4 / 1 GiB minus baseline):

| Files of size | A | B | C |
|---|---|---|---|
| 1 MB, copy mode | 6 463 | 3 231 | 792 |
| 100 MB, copy mode | 67 | 33 | 8 |
| 1 GB, mmap, unedited | 3 417 | 1 708 | 419 |

The fd/handle limit is the practical cap: raise RLIMIT_NOFILE, and do not keep fds for copy-mode files.

### 2.13 Idle (G11)

- **Loop:** event-driven only, using poll / MsgWaitForMultipleObjects and inotify / ReadDirectoryChangesW.
- **Blink:** ≤ 2 wakeups/s. It stops after 10 s of idle, and when the window is unfocused.
- **Per-blink PB:** about 10 µs wake + one-cell raster + request ≈ 0.05 ms on A and B, 0.1 ms on C (E).
- **Outside the app:** compositor recomposition and, on A, PSR exit. That is the reason for the blink timeout.

### 2.14 Original-file consistency (mmap is not a snapshot)

- **Snapshot** = tree root only. It is valid only while the original bytes are unchanged.
- **Byte-exact undo of deleted original bytes:** the deleted range is copied into the add buffer at delete time. Cost O(deleted bytes): 1 MB = 0.11 ms on A; larger deletes are done on a worker.
- **Detection:** inotify (MODIFY, ATTRIB, CLOSE_WRITE, DELETE_SELF, MOVE_SELF) or ReadDirectoryChangesW, plus a size/mtime/inode check on focus and before save.
- **On external change:** freeze, by copying the still-referenced original ranges into private memory. Cost ≤ S ÷ R_pc: 1 GB = 238 / 400 / 667 ms (A / B / C), on a worker and reported to the user.
- **Residual race:** the interval between the external write and its detection. On Linux, a SIGBUS handler covers truncation. On Windows, write-sharing of the original is denied while it is mapped, which closes the race at the cost of blocking other writers. That is a product decision, stated here.

### 2.15 Concurrency

Before any search runs, A's CPU path at 90 Hz already generates DRAM traffic (E):

| Source | GB/s |
|---|---|
| Raster + upload, memcpy-equivalent accounting: 2 × 2 × 20.7 MB × 90 | 7.46 |
| Compositor | 3.73 |
| Scanout | 1.87 |
| **Total** | **13.06** |

- Worker priority does not reserve bandwidth. **One bulk worker** (index, find or save) is the plan. Multi-threaded scanning is optional and gated by measurement under concurrency.
- G1, G3 and G2c must pass with index + find + save all running. Background completion times in that combined case are reported, not gated.

## 3. Chosen architecture (with the number that motivates each)

- **Rendering.**
  - A: GPU (GL 3.3 / Vulkan) or multi-threaded banded CPU raster is *required*, because single-threaded CPU gives 4.60 ms vs T/2 = 5.56 ms (1.21×, (E) on an (M) memcpy rate) and 6.30 ms after idle ((M) ratio).
  - B/C: single-threaded CPU raster is an acceptable fallback (3.4× / 2.9× slack (E)), and is required to exist for no-GPU/VDI machines.
  - (P) gives no reason to prefer either path: fb writes take ≤ 0.33 ms at peak BW.
- **UI thread.** Work slices ≤ 0.5 ms, rendering interruptible by pending input, presentation waits off the input path (G1 from ingress).
- **Present.** Render on input; frame queue depth 1; DXGI waitable swapchain with max frame latency 1. The GDI/XShm fallback has its own queue and is measured separately (G2c).
- **Buffer.** Piece B+ tree over the original plus a chunked add buffer; lazy newline counts; pooled nodes. Chosen because of files larger than RAM, O(1) snapshots and one code path, not because of the gap-buffer crossover (4.7 / 3.5 / 1.5 MB at 0.5 ms).
- **File loading.**
  - Bounded-prefix first viewport (G5).
  - Copy mode below min(256 MiB, RAM/32), done asynchronously; mmap with worker prefetch above that.
  - No foreground major faults. Consistency policy per §2.14.
- **Indexing.**
  - Sparse 16 B / 64 KiB index: 2.44 MB vs 2 GB per-line at 10 GB.
  - Background pass is newline + ASCII flag at (M) 16 GB/s; validation is lazy.
  - Byte-offset scroll until the index is complete.
- **Long lines.** 128 KiB per frame foreground cap, plus a long-line mode (§2.4).
- **SIMD.** SSE2 baseline. Measured on A, SSE2 at 16.7–17.3 GB/s beats AVX2 at 15.5–16.2 on the newline scan, so AVX2 is optional dispatch. Full UTF-8 validation without SSSE3 is compute-bound on C (0.5–1 GB/s (E)), which is why it is lazy.
- **Toolkit.** None. Native GTK3 init alone (56–60 ms (M)) exceeds G4a's warm p50 (25 ms).
- **Linking and size.** Static linking for our code; GL/Vulkan is loaded with dlopen on a worker. Binary ≤ 2 MB, and **≤ 640 KiB touched before the first frame** (C cold, §2.3).
- **Fonts.** Embedded startup font and pre-baked ASCII atlas (61.6 KB on A, 12.2 KB on B/C). Fontconfig/DirectWrite discovery is asynchronous (16–26 ms (M)).
- **Threading.** UI thread plus one bulk worker plus a raster pool on A. Cancellation ack ≤ 1 / 5 ms.
- **Memory.** Peak baseline ≤ 86 / 44 / 31 MB. No per-tab framebuffers. Undo is stored as piece operations.
- **Idle.** Event-driven; blink timeout.

## 4. How each gate is measured

**Software timestamps** (CLOCK_MONOTONIC on Linux, QueryPerformanceCounter on Windows) go into a preallocated trace ring:

| # | Timestamp | Source |
|---|---|---|
| T0 | input ingress | evdev timestamp from a tracer holding the device with EVIOCSCLOCKID = MONOTONIC, correlated by key sequence (X event time has only ms resolution); Windows: raw-input/ETW Win32k input events |
| T1 | app dequeue | gives queue age = T1 − T0 |
| T2 | mutation done | |
| T3 | render done | CPU done; or GPU fence |
| T4 | present submitted | |
| T5 | buffer reusable / compositor-ready | XShm completion |
| T6 | present complete | X Present CompleteNotify (UST/MSC); PresentMon on Windows, including the presentation mode |

**Gate endpoints:** G1 = T4 − T0; G3 = T5 − T0.

**Optical gates.**
- A photodiode on the cursor's mid-screen row, sampled at ≥ 20 kHz, with the threshold at 50 % of the transition. Both dark→light and light→dark changes are measured.
- The start is electrical key contact: a wire tapped on the key switch, or a solenoid with a measured actuation-to-contact time. The real keyboard is used; A uses its internal keyboard.
- Phase is randomized relative to refresh.

**Minimal reference renderer** (for G2c and for promoting aspirations): a bare xcb (A) or Win32 (B) window that draws the typed glyph via the same present path as the editor, on the same machine and display, measured interleaved with the editor.

**Samples.**
- ≥ 10 000 per interaction scenario; 1 000 launches per startup condition.
- p50 and p99 are nearest-rank. Report n and the confidence interval. Failures, timeouts and dropped frames are never excluded.
- Zero misses in 10 000 refreshes gives a one-sided 95 % upper bound of 0.03 %.

**Conditions.**
- **A:** on battery, the stock powersave governor, EPP and PSR left enabled, Cinnamon defaults (effects ON; G4b also with effects OFF), and the disk at its real fullness.
- **B:** the fixture as specified in §1.
- **Cold:** evict the page cache (Linux: `drop_caches`, then verify non-residency with `fincore`/mincore and I/O counters; Windows: RAMMap "empty standby" or reboot, then verify with ETW hard-fault counts). Reopening a just-closed file does not count as cold.
- **First-after-idle:** ≥ 15 s with no input.
- **Concurrency runs:** index + find + save running.

**Memory.**
- Linux: classify smaps (anonymous private, owned shared/SHM, file-backed clean) and add owned GPU allocations (DRM fdinfo).
- Windows: private commit plus working set, with GPU allocations from ETW/DXGI.
- Record the peak over startup and backend handoff.

**Corpus.** ASCII code; Unicode and bidi; malformed UTF-8; dense short lines; a 10 GB single-line file; fragmented edit history (10⁵ edits); dense-match search; 100 tabs. Every result is checked for correctness: a placeholder, stale minimap or dropped key is a failure, not a pass.

## 5. Assumptions and known gaps

1. **A scan and memcpy-ramp numbers are [AC].** The battery rerun is pending. If the battery ST scan comes in below 16 GB/s, G6/G7 PBs scale by 16 / R_bat; the gates are re-derived, not waived.
2. **Raster and upload rates are memcpy proxies (E).** The blend kernel and upload path are still to be benchmarked; G3 on A rests on this.
3. **Every presentation stage is (E):** Muffin scheduling, DWM mode, scaler, panel response, PSR exit, RC6. G2c is designed to be insensitive to them.
4. **Key scan/debounce timing is unmeasured on every target.** i8042 only establishes that A has no poll quantum.
5. **NVMe APST exit on A is unmeasured.** Policy allows ≤ 100 ms, and the cold G4a/G5 p99 gates carry that allowance.
6. **B fixture components are named by class.** Exact SKUs are to be recorded when the fixture is bought, and the monitor's ≤ 1-frame processing latency verified at acceptance.
7. **Windows durability:** there is no directory flush, and rename durability is filesystem-dependent (§2.8).
8. **mmap consistency race** between an external write and its detection (§2.14).
9. **Target C** is a design constraint only. In particular, a VDI/RDP machine has network and encoder latency that this local model does not cover.
10. **Syntax highlighting is out of scope** for the derivations. If it ships, it must fit inside G1, G3 and G9 as measured (visible-only tokenizing ≈ 70 µs (E); whole-file highlighting at ~200 MB/s = 5 s/GB, so it must be lazy).

## 6. Rejected review points

| Point (source) | Decision and reason |
|---|---|
| Bind A's ST scan at 6.3 GB/s (arith #2, systems #9, gates #7) | **Rejected.** Superseded by measurement: SSE2 newline scan 16.7–17.3 GB/s, memchr 20.5 GB/s (M)[AC]. 6.3 was a compute-bound scalar loop. 16 GB/s is used, flagged [AC]. |
| One-worker find provisional at 2S ÷ 6.3 GB/s = 317 ms/GB (systems #11) | **Rejected** for the same reason. The one-worker gate is 125 ms p99 at 16 GB/s. |
| Warm find/index gates of 65 / 90 ms on A (gates #9) | **Rejected.** They implicitly assume multi-threaded scan throughput that has not been measured under concurrency. One worker: 80 / 125. |
| Remove C from the gate set (gates #3) | **Partly rejected** (Tobias' scope). C stays, provisional and not enforced. |
| On-glass startup gates of 40/60 warm and 50/80 cold on A (gates #4) | **Rejected as enforced gates.** The measured Cinnamon map → Expose of 58–68 ms makes on-glass desktop-dominated. Split into G4a (app, enforced) and G4b (on glass, provisional ≤ 100 ms with effects off). |
| G2 as "≤ 2 ms added mean, ≤ 1 refresh added p99" (systems verdict) | **Superseded** by G2c, which is stricter: ≤ 1 / 3 ms p50 / p99 over the reference renderer. |
| Unindexed-jump B gates of 40 / 60 (gates table) | **Adjusted** to 50 / 80. B's PB at 10 GB/s is 40 ms, so a 40 ms p50 would mean k = 1.0. |
| Put shipped syntax highlighting inside the derivations (gates #8) | **Rejected** on scope (the brief excludes it). Gaps #10 requires that it fit the gates if it ships. |
| Treat the B+ tree index as a 1 GiB random working set, or replace it with L3 latency (arith #11) | **Kept pessimistic.** 371 ns is used and labeled a cold-metadata scenario, with the L3 (115 ns) figure shown alongside. |

## 7. Changelog v1 → v2

1. Evidence tags (P)/(M)/(E)/(G); "bound" became "planning bound"; every "physically unreachable" and "forced" claim resting on (E) was removed.
2. All gates now have p50 and p99. A and B are release gates; the B fixture is named, with the 3-frame display as the declared class "B-slow"; C is provisional.
3. Added the measurement section (§4): T0–T6, photodiode, sample counts, cold verification, A on battery with PSR enabled.
4. Arithmetic: A mean 27.0 (×1.1 = 29.7), no-compositor 15.9, tearing 10.3; B 68.3 (×1.1 = 75.1); 286 ratio 1.10.
5. Arithmetic: undo + redraw 41.5 / 17.4 / 20.8 → p99 83 / 35 / 42; tab switch includes minimap (4.60 / 2.44 / 2.92).
6. Arithmetic: request counts use ceilings (A 16 × 128 KiB; B/C 31 × 64 KiB); C 2 MB = 28.8 ms; C touched-at-start ≤ 640 KiB.
7. Arithmetic: MiB/GiB units fixed (C threshold 128 MiB); baseline subtracted from capacity counts; T/2 exact.
8. G1 starts at ingress; p50 0.5 ms added; UI slices ≤ 0.5 ms; queue-age instrumentation; HZ does not bound scheduling.
9. G2: compositor frame relabeled as a scenario; first-after-idle row added (PSR, RC6, measured 1.37× ramp); G2c incremental gate 1 / 3 ms; k = 1.1 is an aspiration; calibration is illustrative.
10. G3: single-threaded slack is 1.21× (with minimap); compute binds below 1.175 GHz; 8.69 ms at 400 MHz → GPU or multi-threaded raster required on A; four endpoints distinguished.
11. G4: split into app-ready (enforced) and on-glass (provisional); measured WM map 58–68 ms; native GTK3 57 ms / ~95 ms used instead of 121 ms.
12. G5: bounded-prefix publication before copy/index; long-line contract; cold row with APST; three mmap states; no foreground major faults.
13. Scan rate 16 GB/s (M)[AC]; §4/§5/§7 recomputed; 9.4 kept for memcpy payload only; raster/upload rates labeled (E).
14. UTF-8: the background pass is newline + ASCII flag; full validation is lazy, with a 3-byte carry and its own budget.
15. G6: count + first 4096 offsets; adversarial dense-match row; linear-time verifier; one worker; cancellation ack 1 / 5 ms.
16. G10: memory split into baseline / unedited / edit-history / residency / graphics; minimap cache included (86 / 44 / 31); edit terms 96 / 64 / 1.25×; page tables 1.95 MB/GB; v1's "10 GB does not fit B" claim removed.
17. §2.14 added: mmap is not a snapshot; deleted-original copy; detection; freeze on external change.
18. Save: ack and durable gates; flush given as p50/p99; warm-source vs cold-source; Windows POSIX-semantics rename recipe; REPLACEFILE_WRITE_THROUGH dropped.
19. Piece edit includes split, allocation and lazy counts; chunked add buffer; gap-buffer crossover at 0.5 ms = 4.7 / 3.5 / 1.5 MB, no longer load-bearing.
20. Concurrency (§2.15): one bulk worker; 13.06 GB/s pre-search traffic on A; gates verified with index + find + save running.
21. Fontconfig: discovery asynchronous (fc-match 16–26 ms (M)).
22. Keyboard: the USB 125 Hz mean penalty is 4 ms, not 8; the start point is defined (electrical contact for optical gates, evdev for software gates).
