# Bench policy (2026-10-09, Tobias via consultant)

Tobias: "I am a bit concerned we are getting sucked into benchmark process porn and progress theatre. Are all the benchmarks necessary? What new info do we gain?" Decision: all five points below, applied from session 6.

## What benches bought us so far
- Decisions: the P1.4 kernel matrix picked bptree. The find pick (simd vs two-way) and the GL pick (EGL vs GLX) are pending and are the same kind: one quiet run each.
- Open questions only a quiet run settles: lineidx G7 (117/207 ms vs 80/125) and G7j (53.7 vs 30/50 ms), layout log_1g row, raster G3, scan_count 11.2 vs 12 GB/s. All measured so far on a loaded box: noise.
- Theatre: re-running rows with 10–30x headroom (undo 1.8 ms vs 63; font 2.4 us vs 50), any gate on a loaded box or on battery ("MISS (loaded)" lines nobody acts on), piece matrix r2/r3 repeats after r1 passed 36/36, power/load stamps on TRACK rows. Law 1 as written ("make bench must pass before a bead closes") re-triggered the full bench set on every cherry-pick.
- The number that matters, keystroke to pixel on the real display, needs the editor loop (P3.3, in flight). Module gates are proxies until then.

## Policy
1. One quiet [AC] run per module when it lands, numbers recorded in its decision doc. No repeats unless a row is within 20 % of its gate.
2. Rows with > 5x headroom become a loose assert in the module test (e.g. 10x the measured p99), no measurement ritual.
3. Open bench items are exactly: the find pick, the GL pick (real display, announced first), and one quiet investigation run for lineidx, layout log_1g, raster G3/G3z, scan. Other bench-only beads close as "measured once, headroom recorded".
4. Law 1 amended (CLAUDE.md, PLAN.md §1): bench exists and passed once on a quiet box; later beads re-run it only when they touch that module's hot path.
5. Priority: M0 (editor loop) and the first real-display end-to-end latency trace (tools/tracedump over the trace ring) outrank every remaining module gate. After M0 the trace number is the primary gate; module benches guard what the trace shows is hot.

## Evidence tags
Unchanged (law 4) for gate verdicts. TRACK rows need no power stamp.

## Addendum 14:45 (Tobias)
After the first real-display run (G1 submit 1.5/14.9 ms, pixels 9.7/27.5 ms p50/p99 under load 8): "I am happy with the key timings so far. I don't see huge value in benchmarking on a quiet box yet. Let us develop further and if Astra or I notice any lag we can revisit. Noisy timings are *far* more representative of work."
- The single quiet investigation run is cancelled. zzj.9, e6x.5, 4w1.6 close with their loaded numbers recorded as (M)[AC, loaded].
- Variant picks (find simd vs twoway, GL egl vs glx) are made on back-to-back runs on the box as it is, same minute, interleaved A/B/A/B; the pick is relative, not a gate verdict.
- Perf beads are opened from observed lag (Tobias, Astra P5.1/P5.1b) with a repro, not from gate rows.
- Gate rows stay in the benches as regression guards that run when a bead touches the module; a miss under load is a note, not a blocker.
