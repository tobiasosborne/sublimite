# Layout status — edit-zzj.9 / P3.1b

Implemented: caller-arena checkpoint table; lazy snapshot WORK_BULK build;
mailbox adoption; exact binary column seeks and known line-end tail jumps;
strict prefix invalidation on edits and stale generation rejection; bounded
unindexed estimates and tail work; utf8_cluster_step across windows/slices;
fixed cluster scratch and exact width for oversized clusters; bounded exact
segmentation/width memo; physical-read and consumed-byte counters.

Tests: RED recorded before implementation; checkpoint publication, deep work
bounds, insert/delete invalidation, stale result, long/window-crossing clusters,
malformed cache context, short line followed by long line all GREEN. Release
allocation guard: 0 allocations over 10,000 relayouts. Final libFuzzer campaign:
915 runs / 311 seconds, no findings; independent naive column/cell model over
synthetic >64 KiB lines and actual src/work publication. make fuzz builds 12.

Bench: final (M)[bat], loaded. Indexed 1 GiB columns 10^6 and 10^8:
16.577/25.594 us and 13.147/20.523 us p50/p99 vs 150 us (G); indicative.
Checkpoint table reserves 4,194,336 B, layout object 53,568 B. Unindexed deep
viewport consumes 358 bytes and reads one 16 KiB window. Unicode p50 improves
2.69x and malformed 2.53x in the same session, with varying load. ASCII/log
also miss on battery; no battery result is a gate verdict.

Missing: coordinator's quiet [AC] verdict for normal 300-row layout,
particularly Unicode/malformed. These rows remain TRACK; a conditional consultant proposal
(uncached Unicode 400/450 us, malformed 225/250 us) is documented, not imposed. Arbitrarily long visible graphemes are bounded per call
but require multiple calls; >16 KiB contiguous glyph composition draws '?'
with approximate=true. One attached store caches one line; P3.2/P3.3 callers
must reserve/select stores, request snapshots and rebuild the full viewport
after adopting a complete index. The worker currently publishes a whole line
including its end, not incremental region descriptors.

Verification: full make check PASSED (22 sanitizer test binaries) on private Xvfb with -noreset, outside
socket-restricted sandbox. Ordinary xvfb-run failed to bind in the sandbox;
reset-enabled Xvfb outside it reproduced existing intermittent plat_init
failures. All standalone x11_stall_test cases pass with -noreset. LSan disabled
per HANDOFF; coordinator should rerun with leaks enabled. No X11 code edited.

Next: read docs/decisions/P3.1b.md, then layout.h and checkpoint.c. Re-run the
layout bench quietly on AC. If both percentiles pass, gate the Unicode and
malformed rows at 150 us; otherwise consult using the recorded stage breakdown
and cache miss counts. One additional loaded [AC] run still missed ordinary rows, while exact deep
rows measured 8.745/16.523 and 7.128/13.303 us; no gate verdict was claimed.
No Makefile changes needed. Do not externally cancel a
checkpoint job; route its mailbox completion or join pool shutdown before
freeing its arena. Invalidate cached sources even while detached.
