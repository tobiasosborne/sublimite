Implemented §8–§10. Frozen headers and checkpoint ownership remain intact; ordinary edits consult no checkpoint state.

Changed [piece.c](/home/tobias/Projects/editor/.wt/edit-4w1.38/src/piece/piece.c), [piece_test.h](/home/tobias/Projects/editor/.wt/edit-4w1.38/src/piece/piece_test.h), [piece_bounds_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.38/tests/piece_bounds_test.c), [piece_mem_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.38/tests/piece_mem_test.c), [STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.38/src/piece/STATUS.md), and [P1.4f.md](/home/tobias/Projects/editor/.wt/edit-4w1.38/docs/decisions/P1.4f.md).

Red — (M)[AC], Not charging, load 5.55:

```text
trim work: pieces=16384 delete=0 slabs=2353 returns=144 bound=160
FAIL trim_work:173: st.slabs_scanned <= limit
range work: tree nodes=40955 bound=9357 height=4
FAIL range_work:195: st.walk_nodes <= limit
reference work: replay=0 bytes=1048576 bound=510 descents=16
FAIL ref_work:224: st.ref_recount_bytes <= 510
```

Green — (M)[AC], Not charging, load 6.01; assertion bounds are (G):

```text
trim work: pieces=16384 delete=0 slabs=80 returns=144 bound=160
trim work: pending returns length=0 slabs=64 bound=64
range work: tree nodes=9357 bound=9357 height=4
reference work: replay=0 bytes=0 bound=510 descents=2
reference prefix rollback: ok (saved partial block; pinned/unpinned transaction snapshot)
piece_bounds_test: ok
typing allocator: malloc_calls=0 guard=active
```

Verification results:

```text
make all: exit=0
fuzz: 22 fuzzers built
make check with Xvfb access: exit=2
remaining sanitizer checks: binaries=20 failures=0
test_replay_cli: all passed
```

Build stamp: (M)[AC], load 5.89. Check stamp: (M)[AC], load 6.05. Remaining checks: (M)[AC], load 16.80. All were Not charging.

All piece sanitizer suites passed. Full `make check` stalled in `raster_test` polling Xvfb; I terminated only this worktree’s test. Its sandbox fallback failed platform initialization. No raster or CLI code changed.

Both requested fuzz campaigns passed — (M)[AC], Not charging, load 6.01:

```text
Done 207244 runs in 121 second(s)
piece_fuzz: exit=0
Done 36988 runs in 121 second(s)
piece_checkpoint_fuzz: exit=0
```

Exactly one full matrix ran, TRACK — (M)[AC], Not charging, load 10.40, 2026-10-09T16:20:23+08:00:

```text
BENCH row=typing_1e4 col=insert p50=0.105 p99=0.397 unit=us n=9474 gate=p99<=50us status=PASS
BENCH row=undo_1e4 col=batch_total p50=71.252 p99=280.837 unit=ms n=11 gate=p50<=63ms,p99<=84ms status=MISS
BENCH row=undo_1e4 col=insert_ref p50=1.258 p99=2.193 unit=us n=10000 gate=none status=TRACK
BENCH row=snapshots_100 col=snapshot_take p50=0.686 p99=4.231 unit=us n=100 gate=none status=TRACK
piece full matrix TRACK: exit=1
```

All memory gates printed PASS; only the undo batch printed MISS. Historical p99 movement: viewport 10.394→20.367 µs, typing insert 0.147→0.397 µs, snapshot take 4.520→4.231 µs. Historical values are (M)[bat], load unrecorded; the uncontrolled comparison includes intervening kernel changes. Full comparisons are in the decision doc.

Open: raster verification, LeakSanitizer rerun, and inherited snapshot/overflow-reference funding issues. No benchmark rerun, git, or bd commands were used.