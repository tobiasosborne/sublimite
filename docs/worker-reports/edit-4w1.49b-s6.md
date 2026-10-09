Finished. All requested checks are green.

Changed:

- [tests/journal_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.49/tests/journal_test.c:1069): exact quota is now `WORK_MAX_JOBS-WORK_RASTER_RESERVE-1`. Journal reservation, successful flush, and unrelated-message delivery checks remain intact.
- [tests/raster_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.49/tests/raster_test.c:784): both final collection steps continue bounded drains while messages remain. Delivery, identity, and ordering assertions remain intact.
- Updated [P1.8c.md](/home/tobias/Projects/editor/.wt/edit-4w1.49/docs/decisions/P1.8c.md) and [work STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.49/src/work/STATUS.md). No implementation changes were needed.

Red runs, **(M)[AC]**, battery `Not charging`:

```text
journal, load1=2.17, exit 1:
journal_test:1069 FAIL submitted==WORK_MAX_JOBS-2

raster --review 5, load1=4.40, exit 1:
raster_test:787: FAIL strip_delivered
raster_test:830: FAIL review_saturated_delivery()==0
P2.5b section 5: RED
```

Final green runs, **(M)[AC]**, battery `Not charging`, load1=4.70:

```text
make all: exit 0
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
P2.5b section 5: GREEN
raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)
check: 39 test binaries passed
test_replay_cli: all passed
make check: exit 0
fuzz: 21 fuzzers built
make fuzz: exit 0
```

All execution used `DISPLAY=:99 EDIT_DISPLAY=:99`. Full checks ran outside network isolation to reach Xvfb; sanitizers used `detect_leaks=0`. Release journal and focused raster tests also passed.

Fuzz runs/time: no campaign executed; `make fuzz` builds only. No work fuzzer exists. Corpus unchanged.

Bench: no rerun for these test/docs changes; prior stamped TRACK rows remain in P1.8c.md.

Open problems: no remaining finishing-run blockers. Leak verification remains for the coordinator; existing out-of-scope findings remain untouched.