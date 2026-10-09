Implemented atomic undo/redo group replay. One checkpoint spans a group’s slices; failures restore exact tree bytes and undo metadata, while completed batch groups remain committed.

Changed: `src/undo/{undo.c,undo.h,STATUS.md}`, `tests/undo_test.c`, `fuzz/undo_fuzz.c`, `bench/undo_bench.c`, and [P1.5e.md](/home/tobias/Projects/editor/.wt/edit-4w1.40/docs/decisions/P1.5e.md). No editor changes.

Removed the NOMEM prefix/retry lock, error-only snapshot stand-in, clear/destroy acceptance of intermediate content, and environment gate. Retained `UNDO_MORE` and the borrowed view for normal slicing.

Red on supplied main, **(M)[AC], Not charging, load1=7.62**, exit 1:

```text
UNDO_TEST_REQUIRE_ATOMIC_GROUP=1 ./build/tests/undo_test 1
review 1: allocation=1 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=2 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=3 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=4 successful_prefix=1 tree_len=1048576 original=1048577
undo_test:340: FAIL c.records==0 && c.groups==0 && !c.has_state && u.partial==0
```

Green by default, **(M)[AC], load1=10.27**, exit 0:

```text
review 1: allocation=4 successful_prefix=0 tree_len=1048577 original=1048577
review 1: exhaustive allocator sweep failures=212 after_slice=84; bytes, queries, records and cursors restored
review 1: completed undo groups survive a later group abort
review 5: slice clear/destroy abort and out-of-band mutation assertion passed
review 8: completed-group state and dirty range across batch failures passed
undo_test: P1.5e all passed
```

Verification, all **(M)[AC], Not charging**:

- `make all`: exit 0, GCC release; load1=10.27.
- `make check`: `check: 42 test binaries passed`; `test_replay_cli: all passed`; load1=7.27.
- Release `editor_test`: all passed, both typing guards report zero allocations; load1=6.31.
- `make fuzz`: `fuzz: 22 fuzzers built`; load1=10.27.
- Undo fuzz: 303127 runs / 301 seconds, exit 0; load1=7.55.
- Final rebuilt-fuzzer follow-up: 101604 runs / 61 seconds, exit 0; load1=12.75.

Single benchmark at **2026-10-09T16:10:23+08:00**, **(M)[AC], load1=6.39, TRACK**, exit 0. Times below are ns; gate columns are **(G)**:

```text
MEMORY (M)[AC] live=640000 records=10000 bytes/record=64 committed=643072 reserved_virtual=5767168 piece_owned=495028 gate=(G)G10f
MEMORY_RECLAIM (M)[AC] cap=1 active=0 retired_before=99984 committed_before=6402048 committed_after=0 piece_owned=133048 G10f=PASS
BENCH name=undo_10k_in_tree_TRACK n=31 p50=31433541 p99=43063527 ci95=[31367704,31756472] gate_p50=58000000 gate_p99=78000000 pass=1 power=[AC]
BENCH name=undo_10k_bookkeeping_mock_G n=31 p50=378502 p99=400175 ci95=[376637,381396] gate_p50=6300000 gate_p99=6300000 pass=1 power=[AC]
```

No new functional blocker remains. The slice assertion relies on the current kernel’s snapshot cache; checkpoint terminal and rollback work remain indivisible. LeakSanitizer was disabled as instructed. Existing piece checkpoint funding limitations remain outside this bead.