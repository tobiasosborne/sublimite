# 01 — Performance target (analytic lower bounds → ship gates)

Method: per target machine, derive the lower bound of each operation from hardware rates and floors.
Engineering target = bound × stated slack k. Rates are **pessimistic**, as Tobias asked:
- **Target A:** the measured battery/powersave numbers in `00-hardware-profile.md` are binding. Unmeasured A values take the pessimistic end of their plausible range.
- **Targets B and C:** the worst commodity configuration is binding. Better configurations appear only in notes.

So each "bound" here is the *achievable floor on the worst-case configuration*, not a strict physics floor. Where it matters, the physics floor (theoretical peak) is given beside it.

Tags: **[meas]** measured in 00; **[spec]** vendor spec; **[est]** my estimate (pessimistic end); **[lit]** literature, cited from memory, verify before quoting.

Notation: T = frame interval; fb = framebuffer bytes = W×H×4; S = file size; R = rate (GB/s = 10⁹ B/s). "App-side" = from input event dequeued by the app to buffer handed to the compositor/X server.

## 0. Summary

### 0.1 Bounds and targets (all ms unless stated)

| # | Operation | A bound | A target | B bound | B target | C bound | C target | k |
|---|---|---|---|---|---|---|---|---|
| 1 | Keystroke→photon, mean, cursor mid-screen | 26.4 | 29.1 | 68.2 | 75.0 | 63.9 | 70.3 | 1.1 (vs chain) |
| 1a | …app-side only (p99) | 0.3 | 2 | 0.2 | 2 | 0.5 | 2 | abs cap 2 ms |
| 1b | …best / worst phase (end-to-end bound) | 12.2 / 41.8 | — | 39.7 / 96.9 | — | 37.1 / 90.7 | — | — |
| 2 | Full redraw, CPU raster + upload | 4.4 | 5.6 | 2.4 | 4.7 | 2.8 | 5.6 | 2 or ½T cap |
| 2a | Full redraw, GPU raster | 0.83 | 1.7 | 0.69 | 1.4 | 0.70 | 1.4 | 2 |
| 2b | One-line redraw / cursor cell | 0.039 / ~0 | 0.1 | 0.016 / ~0 | 0.1 | 0.026 / ~0 | 0.1 | ≥2 |
| 3 | Start→first frame, warm (cold) | 23.5 (25.8) | 47 (52) | 34.0 (42.6) | 68 (85) | 49.5 (78.1) | 99 (100) | 2 / 100 ms cap |
| 4 | Open any S → first text, app-side | 4.6 | 9.1 | 2.6 | 5.3 | 3.7 | 7.5 | 2 |
| 4a | Full index 1 GB cold (background) | 400 | 800 | 2 000 | 4 000 | 6 670 | 13 300 | 2 |
| 5 | Page-down with index (= #2 + 14 µs) | 4.4 | 5.6 | 2.4 | 4.7 | 2.8 | 5.6 | as #2 |
| 5a | Jump to line 10 M without index, warm | 17 | 34 | 22 | 44 | 67 | 134 | 2 |
| 6 | Insert 1 char, 1 GB buffer (piece tree) | 0.0037 | 0.05 | 0.0015 | 0.05 | 0.0018 | 0.05 | ≥10 (inside 1a) |
| 7 | Find literal, 1 GB in RAM, all cores | 43 | 86 | 56 | 111 | 167 | 333 | 2 |
| 8 | Save 1 MB incl. 2 flushes (async) | 5.0 | 10 | 22.5 | 45 | 60 | 120 | 2 |
| 9 | Undo 10k steps (+ one #2 redraw) | 37 | 74 | 15 | 30 | 18 | 36 | 2 |
| 10 | Tab switch (= #2) | 4.4 | 5.6 | 2.4 | 4.7 | 2.8 | 5.6 | as #2 |
| 11 | Minimap full re-raster / per edit | 0.18 / µs | 0.4 | 0.07 / µs | 0.15 | 0.12 / µs | 0.25 | 2 |
| 12 | Baseline private memory (MB) | 52.5 | 79 | 27.6 | 41 | 19.4 | 29 | 1.5 |
| 13 | Idle: app CPU per cursor blink | 0.05 | 0.2 | 0.05 | 0.2 | 0.1 | 0.4 | 4 |

### 0.2 Ship gates (perception ceiling vs relative gate)

Rule: the applied gate is the stricter of (i) the absolute perceptual ceiling and (ii) k × that machine's bound. The exception is when (i) is physically unreachable on that machine. Then (ii) applies, and the table says so.

Perception thresholds used:
- **0.1 s** feels instantaneous for a discrete action; **1 s** keeps the user's flow unbroken [lit: Miller 1968; Card/Robertson/Mackinlay 1991; Nielsen 1993].
- **Typing:** the ceiling is parity with WordPerfect 5.1 on a 286: mean ≤ 13 ms, p99 ≤ 25 ms (§0.3). This is at or below the indirect-input latency JNDs reported in the tens of ms [lit: Deber et al. CHI 2015]. Direct-touch dragging is far stricter (~1–10 ms) [lit: Ng et al. UIST 2012], but keyboard input is indirect, so that limit does not apply here.
- **Continuous motion (scroll):** zero dropped frames. App per-frame work must stay ≤ ½T, leaving the other half for the compositor and jitter.

| Gate | Metric | (i) Absolute ceiling | (ii) Relative | A applied | B applied | C applied |
|---|---|---|---|---|---|---|
| G1 | App-side keystroke latency p99, any file ≤10 GB, also during save/index/find | 2 ms | 10 × app bound | **2 ms** (i) | **2 ms** (ii = i) | **2 ms** (i) |
| G2 | End-to-end keystroke→photon, mean, mid-screen | 13 ms mean / 25 ms p99 | 1.1 × chain mean | **29 ms** (ii; i unreachable in composited mode, floor 26.4) | **75 ms** (ii; i unreachable, floor 68.2) | **70 ms** (ii; i unreachable, floor 63.9) |
| G3 | Full redraw (scroll, page-down, tab switch), app-side, no dropped frames | ½T | 2 × CPU-path bound | **5.6 ms** (i) | **4.7 ms** (ii) | **5.6 ms** (ii) |
| G4 | Process start → first interactive frame on glass, warm / cold | 100 ms | 2 × bound | **47 / 52 ms** (ii) | **68 / 85 ms** (ii) | **99 / 100 ms** (ii / i) |
| G5 | Open → first text, app-side, **independent of S** | 100 ms end-to-end | 2 × bound | **9 ms** (ii) | **5.3 ms** (ii) | **7.5 ms** (ii) |
| G6 | Find 1 GB in RAM, last result | 1 s | 2 × MT bound | **86 ms** (ii) | **111 ms** (ii) | **333 ms** (ii) |
| G7 | Private memory: baseline / per mmapped file | 100 MB (2.5 % of 4 GB) | 1.5 × bound | **79 MB** (ii) | **41 MB** (ii) | **29 MB** (ii) |
| G8 | Idle | 0 wakeups when not blinking; blink ≤ 2 wakeups/s, stops after 10 s | 4 × blink bound | **0.2 ms/blink** | **0.2 ms/blink** | **0.4 ms/blink** |

Per-file gate for G7: copy mode ≤ 1.25·S + 64 KB; mmap mode ≤ 0.1 %·S + 1 MB private.

G2 note: A could reach (i) only with the compositor bypassed and tearing allowed. That floor is 9.8 ms mean (§1). Offer it as an opt-in "low-latency fullscreen" mode.

### 0.3 Historical calibration

| System | Keystroke→photon best / mean / worst | Its own floor (mean) | Multiple of floor | Full-screen redraw |
|---|---|---|---|---|
| IBM PC XT, 8088 @ 4.77 MHz, CGA 60 Hz, WordStar | 3.5 / 16.0 / 35.7 [est] | 12.6 | **1.27×** | 4 000 B, REP MOVSW 9+25n cycles → 50 009 cyc = **10.5 ms** (+CGA wait states; 2–4 frames if retrace-synced to avoid snow) |
| 286 @ 12 MHz, VGA text 70 Hz, WP 5.1 | 2.4 / 12.3 / 24.5 [est] | 11.2 | **1.09×** | 4 000 B over ISA at ~2–5 MB/s ≈ **0.8–2 ms** |
| Modern Electron-class editor on A-class HW | app-side ~10–30 ms; end-to-end ~40–60 ms [lit/est] | 26.4 | **~1.5–2.3×** (app-side 30–100× its 0.3 ms bound) | — |
| This editor at gate (A / B / C) | mean 29 / 75 / 70 | 26.4 / 68.2 / 63.9 | **≤ 1.1×** | 5.6 / 4.7 / 5.6 ms app-side |

XT/286 chain:
- key matrix scan + debounce + serial ≈ 2–8 ms [est]
- INT 9 ISR ≈ 0.1–1 ms
- app ≈ 0.3–10 ms (the 10 ms is CGA retrace waits)
- VRAM write is visible on the next beam pass: 0..T, mean T/2
- no buffer, no compositor, CRT phosphor ≈ 0

Point, quantitatively: the 286 ran at **1.09×** its hardware floor. Modern stacks run at ~2× on end-to-end and ~30–100× on the app-side stage. On A–C, the non-app chain alone (compositor frame + double-buffered vsync, plus scaler, LCD and 125 Hz USB on B/C) has a mean of 26–68 ms. That already exceeds WP5.1's 12.3 ms, so absolute parity is **unreachable in composited mode on every target**. The calibration therefore applies as the multiple: the app must add ≤ 10 % to the chain (G2, k = 1.1). For reference, Luu 2017 ("Computer latency: 1977–2017") [lit] measured end-to-end, camera-based, including key travel: 60–170 ms on modern systems vs ~30 ms on an Apple IIe.

## Target archetypes

| Item | A (measured laptop) | B (office desktop, worst-case config) | C ("any hardware" floor) |
|---|---|---|---|
| CPU | i7-1365U 2P+8E, 5.2 GHz [spec] | i5-12400 6P/12T, 4.4 GHz turbo [spec]; chosen over 13400 for lower clock and L3 | Haswell/Broadwell Celeron/Pentium or Atom-class, 2C/2T, 1.5–2.0 GHz, no turbo [est] |
| SIMD assumed | AVX2 | AVX2 | **SSE2 only** (Haswell/Broadwell Celeron/Pentium have no AVX/AVX2) |
| L2 / L3 | 1.25 MiB/P / 12 MiB [meas] | 1.25 MiB/core / 18 MiB [spec] | 256 KiB–1 MiB / 2 MiB [spec-class] |
| RAM | 32 GB LPDDR5-6400, 102.4 GB/s theor. [spec] | **16 GB DDR4-3200 single-channel, 25.6 GB/s theor.** (OptiPlex/ProDesk often ship 1 DIMM) [spec]; note: dual-channel = 51.2 | **4 GB DDR3L-1600 single-channel, 12.8 GB/s theor.** [spec] |
| ST SIMD read (binding) | 9.4 GB/s [meas: read side of 18.8 memcpy; scalar read was 6.3] | 10 GB/s [est] | 4 GB/s [est] |
| MT read | 23.2 GB/s [meas, 8 thr] | 18 GB/s [est] | 6 GB/s [est] |
| ST bytes moved (memmove/raster write) | 9.4 GB/s [meas] | 7 GB/s [est] | 3 GB/s [est] |
| MT bytes moved | 19 GB/s [meas] | 9 GB/s [est] | 4 GB/s [est] |
| DRAM random latency incl. TLB miss, 1 GiB set | 371 ns [meas] | 150 ns [est] | 180 ns [est] |
| Page-cache read into user buffer | 4.2 GB/s [meas] | 2.5 GB/s [est, Windows cached ReadFile] | 1.5 GB/s [est] |
| Storage seq read / write | NVMe Gen4; 7 GB/s [spec] → **2.5 / 1.0 GB/s** [est, 92 % full, SLC exhausted] | **SATA SSD 0.50 / 0.40 GB/s** [est; spec 0.55]; note: NVMe Gen3 3.0 | **eMMC 0.15 / 0.05 GB/s** [est] |
| 4 KiB QD1 random read | 100 µs [est; spec ~70] | 150 µs [est] | 500 µs [est] |
| Flush (fsync / FlushFileBuffers) | 2 ms [est, no PLP] | 10 ms [est] | 20 ms [est] |
| Readahead/cluster unit | 128 KiB (Linux default) | 64 KiB (Windows image clustering) [est] | 64 KiB |
| iGPU, achievable BW | Iris Xe 96 EU; 25 GB/s [est] | UHD 730 24 EU; 12 GB/s [est] | HD Gen7.5 10–16 EU; 6 GB/s [est]; may be absent (VDI/RDP) |
| Display | 2880×1800 OLED 90 Hz, fb 20.74 MB, T 11.11 ms [meas] | 1920×1080 IPS 60 Hz, fb 8.29 MB, T 16.67 ms [spec]; note: 2560×1440 = 14.75 MB | 1366×768 TN 60 Hz (18.5" office monitor), fb 4.20 MB, T 16.67 ms [spec] |
| Panel response / scaler | 0.1–1 ms / none (eDP) [est] | 5–15 ms / **1 frame** (range 0.5–3) [est] | 2–8 ms / 1 frame [est] |
| Keyboard | EC-emulated i8042 (no poll quantum) [meas]; scan+debounce 0.5–6 ms [est] | USB HID **125 Hz (8 ms)**; scan+debounce 1–6 ms [est]; note: 1000 Hz = 1 ms | as B |
| OS / compositor | Linux 6.8, Xorg + Muffin (Cinnamon); treated as +1 frame [est] | Windows 11, DWM, windowed flip model +1 frame [est] | Windows 10 DWM or X11 compositor, +1 frame |
| Process create (no AV) | 0.83 ms static, 0.93 dynamic [meas] | 10 ms [est]; Defender first-run scan excluded | 20 ms [est] |
| GPU device/context create | 20 ms [est, Mesa iris] | 50 ms [est, D3D11] | 80 ms [est] |
| Editor RAM budget (25 % of RAM) | 8 GB | 4 GB | 1 GB |
| Copy-mode threshold min(256 MB, RAM/32) | 256 MB | 256 MB | 128 MB |

Layout used for counts: monospace cell 18×36 device px on A (9×18 logical at 2×) and 8×16 on B/C. Minimap is 120 logical px wide, with a 2 logical px line pitch. Text cells: A 142×47 = 6 674; B 220×64 = 14 080; C 150×45 = 6 750.

## 1. Keystroke → photon

Formula: L = t_key + t_xport + t_os + **t_app** + w + T_comp + y/H·T + t_scaler + t_panel. Here w ∈ [0, T) is the wait for the compositor latch and T_comp = T (+1 frame).

| Stage (ms) | App controls? | A best | A worst | B best | B worst | C best | C worst |
|---|---|---|---|---|---|---|---|
| Key matrix scan + debounce [est] | no | 0.5 | 6 | 1 | 6 | 1 | 6 |
| Transport: EC→IRQ1 / USB 125 Hz poll | no | 0.05 | 0.2 | 0 | 8 | 0 | 8 |
| Kernel→X / win32k→app wake, incl. C-state exit [est] | no | 0.1 | 1.0 | 0.2 | 1.0 | 0.3 | 1.5 |
| **App: decode, edit, layout 1 line, raster 1 line, present** | **yes** | 0.3 | 0.3 | 0.2 | 0.2 | 0.5 | 0.5 |
| Wait for compositor latch (phase) | only by not adding frames | 0 | 11.1 | 0 | 16.7 | 0 | 16.7 |
| Compositor → vsync (+1 frame) | no | 11.1 | 11.1 | 16.7 | 16.7 | 16.7 | 16.7 |
| Scanout to cursor row (top → bottom) | no | 0 | 11.1 | 0 | 16.7 | 0 | 16.7 |
| Monitor scaler | no | 0 | 0 | 16.7 | 16.7 | 16.7 | 16.7 |
| Panel response | no | 0.1 | 1.0 | 5 | 15 | 2 | 8 |
| **Total** | | **12.2** | **41.8** | **39.7** | **96.9** | **37.1** | **90.7** |

- **Mean, mid-screen** (mean of each stage; w = T/2; row = T/2): A 26.4, B 68.2, C 63.9 ms. The app's share of that is 1.1 %, 0.3 % and 0.8 %.
- **Physics floors.**
  - No compositor, vsync on: means of A 15.3, B 51.5, C 47.2 ms.
  - No compositor, tearing allowed, so only the beam distance remains (mean T/2): A 9.8, B 43.2, C 38.9 ms.
  - "Fastest possible" on A is 1.05 ms: perfect phase, top row, tearing.
  - A B outlier with a 3-frame scaler adds +33 ms. A 1000 Hz keyboard removes 3.5 ms from B's mean.
- **App bound (A, 0.3 ms).**
  - Piece-tree insert: ~4 µs (§6).
  - Shape one line of ≤ 200 ASCII chars: ~2 µs.
  - Raster one line strip: 2 560×36×4 = 368 640 B at 9.4 GB/s → 39 µs.
  - X request/flush and context switches: ~0.1 ms.
  - Margin for the powersave clock ramp from 400 MHz: ~0.15 ms.
  - B: line 112 640 B → 16 µs. C: line 77 184 B → 26 µs, plus slow-CPU syscalls.
- **Slack.** k = 10 on app-side p99, capped by the 2 ms absolute ceiling. The 2 ms is ≤ 0.1 × the smallest perceptual increment, so the app can never be why a keystroke feels slow. k = 1.1 on the end-to-end mean, which is the 286 multiple.
- **Forces:**
  - Render immediately on input, never on a timer tick.
  - No triple buffering, no frame queue deeper than 1 (DXGI max frame latency = 1, waitable swap chain).
  - Damage-limited redraw.
  - The edit plus repaint of the affected line must never touch O(S) data.

## 2. Full-screen and partial redraw

**CPU path.** Write every fb pixel once: background, plus glyph alpha blend from an L2-resident atlas. A's ASCII atlas is 95×18×36 = 61 560 B.
- t_raster = fb / R_move. Compute is not binding: SIMD blend ≈ 0.5 cycle/px → A 5.18 Mpx × 0.5 / 4 GHz = 0.65 ms. It would bind only if the core stayed at its 400 MHz floor (6.5 ms); HWP ramps up under sustained load within ~ms [est].
- Then upload one copy: XShmPutImage → Xorg/glamor pixmap, or BitBlt → DWM redirection surface. t_upload = fb / R_move.
- Compositor (not the app): 2·fb / R_gpu.

| ms | A | B (1080p) | B (1440p, note) | C |
|---|---|---|---|---|
| fb bytes | 20 736 000 | 8 294 400 | 14 745 600 | 4 196 352 |
| CPU raster ST (MT) | 2.21 (1.09) | 1.18 (0.92) | 2.11 (1.64) | 1.40 (1.05) |
| + upload copy | 2.21 | 1.18 | 2.11 | 1.40 |
| **CPU app-side bound** | **4.42** (= 40 % T) | **2.36** (14 % T) | 4.21 (25 % T) | **2.80** (17 % T) |
| GPU raster bound fb / R_gpu (no upload with DRI3/flip) | 0.83 | 0.69 | 1.23 | 0.70 |
| Compositor 2·fb / R_gpu | 1.66 | 1.38 | 2.46 | 1.40 |
| Scanout traffic fb × refresh | 1.87 GB/s | 0.50 GB/s | 0.88 GB/s | 0.25 GB/s |

- **Partial redraw.** One line = 39 / 16 / 26 µs. A cursor cell is 2 592 B (A) or 512 B (B/C), effectively 0 µs; per-request overhead dominates (~20–50 µs).
- **Slack.** k = 2, capped at ½T = 5.6 ms (A) or 8.3 ms (B/C).
- **Layout.** Shaping 14 k ASCII cells at ~5 ns each is 70 µs, so not binding. HarfBuzz at ~1 µs/char would be 14 ms, so shaped lines must be cached.
- **Forces:**
  - **GPU is not forced on any target.** CPU raster fits ½T everywhere.
  - A on battery is marginal: ST CPU path 4.42 vs the 5.6 cap is 1.27× slack; MT raster gives 3.3 ms, 1.7× slack. So A wants GPU or multi-threaded banded raster.
  - B and C pass single-threaded CPU at 2–3.5× margin. C at 1366×768 needs only 2.8 ms.
  - Therefore the CPU path is mandatory as fallback and the GPU path is preferred on HiDPI.

## 3. Cold start → first interactive frame

**Bound** = exec + display connect + unavoidable roundtrips + keymap + WM map + font/atlas + first full redraw (CPU path, which has no GPU init on the critical path) + present (latch → vsync = T, best phase).

**A warm**, total 23.5 ms:

| Step | ms |
|---|---|
| Exec, static [meas] | 0.83 |
| 7 roundtrips × 0.3 ms [est]: setup reply, pipelined InternAtom batch, MIT-SHM or DRI3 query, 3 for XKB device/keymap, MapNotify/Expose | 2.1 |
| xkbcommon keymap compile [est] | 1.5 |
| WM reparent/map [est; excludes Cinnamon map animation] | 2 |
| Embedded font, 95-glyph raster | 1.5 |
| Raster + upload | 4.42 |
| Present (+T) | 11.1 |

**A cold:** add the binary 2 MB / 128 KiB = 15 readahead requests × (100 + 52 µs) = 2.3 ms. Total 25.8 ms. The GL driver, libc and the font are hot because the compositor and desktop use them.

**B warm**, total 34.0 ms; **cold:** add 2 MB in 31 × 64 KiB clusters × (150 + 131 µs) = 8.6 ms, total 42.6 ms.

| Step | ms |
|---|---|
| CreateProcess | 10 |
| Window class/create/DPI | 3 |
| Font | 2 |
| Redraw | 2.36 |
| Present | 16.7 |

**C warm**, total 49.5 ms; **cold:** add 31 × (500 + 437 µs) = 28.6 ms, total 78.1 ms.

| Step | ms |
|---|---|
| Process | 20 |
| Window | 6 |
| Font | 4 |
| Redraw | 2.8 |
| Present | 16.7 |

**GPU path.** Device creation of 20 / 50 / 80 ms [est] would dominate. It must run on a worker thread, with the first frame CPU-rastered or the GPU swap-in after init.

**Comparison.** GTK3 load alone is 121 ms [meas], which is 5× A's bound and over the 100 ms ceiling by itself.

**Binary size budget.** The cold binary read must stay ≤ 10 ms. Readable in 10 ms: A 8.6 MB, B 2.3 MB, C 0.7 MB. **Hard limit 2 MB total; ≤ 1 MB of pages touched before first frame.** C cold at 1 MB is ≈ 14 ms.

**Forces:** no toolkit; no fontconfig on the critical path (1 268 fonts installed); embedded default font plus a pre-baked atlas; static link of our code; GPU init off the critical path. C cold (78 vs a 100 ms cap) has only 1.28× slack, so this is where everything above is load-bearing.

## 4. Open a file

**First text** = open + one random read of ≤ 64 KiB at the start (or the target offset) + full redraw. Read: A 0.13, B 0.28, C 0.94 ms cold. **Independent of S** → G5.

**Full index**, background, one fused pass: UTF-8 check + newline count per 64 KiB chunk. SSE2 `pcmpeqb`/`pmovmskb` runs at ≥ 8 B/cycle from cache, which beats every DRAM rate here, so the pass is memory-bound.
- Warm: S / R_mtread.
- Cold: S / R_disk (the device is binding).

| S | A warm MT | A cold | B warm MT | B cold (SATA) | C warm MT | C cold (eMMC) | Per-line u64 index @40 B/line | Sparse index 16 B / 64 KiB |
|---|---|---|---|---|---|---|---|---|
| 1 KB | ~0 | ~0.1 (1 IO) | ~0 | ~0.15 | ~0 | ~0.5 | 200 B | 16 B |
| 1 MB | 0.04 | 0.4 | 0.06 | 2.0 | 0.17 | 6.7 | 0.2 MB | 0.24 KB |
| 100 MB | 4.3 | 40 | 5.6 | 200 | 17 | 667 | 20 MB | 24 KB |
| 1 GB | 43 | 400 | 56 | 2 000 | 167 | 6 667 | 200 MB | 244 KB |
| 10 GB | 431 | 4 000 | (no fit) | 20 000 | (no fit) | 66 667 | **2 GB** | **2.4 MB** |

All times in ms.

- **Read path.** read() into a private buffer costs S / R_pc: 1 GB = 238 ms (A), 400 (B), 667 (C). Use it only below the copy-mode threshold (256 / 256 / 128 MB). Above it, mmap is used, read-only and via windowed views. A 10 GB file exceeds B's 16 GB once the OS and the page cache are counted, and exceeds C's 4 GB outright. So mmap/streaming is forced.
- **Slack.** k = 2 on the app-side first-text time; k = 2 on background indexing.
- **Forces:**
  - Lazy, background, cancellable indexing.
  - The first screen never waits for the index.
  - A sparse chunk index (16 B / 64 KiB = 0.024 % of S), not a per-line array: a per-line u64 index costs 20 % of S, which is 2 GB at 10 GB.
  - Invalid UTF-8 is flagged per chunk and preserved byte-exact.
  - On Linux, mmap needs SIGBUS handling and change detection for files truncated underneath.

## 5. Scroll / page-down / jump

- **With index:**
  - Binary search over S/64 KiB chunks: 10 GB → 152 588 chunks → 18 levels. Cost 18 × 371 ns = 6.7 µs on A.
  - Then scan ≤ 64 KiB: 65 536 / 9.4 GB/s = 7.0 µs.
  - O(log n) plus O(64 KiB), so the cost is #2 plus 14 µs.
- **Without a complete index:**
  - Jump to line N scans N × L̄ bytes from the nearest indexed point. Example: N = 10⁷, L̄ = 40 → 400 MB.
  - Warm: A 17, B 22, C 67 ms. Cold: A 160, B 800, C 2 667 ms.
  - Jump to end or to a percentage is O(1) by byte offset: one random read.
- **Smooth scroll.** Per frame ≤ #2. With a GPU path, only newly exposed rows are rastered.
- **Forces:** scroll position and scrollbar are **byte-offset based** until indexing completes, with line numbers shown as "unknown/estimated". Line lookup must be O(log n) via per-node newline counts.

## 6. Insert/delete one character

**Gap buffer.** A gap move of distance d costs d / R_move. Worst case d = S.

| S | A | B | C |
|---|---|---|---|
| 1 MB | 0.11 ms | 0.14 ms | 0.33 ms |
| 100 MB | 10.6 ms | 14.3 ms | 33 ms |
| 1 GB | 106 ms | 143 ms | 333 ms |

Gap growth (realloc) has the same cost.

**Crossover.** S* = budget × R_move.
- Budget = the full 1 ms app target: S* = 9.4 / 7.0 / 3.0 MB.
- Budget = ¼ of it, the realistic edit share: S* = 2.35 / 1.75 / **0.75 MB**.

**Piece tree.** B+ tree with fanout ≥ 16 over pieces. 10⁵ edits → 2×10⁵ pieces → ≤ 5 levels × ≤ 2 misses × latency.
- B+ tree: A 3.7 µs, B 1.5, C 1.8.
- Binary tree (~35 levels): A 13 µs.
- Independent of S.

Gate: inside G1; target ≤ 50 µs (k ≥ 10, absolute cost negligible).

**Forces:** piece table over an immutable original (mmap or copy) plus an append-only add buffer, in one code path for all sizes. A gap buffer fails C above ~0.75 MB and needs the whole file in private RAM, which conflicts with #4.

## 7. Find (literal)

**Formula.** S / R_read. A memchr-class first-byte filter plus verify runs at ≥ 8 B/cycle with SSE2, so it is DRAM-bound everywhere.

| ms | A | B | C |
|---|---|---|---|
| 100 MB, ST / MT | 10.6 / 4.3 | 10.0 / 5.6 | 25 / 16.7 |
| 1 GB, ST / MT | 106 / 43 | 100 / 56 | 250 / 167 |
| 1 GB, cold | 400 | 2 000 | 6 667 |

- **Physics floor**, 1 GB at theoretical peak: A 9.8, B 39, C 78 ms.
- **Find-as-you-type.** First match = d_first / R_read + one #2 frame. A match inside the viewport (~10 KB) takes µs.
- **Slack.** k = 2 on the MT bound.
- **Forces:** search runs on worker threads over an immutable snapshot, chunked so it can be cancelled on the next keystroke, with results streamed. The UI thread never scans.

## 8. Save

**Formula.** Save = max(S / R_pc copy, S / R_write) + 2 flushes: one for the temp-file data, one for the directory after rename(); on Windows, ReplaceFile/MoveFileEx with write-through.

| ms | A | B (SATA) | C (eMMC) |
|---|---|---|---|
| 1 KB | 4.0 | 20 | 40 |
| 1 MB | 5.0 | 22.5 | 60 |
| 100 MB | 104 | 270 | 2 040 |
| 1 GB | 1 004 | 2 520 | 20 040 |

- **Slack.** k = 2.
- **Forces:**
  - Save runs on a worker against an O(1) snapshot (immutable pieces), with G1 holding during the save.
  - Write temp, flush, rename, flush dir.
  - Windows: renaming over a file that has a mapped view fails, so either open with FILE_SHARE_DELETE and use POSIX-semantics rename, or copy the still-referenced original ranges before replace (gap, see §Assumptions).

## 9. Undo/redo

- **Single step.** Apply the inverse piece op, O(log n): ≤ 4 µs. Then a partial redraw (#2b).
- **10k steps, batched, one render.**
  - Formula: 10⁴ × 10 misses × latency. A 37 ms, B 15 ms, C 18 ms. B is faster than A because of the measured LPDDR5 + TLB latency.
  - Then one #2 redraw.
- **Memory.** 32 B per record plus the add-buffer text, so 10k steps ≈ 320 KB.
- **Slack.** k = 2.
- **Forces:** batch-apply, then render once. Per-step re-layout would cost 10k × #2 = 44 s on A.

## 10. Tab switch

Pointer swap (ns) + #2 + #11. Bound and gate are as #2 (G3). **Forces:** keep no per-tab framebuffers, re-raster instead; each tab keeps its view anchor as a byte offset.

## 11. Minimap

- **Geometry.** 120 logical px wide (120 columns at 1 px per char), 2 logical px per line, showing the window around the viewport: A 450, B 540, C 384 lines.
- **Full re-raster** = minimap bytes / R_move:

  | | A | B | C |
  |---|---|---|---|
  | Bytes | 240×1800×4 = 1 728 000 | 518 400 | 368 640 |
  | Time | 0.18 ms | 0.07 ms | 0.12 ms |

  Source text read: ~450 × 40 B = 18 KB.
- **Per edit.** One 1-line strip of ≤ 3.8 KB: µs.
- **Slack.** k = 2.
- **Forces:**
  - A whole-file density overview is computed from the sparse index during indexing, never by rasterizing the file. At 25 M lines it is a 55 000:1 downsample.
  - Cache the minimap bitmap, about 3× the viewport tall; scrolling is an offset.

## 12. Memory footprint

**Baseline bound** = 2·fb (double buffer) + 2 MB binary + 1 MB atlas/font + 8 MB driver private [est]:
- A: 41.5 + 11 = 52.5 MB
- B: 27.6 MB
- C: 19.4 MB

k = 1.5 → 79 / 41 / 29 MB.

**Per file:**
- Copy mode (S ≤ threshold): ≈ S.
- mmap mode: private memory ≈ 16 B × S/64 KiB + 48 B/piece + typed bytes + 32 B/undo step. A 10 GB file costs ~2.4 MB of index. Its clean page cache is reclaimable.
- Tab struct: ~8 KB.

**Max files open** within the editor budget (8 / 4 / 1 GB), using the G7 per-file gate:

| Files | A | B | C |
|---|---|---|---|
| 1 MB (1.314 MB each) | 6 088 | 3 044 | 761 |
| 100 MB (125 MB each) | 63 | 31 | 7 |
| 1 GB mmap (~2 MB each) | ~4 000 | ~2 000 | ~500 |

The practical limit is fds/handles: raise RLIMIT_NOFILE, or do not keep fds open for copy-mode files.

## 13. Idle

- **Wakeups.** Zero timers or polling. The event loop blocks in poll() or MsgWaitForMultipleObjects. File watching uses inotify or ReadDirectoryChangesW.
- **Blink.** ≤ 2 wakeups/s (500–600 ms half-period), stopping after 10 s idle (GTK default). No blink when unfocused.
- **Per-blink bound** = wake (~10 µs) + redraw one cell (≤ 2.6 KB) + present request ≈ 0.05 ms on A and B, 0.1 ms on C. k = 4.
- **Outside the app.** Each blink makes the compositor recomposite (~0.5–1.7 ms GPU) and, on A, can force a PSR exit. That system cost is why the blink timeout matters.

## Addendum: syntax highlighting (out of scope for the gates)

- **Visible-only tokenizing.** About 14 k chars × 5 ns ≈ 70 µs, plus 1–8 B of state per line.
- **Whole-file highlighting.** At 1 GB and ~200 MB/s that is ~5 s, so highlighting must be lazy and viewport-driven, with incremental state per chunk.

## Forced architecture decisions

- **Rendering:** a CPU raster path is mandatory. It meets G3 on B and C single-threaded (2.36 / 2.80 ms vs 4.7 / 5.6) and covers VDI/RDP/no-driver C machines. A GPU path (GL 3.3 / Vulkan on Linux, D3D11 FL10 on Windows) is preferred on HiDPI A, where the ST CPU path has only 1.27× slack against ½T (#2). A glyph atlas and damage tracking are required in both paths.
- **Present:** render on input; frame queue depth 1; no added frames (G2, k = 1.1). Optional compositor bypass (`_NET_WM_BYPASS_COMPOSITOR`, fullscreen) on A brings the floor to 9.8–15.3 ms, the only route to WP5.1 absolute parity (#1).
- **Buffer:** a piece table / B+ tree over an immutable original plus an append-only add buffer, with per-node byte and newline counts. A gap buffer fails above 0.75–2.35 MB (#6) and cannot work for files larger than RAM (#4).
- **File loading:**
  - Copy mode below min(256 MB, RAM/32); mmap with windowed views above it (#4, #12). The first screen comes from one read, independent of S (G5).
  - The sparse 64 KiB chunk index is built in the background (#4).
  - Byte-offset scroll model until the index is complete (#5).
- **Threading:**
  - The UI thread does input, edit, layout and raster only. Index, find and save run on workers over immutable snapshots, and are cancellable (G1 must hold during them).
  - On C there are 2 threads, so workers run at lower priority and MT speedup is ≤ 1.5× (#7).
- **SIMD:** hot loops target SSE2 baseline. AVX2 is an optional dispatch only, because every bulk pass is DRAM-bound even at SSE2 widths (#4, #7). Non-ASCII UTF-8 validation without SSSE3 drops to ~0.5–1 GB/s on C (compute-bound), so validation is lazy, per chunk on display.
- **Toolkit:** none. GTK3 costs 121 ms, which exceeds every warm-start gate (G4). Use raw xcb plus xkbcommon on Linux and raw Win32 on Windows.
- **Linking:** static for our code. Use dlopen only for GL/Vulkan, and only from a worker thread. Binary ≤ 2 MB, ≤ 1 MB touched at startup (#3: C eMMC).
- **Fonts:** embedded default font plus a pre-baked ASCII atlas (61.6 KB on A, 12.2 KB on B/C). No fontconfig or DirectWrite on the startup critical path (#3).
- **Memory:** baseline ≤ 29 MB on C, so it fits easily in 4 GB alongside Windows 10. No per-tab framebuffers. Undo is stored as piece ops, not text snapshots (#9, #12).
- **Idle:** fully event-driven, with blink timeout (#13).

## Assumptions and known gaps

1. **Battery numbers on A** are binding, as instructed. The measured scalar ST read (6.3 GB/s) is below the read side of the measured memcpy (9.4 GB/s). I use 9.4 for SIMD scans; if a SIMD scan measures below 9.4, the #4, #7 and #5 bounds scale up proportionally. Measuring it is the first follow-up.
2. **DRAM latency on A** (371 ns) includes 4 KiB-page TLB misses. Hugepages for the piece tree and index would lower it, but the gates do not assume them.
3. **Compositor model** is +1 frame for Muffin, DWM and C. Muffin might latch later (lower), and DWM can do independent flip / MPO for some windows (lower). Unmeasured; latency instrumentation with a photodiode on A is needed.
4. **A eDP panel self-refresh (PSR/PSR2):** exiting it on the first update after idle may add up to ~1 frame [est]. This is not in the table. Check with `i915.enable_psr=0`.
5. **Keyboard scan/debounce** (0.5–6 ms on A's EC, 1–6 ms on USB boards) is a pure estimate and the largest uncertainty in stage 1. i8042 on A has no poll quantum, so it is ≈ 8 ms better than B's 125 Hz USB in the worst case.
6. **Office monitor scaler and LCD response** (1 frame, 5–15 ms) are estimates. Real units range from 0.5 to 3 frames.
7. **Windows numbers** for B and C (CreateProcess 10–20 ms, D3D11 device 50–80 ms, cached ReadFile 1.5–2.5 GB/s, flush 10–20 ms) are estimates. Corporate Defender/EDR can add 10–500 ms on first launch; that is excluded from G4 and must be reported separately.
8. **Storage:** A's NVMe is 92 % full, so the derated 2.5 / 1.0 GB/s and 100 µs figures are estimates; the C eMMC figures are also estimates. NVMe flush latency without PLP varies by ~10×.
9. **iGPU achievable bandwidth** (25 / 12 / 6 GB/s) is estimated. The GPU-path bounds scale inversely with it.
10. **XT/286 figures** come from instruction timings (8088 REP MOVSW = 9+25n cycles) plus estimates of keyboard and ISR time. The modern-editor latencies come from literature [lit] and should be verified before external use.
11. **Windows mmap plus atomic replace** of a mapped original is an unresolved design risk (#8).
12. **Scope:** 64-bit only. A 32-bit build on C could not map large files without strict windowing, which the design uses anyway.
