Completed both beads. Changed `src/journal/journal.c`, `journal.h`, `STATUS.md`, `tests/journal_test.c`, `fuzz/journal_fuzz.c`, `bench/journal_bench.c`, and [P1.9.md](/home/tobias/Projects/editor/.wt/edit-4w1.33/docs/decisions/P1.9.md:613).

Flush reports sticky FULL after draining accepted records. Logical INSERT admission is atomic; default pastes fit without allocation or blocking, and CRC runs on the worker. Large checkpoints receive sized bounds. Paths are canonical and rotation uses a retained directory fd. Session schemas, base conflicts, temporary names, and exact-prefix oracles are fixed.

The shared queue costs **2 MiB + 8 KiB (E)** plus arena overhead; G10f accounting is documented. Sync defaults are unchanged. The post-publication return was already present; verified against a restored-wait variant. No `src/work` changes.

Red evidence, all exit 1: initial tests Charging [AC], load 3.65; bench/worker load 3.74; prefix/path variants load 15.46; base-read variant Full [AC], load 5.82.

```text
MAJOR 4: journal_test:498 FAIL journal_flush(j)==JOURNAL_FULL
MAJOR 5 split/atomic: journal_test:521 FAIL rc==JOURNAL_OK && allocations==0
MAJOR 6: journal_test:569 FAIL journal_rotate(j,cp,count+4)==JOURNAL_OK
MAJOR 7: journal_test:596 FAIL b.path[0]=='/' && !strcmp(b.path,basepath)
MAJOR 9: journal_bench:152 FAIL rc==0 && allocations==0
MINOR 10 seeded extra-prefix-drop: journal_test:743 FAIL rr.valid_bytes==boundary && rr.records==count && rr.last_sequence==count
MINOR 11 writer: journal_test:624 FAIL journal_append(j,JOURNAL_WINDOW,99,window,sizeof window)==JOURNAL_INVALID
MINOR 11 parser: journal_test:635 FAIL rr.corrupt && rr.valid_bytes==start && rr.records==which && rr.last_sequence==which
MINOR 12 old IO classification: journal_test:654 FAIL journal_capture_base_with_io(basepath,&b,&io)==JOURNAL_BASE_CHANGED
MINOR 13 basename: journal_test:672 FAIL journal_rotate(j,&cp,1)==JOURNAL_OK
MINOR 13 full path: journal_test:709 FAIL journal_rotate(j,&cp,1)==JOURNAL_OK
edit-4w1.31 restored wait: journal_test:382 FAIL atomic_load(&pool.slots[i].busy)==0
```

Corresponding green runs, all exit 0; full final journal suite Full [AC], load 12.54:

```text
journal_test: FULL flush reports suspension; complete checkpoint clears it
journal_test: split 1 MB paste accepted, replay bytes exact, allocations=0
journal_test: atomic 1 MiB paste accepted, replay bytes exact, allocations=0
journal_test: untitled checkpoint >64 MiB restores exact content and resumes appends
journal_test: canonical base and journal paths survive cwd changes
journal_test: session IDs rejected by writer
journal_test: session IDs rejected by CRC-repaired parser
journal_test: unreadable/short/missing bases are conflicts; replay leaves log intact
journal_test: 255-byte journal basename rotates with independent temporary name
journal_test: 4095-byte journal path rotates via retained directory fd
journal_test: journal rotation survives parent directory rename
journal_test: exact scripted corruption/truncation prefix (first page, 64 KiB, PAD, byte contents)
journal_test: worker returns with undrained completion
journal_test: malloc_guard=active append_allocations=0
journal_bench: ok (default options, exact content, TRACK only)
```

Final verification: all commands exited 0. Builds Full [AC], load 9.59; check/fuzz/kill Full [AC], launch load 5.30. Check used DISPLAY=:99, ASan/UBSan, LSan disabled, and socket access so X11 tests executed.

```text
make all exit=0
check: 24 test binaries passed
test_replay_cli: all passed
make check exit=0
fuzz: 12 fuzzers built
Done 51079 runs in 301 second(s)
journal fuzz exit=0
journal_kill_test: ok trials=1000 script_edits=100000 issued=49692225 replayed=47661008 acknowledged=47173632 (ack<=replayed<=issued; piece_tree==independent_byte_model_at_replayed)
elapsed_seconds=211.77 (M)[AC] load1=5.30
```

Bench TRACK rows; every fixture uses default options. Delivery waits are reported separately from enqueue timing. Before/after CPU loads differ.

```text
TRACK journal_worker_cpu syncs=8 cpu_us_per_sync=641.824 undrained_50ms_cpu_us_per_sync=474.407 worker_return=0 (M)[AC] load1=11.70
TRACK journal_worker_cpu syncs=8 cpu_us_per_sync=144.213 undrained_50ms_cpu_us_per_sync=0.000 worker_return=1 (M)[AC] load1=5.84
TRACK journal_paste bytes=1000000 requests=100 p50_ms=0.168 p99_ms=0.229 allocations=0 guard=active content_verified=1 endpoint=journal_enqueue G9_full_frame_ms=5/15_(G)_unmeasured (M)[AC] load1=5.84
TRACK journal_append payload=1 edits=100000 p50_ns=110 p99_ns=544 local_budget_ns=20000_(E) timer_ms=5 final_flush=1 bulk_jobs=60 bulk=index_scan/find_scan/save_write_standins (M)[AC] load1=6.33
TRACK journal_backpressure payload=1 delivery_pauses=0 total_pause_ms=0.000 max_pause_ms=0.000 measured_enqueue_excludes_delivery_wait=1 (M)[AC] load1=6.33
TRACK journal_replay payload=1 edits=100000 records=100006 wire_MB_s=16.12 inserted_payload_MB_s=0.21 records_s=314821 content_verified=1 base_load=1 buffers=2 session_restored=1 (M)[AC] load1=6.33
TRACK journal_sync payload=1 syncs=79 max_bytes=65536 max_interval_ms=27.944 timer_driven=1 (M)[AC] load1=6.33
TRACK journal_append payload=1024 edits=100000 p50_ns=143 p99_ns=848 local_budget_ns=20000_(E) timer_ms=5 final_flush=1 bulk_jobs=60 bulk=index_scan/find_scan/save_write_standins (M)[AC] load1=7.11
TRACK journal_backpressure payload=1024 delivery_pauses=288 total_pause_ms=1608.822 max_pause_ms=26.843 measured_enqueue_excludes_delivery_wait=1 (M)[AC] load1=7.11
TRACK journal_replay payload=1024 edits=100000 records=100006 wire_MB_s=85.00 inserted_payload_MB_s=76.96 records_s=150283 content_verified=1 base_load=1 buffers=2 session_restored=1 (M)[AC] load1=7.11
TRACK journal_sync payload=1024 syncs=864 max_bytes=65536 max_interval_ms=17.516 timer_driven=1 (M)[AC] load1=7.11
TRACK journal_default_exhaustion accepted=127 wire_bytes=67104768 limit=67108864 flush=FULL durable_prefix=1 content_verified=1 reason=disk_budget (M)[AC] load1=7.11
TRACK journal_idle_sync interval_ms=1002.496 bytes=4096 saw_unsynced=0 (M)[AC] load1=7.26
TRACK journal_zero_data_sync interval_ms=1001.535 bytes=8192 saw_unsynced=1 (M)[AC] load1=7.26
```

No unresolved implementation finding in scope. Coordinator checks remain: whole-editor G1/G9, integrated RAM accounting, actual index/find integration, quiet-box battery verdicts, and LSan. The loss-window decision remains edit-4w1.34. Full evidence and verification commands are in [STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.33/src/journal/STATUS.md).