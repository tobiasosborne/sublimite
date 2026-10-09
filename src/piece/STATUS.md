# Piece status — P1.4f / edit-4w1.38

Review P1.4-1 §8–§10 are implemented: synchronized occupancy/partial/empty
slab queues replace live-pool scans; queries and snapshot takes drain a fixed
batch of completed slabs; range reads and deletion collection/fallback use a
local ancestor stack; ADD newline prefixes and batched descriptor insertion
bound reference replay recounts independently of payload size.

Two-slot slab sizes and leaf/branch/snapshot slot sizes are unchanged. Compact
ADD prefixes are allocator-charged. No frozen/public header, other module,
Makefile, frozen referee, corpus or fuzzer source was changed. Private work
counters are absent from ordinary release objects. The counting regressions
check reclamation work at different live-pool sizes, pending worker returns on
empty/nonempty trees, tree/snapshot traversal and content, large replay, deep
bulk COW, full newline totals and prefix-cache restoration after checkpoints.

P1.4d's owner/growth/uint64_t fixes and P1.4e's checkpoint/lifetime fixes remain
intact. Checkpoint ownership, allocation-free endpoints, snapshots and ref
rules are preserved. The restored ADD boundary cache is repaired during abort;
ordinary edits consult no checkpoint state. The sole inactive checkpoint
branch remains in snapshot take. The frozen public iterator still uses its
four-word traversal state; the range APIs need no header change.

Design and pasted red/green evidence: `docs/decisions/P1.4f.md`.

Verify on Xvfb only:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/fuzz/piece_fuzz -max_total_time=120
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/fuzz/piece_checkpoint_fuzz -max_total_time=120
```

Focused sanitizer probes: `build/san/tests/piece_bounds_test --trim-work`,
`--range-work`, `--ref-work`. `build/tests/piece_mt_test` verifies the active
release malloc guard. Run the full matrix once with a power/load stamp:
`DISPLAY=:99 EDIT_DISPLAY=:99 tools/bench_variant.sh src/piece`; results are
TRACK on this shared box, with no timing verdict or repeated benchmark chase.

Final GCC `make all`, sanitizer/fuzzer builds and `make fuzz` pass. Final build
stamp: (M)[AC], Not charging, load 5.89, 2026-10-09T16:15:30+08:00.
Final counting probes and the release allocator test pass, including zero
malloc calls with the active guard (M)[AC], Not charging, load 6.01,
2026-10-09T16:16:19+08:00. Both piece fuzzers completed the requested campaign:
207244 / 36988 executions, each reporting 121 s for a 120 s request (M)[AC],
Not charging, load 6.01, 2026-10-09T16:16:19+08:00; no findings.

The complete `make check` is **not green**: sandbox CLI could not connect to
Xvfb. With authorized :99 socket access it passed every suite before raster,
including all piece and checkpoint rows, then stalled in raster's Xvfb poll.
Only this worktree's stalled raster process was terminated. Its sandbox
fallback fails platform initialization; no raster/CLI source was changed.
Check stamp: (M)[AC], Not charging, load 6.05, 2026-10-09T16:15:59+08:00.
All remaining sanitizer binaries and replay CLI pass (M)[AC], Not charging,
load 16.80, 2026-10-09T16:23:59+08:00. Existing native EGL/Present and undo
atomicity skips remain. LeakSanitizer remains a coordinator check.

Exactly one full TRACK matrix ran. Its only printed MISS is undo_1e4 batch,
71.252 / 280.837 ms p50/p99 (M)[AC], Not charging, load 10.40,
2026-10-09T16:20:23+08:00. All memory gates print PASS; no timeout; huge-only
row SKIP. The decision records raw key cells and changes versus the historical
synthesis matrix, whose old load was unrecorded. This is an uncontrolled
comparison across intervening kernel changes, not a P1.4f timing verdict.
No benchmark was rerun. Full output: `/tmp/p14f-evidence/matrix.log`.

Existing P1.4e open contracts remain unchanged: the fixed snapshot allowance
at arbitrary depth and immutable boundary copies, and requested overflow-ref
ADD funding. Optional `piece_mem_test --depth-contract` and `--requested`
continue to expose those issues. All ordinary memory rows retain their
original G10f bounds. There is no remaining implementation work for §8–§10.
The out-of-scope raster check failure and coordinator validation remain.
