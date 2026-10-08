# perf/ — performance target for the editor

Method ("perf-target"): pick concrete hardware targets, derive planning bounds for every editor operation from
hardware limits (memory bandwidth/latency, cache, CPU, display scanout, input path, storage, OS floors), set
each ship gate as a stated multiple of the bound, then have it adversarially reviewed until the numbers and the
pass/fail contracts stop moving.

| File | What it is |
|---|---|
| `00-hardware-profile.md` | Measured profile of Target A (this ThinkPad X1 Carbon Gen 11) + three addenda of follow-up measurements: SIMD scan rate, WM map latency, native GTK init, full-frame CPU raster + XShm upload (naive and SSE2 kernels), after-idle penalties. |
| `01-perf-target.md` | **The target document (current: v3.4).** Evidence-tagged planning bounds, p50/p99 ship gates for Targets A (laptop, enforced), B (office desktop fixture, enforced), C (low-end, provisional), measurement method, chosen architecture, assumptions, rejected review points, changelog. |
| `01-perf-target-v1.md`, `-v2.md` | Archived earlier versions. |
| `02-review-arithmetic.md`, `-systems.md`, `-gates.md` | Round-1 adversarial reviews of v1 (Codex gpt-6.1-sol, xhigh), one angle each. |
| `02-review-verify.md` | Round-2 verification of v2 (findings F1–F12). |
| `02-review-verify3.md` | Round-3 verification of v3.2 (findings R1–R8; concluded no gate limit needs to change). |
| `02-review-verify4.md` | Round-4 scoped close-out of v3.3. |

Benchmark sources: `bench/`. Derivation by a Claude Opus subagent; reviews by `codex exec -m gpt-6.1-sol -c model_reasoning_effort=xhigh -s read-only`.

## Outstanding measurements (priority order, from the reviews)
1. Target A **on battery**: SSE2 MT4/MT8 and GPU full frames through real completion, warm and after ≥ 15 s idle, plus first-key strip; everything in the addenda was measured while charging.
2. Target A on battery: newline/ASCII scan, dense counting, adversarial verifier on a new mapping (faults included), under bulk-worker contention.
3. The real **B fixture** (not A hardware at 1080p): raster/upload through DXGI/GDI completion, then optical tails with photodiode; monitor and keyboard acceptance.
Then: GPU first frame after idle incl. device wake; whether DRI3/persistent-mapped buffers avoid the 4.5× server-upload wake; NVMe APST exit; perf counters for the raster bottleneck attribution.
