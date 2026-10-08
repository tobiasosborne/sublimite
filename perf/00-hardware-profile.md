# Target A: this laptop — measured hardware profile

Captured 2026-10-08 on battery, `powersave` governor. Treat measured numbers as a pessimistic floor;
on AC/performance governor expect ~1.3–2x better bandwidth and ~20–30% lower latency.

## Machine
- Lenovo ThinkPad X1 Carbon Gen 11 (21HMS0HS00)
- Linux Mint 22.3, kernel 6.8.0-146-generic, X11 (not Wayland), Cinnamon desktop (compositing WM)
- CONFIG_PREEMPT_VOLUNTARY, HZ not shown (Ubuntu default 1000)

## CPU: Intel Core i7-1365U (Raptor Lake-P, 2P + 8E cores, 12 threads)
- Max 5.2 GHz (P-core turbo), min 400 MHz. Base 15 W TDP class.
- ISA: AVX2, FMA, BMI2, SHA-NI, AES, F16C, SSE4.2. **No AVX-512.**
- Cache (sysfs, P-core cpu0): L1d 48 KiB 12-way, L1i 32 KiB 8-way, L2 1280 KiB 10-way (per P-core; E-core clusters share 2 MiB per 4 cores), L3 12 MiB 12-way shared. 64 B lines.
- lscpu totals: L1d 352 KiB (10 inst), L2 6.5 MiB (4 inst), L3 12 MiB.

## Memory: 32 GB (30 GiB usable), soldered LPDDR5 (X1C Gen11 spec: LPDDR5-6400, 128-bit → 102.4 GB/s theoretical)
Measured (random pointer chase, 4 KiB pages, no hugepages → DRAM numbers include TLB-miss cost):

| working set | latency |
|---|---|
| 16–32 KiB (L1) | 2.8–3.5 ns |
| 256 KiB (L2) | 7.6 ns |
| 1 MiB (L2) | 16.7 ns |
| 2 MiB (L2/L3 edge) | 28.8 ns |
| 8 MiB (L3) | 115 ns |
| 32 MiB (DRAM) | 273 ns |
| 256 MiB (DRAM) | 293 ns |
| 1 GiB (DRAM) | 371 ns |

Bandwidth (simple scalar loops, -O2, no explicit SIMD):

| test | GB/s |
|---|---|
| seq read, 1 thread | 6.3 |
| memcpy, 1 thread (r+w counted) | 18.8 |
| memcpy, 1 thread, L2-resident 1 MiB | 24.3 |
| seq read, 4 threads | 11.0 |
| seq read, 8 threads | 23.2 |
| memcpy, 8 threads (r+w) | 37.9 |

## Storage: KIOXIA KXG8AZNV2T04 2 TB NVMe, PCIe 4.0 x4 (16 GT/s), ext4 root 480 GB partition, 92% full
- Vendor spec class: ~7 GB/s seq read, ~70 µs random read latency (not measured; disk nearly full).
- Page-cache read, 1 GiB via dd 1 MiB blocks: 4.2 GB/s. 100 MB warm `cat`: 20–40 ms.

## GPU / Display
- Intel Iris Xe (Raptor Lake-P), Mesa, OpenGL 4.6 / Vulkan available. Shares DRAM bandwidth with CPU.
- Internal panel eDP-1: **2880×1800**, modes 90 Hz (active) and 60 Hz. OLED (X1C Gen11 2.8K option). 302×189 mm → 242 ppi. HiDPI scale 2x in use → logical 1440×900.
- Framebuffer: 2880×1800×4 B = 20.7 MB per frame; at 90 Hz = 1.87 GB/s just to scan out.
- Frame interval 11.1 ms (90 Hz) / 16.7 ms (60 Hz).

## Input
- Keyboard: internal, i8042 PS/2 ("AT Translated Set 2 keyboard") — interrupt-driven, no USB poll quantization.
- Touchpad/TrackPoint: SYNA8016 over I2C, Elan TrackPoint PS/2.

## Process / toolkit floors (measured)
- fork+exec+wait of /bin/true: 1.3 ms. Trivial dynamic C binary: 0.93 ms; static: 0.83 ms.
- Loading GTK3 via python-gi (dlopen of the whole GTK stack): 121 ms — i.e. what a toolkit costs if you take one.
- 1268 fonts installed; default monospace = DejaVu Sans Mono.

# Target B: commodity office desktop — archetype (to be defined analytically in 01-perf-target.md)
Not measured. Defined as a spec'd archetype representative of a 2023–2026 corporate desktop.

# Addendum 2026-10-08 (after review round 1): follow-up measurements on Target A
**Power state: laptop was CHARGING (AC) during these runs**, governor still `powersave`, EPP balance_power. Re-run on battery is a pending follow-up; expect modest degradation, not the 2–3x gap below.

## Single-thread SIMD scan rate (the disputed 9.4 vs 6.3 GB/s input) — MEASURED
1 GB decimal buffer, 40-byte lines, -O2, single P-core thread:

| kernel | ms / GB | GB/s |
|---|---|---|
| SSE2 newline-count + non-ASCII flag, 64 B/iter | 57.8–59.8 | 16.7–17.3 |
| AVX2 same, 128 B/iter | 61.6–64.6 | 15.5–16.2 |
| glibc memchr (no match) | 48.2–49.2 | 20.3–20.7 |
| AVX2 scan of one 64 KiB chunk (L2/L3-resident) | 2.39 µs each | 27 |

Conclusion: the original scalar 6.3 GB/s loop was compute-bound, not memory-bound. A real SIMD scan runs at **~16–20 GB/s single-threaded**; the document's inferred 9.4 was itself 2x too pessimistic. Use **16 GB/s** as the binding ST scan rate for A (pending battery rerun), and keep 6.3 only as "what a naive scalar loop gets".

## Clock ramp after idle (powersave, 400 MHz floor)
- memcpy 20 MB immediately after 2 s idle: 2.19 ms; warmed: 1.60 ms (12.5 GB/s payload). First-burst penalty ≈ 1.37x / +0.6 ms on a 20 MB job. (The AVX2 after-idle probe was optimised away by the compiler; memcpy figure stands.)

## Window creation to first Expose on this desktop (Cinnamon/Muffin, X11, desktop effects ON) — MEASURED
Minimal native xcb program: connect, create 1440x900 window, map, wait for Expose, draw 6 674 rects, sync roundtrip.

| stage | ms |
|---|---|
| xcb_connect | 0.26–1.8 |
| create_window + map + flush | 0.01–0.02 |
| **map → first Expose (WM/compositor)** | **57.8–67.5** |
| draw screenful of rects + GetInputFocus sync | 0.19–0.61 |
| whole process wall incl. fork+exec+exit, 15 runs median / min / max | 57.5 / 28.0 / 78.2 |

Conclusion: the app-side cost of a native window is well under 1 ms; the **window manager's map path costs ~60 ms** on this stock Cinnamon setup (Muffin "traditional" map effect = 120 ms animation × effect multiplier; first Expose arrives partway through). The derivation's "WM reparent/map ≈ 2 ms" was off by ~30x. This is outside the app's control but inside any "on glass" startup metric.

## Native GTK3 (C, not Python) minimal window
- gtk_init: 56.4–60.5 ms; gtk_init → first `draw` signal: 88–95 ms; whole-process wall median 98 ms (10 runs, 76.7–108.1).
- The earlier 121 ms figure was Python-GI inflated. Native GTK3 init is **~57 ms**, i.e. GTK adds ~40 ms over raw xcb to the same first frame. Still larger than the entire app-side startup budget, so the "no toolkit" decision stands on corrected numbers.

## Other facts surfaced by reviewers from this machine (read-only probes)
- `nvme_core.default_ps_max_latency_us=100000`: NVMe APST may sleep with up to 100 ms exit allowance → "first cold read after idle" is not bounded by QD1 latency.
- intel_pstate, EPP `balance_power`, `hwp_dynamic_boost=0`, min 400 MHz.
- CONFIG_HZ=1000, CONFIG_PREEMPT_DYNAMIC=y (runtime mode unverified).
- `fc-match monospace` as a fresh process: 16–26 ms (fontconfig init must never be synchronous on the UI thread).

# Addendum 2 (2026-10-08, after round-2 verification): full-frame CPU raster + XShm upload — MEASURED on A (AC, powersave)
Setup: xcb + MIT-SHM image, pessimistic scalar blend kernel (bg fill, then per-pixel 8-bit alpha blend from a glyph atlas whose coverage is ~100 % nonzero, i.e. every pixel blended — real text has ~20–30 % coverage), -O2, no hand SIMD. Upload = XShmPutImage to a server-side pixmap of the same size, timed to a GetInputFocus round-trip (server has finished the copy). No window mapped, so compositor is NOT included. External HDMI 1920x1080@60 monitor was attached (root 4800x1800); numbers below use explicit sizes.

| 2880x1800 (A panel), 20.7 MB fb | ST raster | upload | total |
|---|---|---|---|
| first frame after 15 s idle | 33.9 ms | 6.3 ms | 40.1 ms |
| warmed (5 runs) | 13.1–22.7 ms (steady 13.1) | 1.4–1.9 ms | 14.5–24.6 |
| MT 4 bands after idle | 14.5 ms | | |
| MT 4 bands warmed | 7.2–8.3 ms | | |
| MT 8 bands warmed | 4.8–6.3 ms | | |
| one text-line strip (2880x36) | 0.44 ms | 0.64 ms | 1.08 ms |

| 1920x1080 (B-class), 8.3 MB fb | ST raster | upload | total |
|---|---|---|---|
| first frame after 15 s idle | 15.2 ms | 4.8 ms | 20.0 ms |
| warmed | 5.2–12.7 ms (steady 5.2) | 0.6–1.4 ms | 5.8–17.3 |
| MT 4 after idle / warmed | 5.2 / 3.0–3.4 ms | | |
| MT 8 warmed | 1.9–2.2 ms | | |
| one line strip (1920x36) | 0.38 ms | 0.42 ms | 0.80 ms |

Implications (A, T/2 = 5.56 ms):
- CPU raster is **compute-bound, not bandwidth-bound**: 13.1 ms steady for 5.18 Mpx ≈ 10 cycles/px at ~4 GHz vs the modeled 0.5 cycle/px; the memcpy stand-in (2.2 ms) underestimated naive CPU raster by 6x. A hand-SIMD blend (16 px/iter) would plausibly reach 2–4 ms but is unmeasured; with realistic ~25 % glyph coverage the blend work drops ~3–4x, also unmeasured.
- **After-idle penalty is 2.6x (33.9 / 13.1), not the 1.37x inferred from memcpy.** Upload after idle 6.3 ms vs 1.4 ms = 4.5x (server-side wake + clock).
- Single-thread CPU full-frame raster on A **fails G3 by 2.4x warm and 6x after idle**; 8-thread banded raster sits at the T/2 edge warm and has no idle margin. → **GPU raster is required on A** (not merely preferred); banded MT CPU raster is the fallback only.
- B-class 1080p: ST warm 5.2 ms fits T/2 = 8.33 ms (1.6x slack); after idle 15.2 ms fails; MT4 fits in both states. → on B, CPU raster must be multi-threaded or the first frame after idle will drop.
- **One-line update costs ~1.1 ms on A (0.44 raster + 0.64 server round-trip)**, vs 0.08 ms modeled: the per-keystroke app stage of 0.3 ms is wrong by ~3.5x with this naive kernel and a synchronous server sync. Without the sync wait (fire-and-forget put + flush) the app-side cost is the 0.44 ms raster; the 0.64 ms is server latency the app does not wait on but which precedes compositor readiness. G1 (2 ms p99 at ingress) is still met but with ~2x slack, not 7x.

# Addendum 3 (2026-10-08): optimized SSE2 blend kernel + realistic coverage — MEASURED on A (AC, powersave), raster only (no upload)
Same layout as Addendum 2 (bg fill then glyph blend from atlas) but SSE2 4-px blend with 16-bit math, and atlas coverage set to 25 % nonzero (realistic text) or 100 %. Pure raster timing; add Addendum 2's upload (1.4 ms warm / 6.3 ms after idle at 2880x1800) for the full CPU path.

| 2880x1800 | SSE2 25 % | SSE2 100 % | scalar 25 % |
|---|---|---|---|
| ST first frame after 15 s idle | 20.1 ms | 24.1 ms | 35.5 ms |
| ST warmed (best / mean of 10) | 5.83 / 6.45 ms | 5.59 / 6.32 ms | 14.9 / 15.1 ms |
| MT4 after idle | 5.39 ms | 5.50 ms | 10.6 ms |
| MT4 warmed best | 3.12 ms | 4.76 ms | 6.61 ms |
| MT8 warmed best | 3.16 ms | 3.09 ms | 3.41 ms |
| one line strip | 0.40 ms | 0.31 ms | 0.34 ms |

| 1920x1080, SSE2 25 % | |
|---|---|
| ST after idle / warmed best | 10.9 / 2.00 ms |
| MT4 after idle / warmed best | 3.57 / 1.13 ms |

Implications:
- With a decent SIMD kernel, coverage no longer matters (5.6 vs 5.8 ms): the single-thread CPU path on A is **memory-traffic/overhead bound at ≈ 5.6–5.8 ms warm**, i.e. exactly T/2 with zero margin, before the 1.4 ms upload. The "compute-bound" statement in Addendum 2 applies to the naive scalar kernel only.
- **A, warm:** MT4 raster 3.1 + upload 1.4 = 4.5 ms < 5.56 → multi-threaded CPU path is a viable warm fallback. **A, first frame after idle:** MT4 5.4 + upload 6.3 = 11.7 ms ≫ 5.56 → CPU path fails after idle regardless of threading. ST after idle 20 ms. → GPU raster required on A for the after-idle case; the after-idle penalty (3.4x ST, 1.7x MT4) is the dominant risk, and it is a clock/power-state effect, not a kernel effect.
- **B-class 1080p:** ST warm 2.0 ms (4x slack vs T/2 = 8.33), ST after idle 10.9 ms (fails), MT4 after idle 3.6 ms (fits). → B's CPU path must be multi-threaded, as already concluded.
- One text-line strip raster 0.3–0.4 ms is kernel-independent → it is overhead/bandwidth, and together with the 0.64 ms server round-trip it fixes the app-stage PB at ≈ 0.5–0.6 ms app-side for the CPU path.
