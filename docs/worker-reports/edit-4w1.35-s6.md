Implemented findings **2–8** and edit-4w1.26. **BLOCKER 1 remains unresolved under the permitted exception**: the atomicity regression is compiled but skipped by default; the opt-in remains red. The frozen piece header was unchanged.

Changed: [undo.c](/home/tobias/Projects/editor/.wt/edit-4w1.35/src/undo/undo.c), [undo.h](/home/tobias/Projects/editor/.wt/edit-4w1.35/src/undo/undo.h), [undo_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.35/tests/undo_test.c), [undo_bench.c](/home/tobias/Projects/editor/.wt/edit-4w1.35/bench/undo_bench.c), [undo_fuzz.c](/home/tobias/Projects/editor/.wt/edit-4w1.35/fuzz/undo_fuzz.c), [P1.5d.md](/home/tobias/Projects/editor/.wt/edit-4w1.35/docs/decisions/P1.5d.md), [STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.35/src/undo/STATUS.md).

Changes cover guaranteed capture capacity, packed storage with decommit and committed-memory accounting, bounded reclamation, sliced replay with a pre-group view, real-tree G9 gating, backend-label rejection, and expanded failure/fuzz coverage.

Red regressions — (M)[AC], Charging, load1=4.50; benchmark probes use synthetic samples (E):

```text
review 1: allocation=1 successful_prefix=1 tree_len=1048576 original=1048577
undo_test:305: FAIL piece_len(t)==BIG+1 && piece_read(t,0,b,BIG+1)==0 && memcmp(b,text,BIG)==0 && b[BIG]=='Z'
undo_test:247: FAIL undo_undo(&u,1,&c)==0 && c.groups==1
review 3: cleared resident pool=6402048 allowance=4096
undo_test:265: FAIL n<=4096
review 4: redo records reclaimed by key=100000 bound=16
undo_test:274: FAIL reclaimed<=16
review 5: first slice rc=0 records=100000 groups=1
undo_test:285: FAIL rc==UNDO_MORE && c.records==1 && c.groups==0 && !c.has_state
undo_bench:58 failed: replay_verdict(&real,&mock)==1
undo_bench:61 failed: !accepts_backend_label("--bptree-indicative")
undo_test:317: FAIL view && piece_snapshot_len(view)==0
```

Green regressions — (M)[AC], Charging, load1=3.43:

```text
review 1: SKIP group atomicity; docs/decisions/P1.5d.md piece.h amendment proposal
review 2: expansion and one-slot admission passed
review 3: cleared resident pool=0 allowance=4096
review 4: redo records reclaimed by key=0 bound=16
review 5: first slice rc=4 records=1 groups=0
review 6: real miss rejected
review 7: label-only option rejected
review 8: redo failure positions=14 passed
review 8: completed-group state and dirty range across batch failures passed
undo_test: 10000 keys mallocs=0 guard=active bytes/record=64
undo_test: all passed
```

Final validation:

```text
make all: PASS, exit 0
check: 23 test binaries passed
test_replay_cli: all passed
fuzz: 12 fuzzers built
```

`make check` used `DISPLAY=:99 EDIT_DISPLAY=:99`, approved Xvfb socket access, and `ASAN_OPTIONS=detect_leaks=0`.

Full fuzz — (M)[AC], Charging, initial load1=3.28:

```text
Done 521407 runs in 301 second(s)
FUZZ elapsed_s=301.03 exit=0
```

An earlier attempt exposed premature trimming in the independent model. The corrected model passes the reduced reproducer and the full run.

Single benchmark run — **TRACK**, (M)[AC], Charging, load1=3.74; exit 0:

```text
BENCH name=undo_10k_in_tree_TRACK n=31 p50=2005664 p99=2089733 ci95=[2000789,2054569] gate_p50=58000000 gate_p99=78000000 pass=1 power=[AC]
BENCH name=undo_10k_bookkeeping_mock_G n=31 p50=425297 p99=447824 ci95=[421196,434180] gate_p50=6300000 gate_p99=6300000 pass=1 power=[AC]
MEMORY_RECLAIM cap=1 active=0 retired_before=99984 committed_before=6402048 committed_after=0 piece_owned=133328 G10f=PASS
```

Open: allocation-free whole-group rollback requires the exact **piece.h amendment proposal** in P1.5d.md. Slice deadlines cannot interrupt one piece call. Large cap reductions may discard extra oldest history. Quiet-box gate verdicts, integrated frame measurements, and leaks-on verification remain with the coordinator.