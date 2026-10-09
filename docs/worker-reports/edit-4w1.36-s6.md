Completed P1.4d: checked snapshot owner cap, insertion-growth refusals, and permanent uint64_t boundary coverage. Added no production allocations or globals.

Changed files:

- [piece.c](/home/tobias/Projects/editor/.wt/edit-4w1.36/src/piece/piece.c), [piece_test.h](/home/tobias/Projects/editor/.wt/edit-4w1.36/src/piece/piece_test.h): checked arithmetic, owner saturation, private hooks, and the high-offset pointer fix.
- [piece_bounds_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.36/tests/piece_bounds_test.c), [piece_mt_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.36/tests/piece_mt_test.c), [piece_fuzz.c](/home/tobias/Projects/editor/.wt/edit-4w1.36/fuzz/piece_fuzz.c): boundary tests, final-owner-slot race, wide offsets, and retained-reference growth.
- [P1.4d.md](/home/tobias/Projects/editor/.wt/edit-4w1.36/docs/decisions/P1.4d.md), [STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.36/src/piece/STATUS.md): design, evidence, and verification.

Relevant red/green lines:

```text
§1 RED — exit 1
FAIL owner_boundary:30: piece_snapshot_retain(s) == NULL
piece_bounds_test: FAILED

§1 GREEN — exit 0
snapshot owner boundary: ok (retain/take refuse saturation; mapping balanced)
piece_bounds_test: ok

§2 RED — exit 1
FAIL insert_boundary:69: piece_insert(t, UINT64_MAX - 1, (const uint8_t *)"zz", 2) == PIECE_ERR_RANGE
FAIL ref_boundary:99: piece_insert_ref(t, UINT64_MAX - 1, &r) == PIECE_ERR_RANGE
FAIL ref_sum_boundary:132: piece_insert_ref(t, 0, &r) == PIECE_ERR_RANGE
piece_bounds_test: FAILED

§2 GREEN — exit 0
insert growth boundary: ok (cursor/COW/subtree sums; exact UINT64_MAX; refusal unchanged)
reference growth boundary: ok (sum spans before mutation; exact UINT64_MAX)
reference span-sum overflow: ok (rejects before allocation; tree unchanged)
piece_bounds_test: ok

§11 RED — UBSan halt, exit 1
src/piece/piece.c:854:46: runtime error: addition of unsigned offset to 0x531000000801 overflowed to 0x5310000007ff

§11 GREEN — exit 0
uint64_t cursor offset: ok (subtract logical start before pointer addition)
uint64_t positions: ok (4295098368 logical bytes; 2^32/chunk/EOF/range; snapshot after destroy)
piece_bounds_test: ok
```

Verification:

```text
make all: PASS, exit 0
make check: check: 29 test binaries passed
test_replay_cli: all passed
make fuzz: fuzz: 15 fuzzers built
snapshot owner race: ok (one winner for the final slot; no wrap)
```

Piece fuzz: **182,751 runs in 121 seconds**, no findings; requested limit 120 s. **(M)[AC] Full, load 12.85**, `2026-10-09T13:25:17+08:00`.

Release typing allocator: **malloc_calls=0, guard=active**. **(M)[AC] Full, load 12.54**, `2026-10-09T13:26:18+08:00`.

Exactly one before/after `piece_bench --quick` pair; no full matrix:

```text
TRACK (M)[AC] Charging, load 6.63 — 2026-10-09T13:14:11+08:00
typing_1e4 insert:      p50=0.058 p99=0.314 us
typing_1e4 batch_total: p50=0.019 p99=0.023 ms

TRACK (M)[AC] Full, load 4.58 — 2026-10-09T13:28:45+08:00
typing_1e4 insert:      p50=0.060 p99=0.357 us
typing_1e4 batch_total: p50=0.019 p99=0.022 ms
```

Open checks: quiet-box performance verdict and LSan-enabled verification remain with the coordinator. Live raster/X11 checks skipped on sandbox Xvfb socket failures. Findings §3–§10 remain untouched.