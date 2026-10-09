Fixed §6: retained recovery bases now use a validated private reflink or bounded worker copy, synced before PREPARED publication. P1.9d/P1.9e semantics and fsync defaults remain intact.

Changed files:

- [journal.c](/home/tobias/Projects/editor/.wt/edit-4w1.45/src/journal/journal.c): independent retention and worker copy.
- [journal.h](/home/tobias/Projects/editor/.wt/edit-4w1.45/src/journal/journal.h): comments only.
- [journal_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.45/tests/journal_test.c): external-writer, copy and barrier regressions.
- [STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.45/src/journal/STATUS.md): current verification.
- [P1.9f.md](/home/tobias/Projects/editor/.wt/edit-4w1.45/docs/decisions/P1.9f.md): verdict, design and evidence.

Red: both external-writer commands failed (M)[AC], load1=4.45; copy oracle failed (M)[AC], load1=6.02:

```text
journal_test:247 FAIL journal_replay_file(path,load_save,&c,&rr)==0
journal_test:479 FAIL verify_save(path,"Xdef")==0
retained-fd exit=1; retained-alias exit=1
journal_test:563 FAIL rc==0 && save.prepared && faults.writes>=4 && faults.worker_io
retained-copy exit=1
```

Green: the same commands passed (M)[AC], load1=5.20:

```text
journal_test: retained recovery survives old-fd truncate/overwrite (replacement, acknowledged post-save edits, restart before finish)
journal_test: retained recovery survives hard-link alias overwrite (replacement, acknowledged post-save edits, restart before finish)
journal_test: retained worker copy ok (short writes, exact multi-chunk bytes, mutation rejection, data/directory failures before publication)
```

Verification used `DISPLAY=:99 EDIT_DISPLAY=:99`; power was Not charging [AC]:

```text
make all: exit=0                                      [AC] load1=4.71
make check: exit=0; 33 test binaries passed        (M)[AC] load1=3.00
test_replay_cli: all passed                           [AC] load1=3.00
make fuzz: exit=0; 19 fuzzers built                 (M)[AC] load1=3.00
journal_test: malloc_guard=active append_allocations=0 (M)[AC] load1=5.42
journal_fuzz: exit=0; Done 18779 runs in 121 second(s) (M)[AC] load1=4.83
```

The fuzzer used the existing corpus and the requested 120-second limit (G), with no findings. Full check required approved local socket access after sandbox IPC EPERM; ASan/UBSan stayed enabled and LSan was disabled.

One benchmark run, TRACK only; launch [AC], load1=11.50:

```text
TRACK journal_paste bytes=1000000 requests=100 p50_ms=0.218 p99_ms=0.400 allocations=0 guard=active content_verified=1 endpoint=journal_enqueue G9_full_frame_ms=5/15_(G)_unmeasured (M)[AC] load1=11.62
TRACK journal_append payload=1 edits=100000 p50_ns=136 p99_ns=668 local_budget_ns=20000_(E) timer_ms=5 final_flush=1 bulk_jobs=60 bulk=index_scan/find_scan/save_write_standins (M)[AC] load1=11.62
TRACK journal_append payload=1024 edits=100000 p50_ns=138 p99_ns=904 local_budget_ns=20000_(E) timer_ms=5 final_flush=1 bulk_jobs=60 bulk=index_scan/find_scan/save_write_standins (M)[AC] load1=11.49
journal_bench: ok (default options, exact content, TRACK only)
```

Open problems: none in this finding. Copy fallback requires space for the previous generation. LSan and quiet-box gates remain coordinator checks; cadence remains edit-4w1.34.