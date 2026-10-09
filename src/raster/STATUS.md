# raster P2.5 — Session 5 handoff

Done:
- Audited and kept the inherited scalar/exact SSE2 kernels, snapshots, MT4
  WORK_RASTER jobs, XShm off-screen pixmap, Sync fence, matching PresentPixmap
  and IdleNotify, metrics, frozen conformance include and benchmarks.
- Fixed uploads to issue from UI work events, preventing failed-submit side
  effects; added failing-then-passing upload-order regression.
- Bounded 64-slot per-job SSE2 colour tables, direct SSE2 fallback after 64
  rebuilds; no atlas-derived image caching or typing-path allocation.
- QueryFence before PresentPixmap (T5 = upload device completion plus T4),
  reply-queue recheck before sleeping; new red/green request-order regression.
- Correct typing damage, mutation-through-submit counting guard, paired kernel
  comparisons, --track and correct CPU minimap allowance in bench output.
- Rejected and removed the slower eight-pixel experiment; streaming kernel kept
  tested as a benchmark candidate, not used by the backend.

Verification COMPLETE:
- Final xvfb-run -a -s '-screen 0 2880x1800x24 -noreset' make -C WT check:
  23 test binaries passed, ASan/UBSan + leaks, including frozen conformance.
- Release live test with active allocation guard: both regressions, pixel readback
  and zero allocations through mutation/submit passed.
- make -C WT fuzz: 13 fuzzers built. Final kernel differential fuzz: 142,882
  inputs in 301 s, no mismatch/sanitizer findings (scalar/SSE2/stream/cached).
- Complete shared --track matrix (no --quick): 2,000 full-frame samples per size,
  typing, 10,000 scroll frames, five 15 s idle samples per size, init, paired
  kernels, allocation counts and per-stage p50/p99. All X runs isolated in Xvfb.
- Final .text: raster.o 6,219 + raster_kernel.o 5,170 = 11,389 B, TRACK.

Evidence / remaining acceptance:
Read docs/decisions/P2.5.md FIRST: full report, exact RED/GREEN/check/bench/fuzz
output, choices, counter evidence and limitations are there. Paired cached/direct
kernel p50 ratios: 0.747 (M)[bat] 15 px, 0.835 (M)[AC] 30 px, both TRACK under load.
Full-frame TRACK p50/p99: 26.252/46.537 ms 15 px [bat], 8.845/16.403 ms 30 px [AC].
Power changed during scroll; its synthetic MSC miss count is not a physical verdict.
0 guarded input-to-submit allocations; 7 exempt libxcb allocations per present.

Next: coordinator runs G3 5.0/5.56 ms and G3z on quiet [AC] hardware. No acceptance
verdict from loaded Xvfb. Minimap not built (CPU MT4 allowance 0.26 ms (E)).
No Makefile or frozen interface edits; retained src/raster/LDLIBS=-lxcb-sync.
No additional implementation task is claimed closed by the indicative timings.
