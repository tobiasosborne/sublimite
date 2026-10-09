Verification complete; no in-scope recovery or retry defect found. **No code changes.** Source hashes confirm all temporary reverts were restored.

Updated [P1.9.md](/home/tobias/Projects/editor/.wt/edit-4w1.32/docs/decisions/P1.9.md) with paired red/green evidence and fresh Session 6 results, trimmed old fuzz logs, and added [STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.32/src/journal/STATUS.md).

Each existing regression failed with its relevant fix hunk reverted, then passed restored. Charging [AC]; load 6.48 for save/retry, 6.36 for barriers:

```text
BLOCKER 2 RED:
journal_test:247 FAIL journal_replay_file(path,load_save,&c,&rr)==0
journal_test:331 FAIL verify_save(path,"Xdef")==0
GREEN:
journal_test: save ok (file_save_begin, retained generation, post-save edits, two buffers, marker/rename/dir failures)

MAJOR 3 RED:
journal_test:118 FAIL d->retry_offset==17
GREEN:
journal_test: retry ok (short write + EIO, two batches, exact offset, checkpoint replacement)

MAJOR 8 RED:
journal_test:196 FAIL journal_get_stats(j).error==JOURNAL_IO && journal_get_stats(j).durable_sequence==0
GREEN:
journal_test: barriers ok (durable image, creation, 64KiB straddle, sync/rename/dir failures)
```

Fresh build/check results:

```text
make all: exit 0 — GCC release; [AC], load 5.54
check: 23 test binaries passed
test_replay_cli: all passed
fuzz: 12 fuzzers built
journal_test: malloc_guard=active append_allocations=0
```

Full check used ASan/UBSan with `detect_leaks=0`, `DISPLAY=:99 EDIT_DISPLAY=:99`, and local Xvfb socket access. Launch loads: check 4.30, fuzz build 4.73, release allocator check 5.73, all [AC].

Fuzz (M)[AC], launch load 4.12, clean:

```text
#76830 DONE cov: 2439 ft: 10224 corp: 571/23Kb lim: 128 exec/s: 255 rss: 170Mb
Done 76830 runs in 301 second(s)
```

Kill test (M)[AC], launch load 4.10:

```text
journal_kill_test: ok trials=1000 script_edits=100000 issued=49375204 replayed=48296482 acknowledged=47173632 (ack<=replayed<=issued; piece_tree==independent_byte_model_at_replayed)
elapsed_seconds=167.04
```

One benchmark invocation, exit 0. **All rows TRACK (M)[AC], launch load 3.56**; printed pass fields are local comparisons. Replay throughput counts wire bytes.

```text
TRACK journal_worker_cpu syncs=8 cpu_us_per_sync=169.526 undrained_50ms_cpu_us_per_sync=0.000 worker_return=1 (M)[AC]
BENCH name=journal_append_1B_(M)_ns n=100000 p50=180 p99=264 ci95=[179,180] gate_p50=0 gate_p99=20000 pass=1 power=[AC]
TRACK journal_replay mode=piece_tree payload=1 records=100000 MB_s=48.42 records_s=1100615 (M)[AC]
TRACK journal_sync payload=1 syncs=98 max_bytes=45056 last_bytes=28672 max_interval_ms=2.859 (M)[AC] threshold=64KiB_or_1s
BENCH name=journal_append_1KiB_(M)_ns n=100000 p50=3823 p99=5193 ci95=[3819,3825] gate_p50=0 gate_p99=20000 pass=1 power=[AC]
TRACK journal_replay mode=piece_tree payload=1024 records=100000 MB_s=231.25 records_s=212538 (M)[AC]
TRACK journal_sync payload=1024 syncs=2344 max_bytes=65536 last_bytes=36864 max_interval_ms=7.505 (M)[AC] threshold=64KiB_or_1s
TRACK journal_idle_sync interval_ms=1009.946 bytes=4096 (M)[AC]
```

Open work: coordinator quiet-box gate verdicts and LeakSanitizer-enabled check; unchanged cadence decision edit-4w1.34 and deferred MAJOR 4–7/9 plus MINOR findings in edit-4w1.33. No unresolved in-scope problem.