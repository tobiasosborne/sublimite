All required checks are green. The callers now drive asynchronous open/save receipt to completion while preserving G5’s first-viewport endpoint and the G8 measurement points.

Changed files:

- [tests/file_kill_test.c](/home/tobias/Projects/editor/.wt/edit-4w1.42/tests/file_kill_test.c): mailbox waits, prefix/cell oracle, phase ordering, and save retirement; kill/cancel and allocation checks retained.
- [bench/file_bench.c](/home/tobias/Projects/editor/.wt/edit-4w1.42/bench/file_bench.c): asynchronous acquisition, failure handling, internal-message decoding, and untimed retirement.
- [fuzz/file_fuzz.c](/home/tobias/Projects/editor/.wt/edit-4w1.42/fuzz/file_fuzz.c): kind-specific waits, phase assertions, and asynchronous nonregular-file rejection.
- [P1.7d.md](/home/tobias/Projects/editor/.wt/edit-4w1.42/docs/decisions/P1.7d.md:408) and [file/STATUS.md](/home/tobias/Projects/editor/.wt/edit-4w1.42/src/file/STATUS.md): changes and evidence recorded.

Red, exit=1, ASan/UBSan/LSan on `:99`, (M)[AC, Not charging; load1=4.86]:

```text
file_kill_test:157: FAIL tree && f && file_attach(f, tree) == FILE_OK
file_kill_test:164: FAIL piece_delete(tree, 0, 1, NULL) == 0 && piece_insert(tree, 0, &byte, 1) == 0
file_kill_test:172: FAIL rc == FILE_OK
file_kill_test:179: FAIL atomic_load(&job.done) && job.seen == 1 && job.completion.status == FILE_OK && job.completion.generation == generation && job.completion.size == sizeof bytes
file queued-open probe: FAIL prefix bytes before first viewport
```

Green release run, exit=0, (M)[AC, Not charging; load1=11.21]:

```text
file_save_alloc: PASS pooled fresh ack guard=active
file_kill_test: PASS visibility/cancellation; power-loss barriers unverified
```

Verification used `DISPLAY=:99 EDIT_DISPLAY=:99`. Full check ran outside the sandbox with leaks enabled.

```text
make all: exit=0                 (M)[AC, Not charging; load1=7.50]
check: 45 test binaries passed
test_replay_cli: all passed
make check: exit=0, leaks=1      (M)[AC, Not charging; load1=14.74]
fuzz: 23 fuzzers built
make fuzz: exit=0                (M)[AC, Not charging; load1=7.50]
Done 9289 runs in 121 second(s)
file_fuzz: exit=0                (M)[AC, Not charging; load1=11.21]
```

Fuzz used `-max_total_time=120 -timeout=20 -max_len=4096`, sandbox `detect_leaks=0`; no findings or corpus regeneration.

Single TRACK benchmark: exit=0, launch (M)[AC, Not charging; load1=12.79]. All warm correctness checks completed. Endpoint rows below; every primitive row is recorded in the decision file.

```text
TRACK G5_warm_sparse10g_busy0 (M)[AC] load1=12.79 n=100 p50=5.674 ms p99=13.300 ms gate=6/9 ms (G)
TRACK G5_warm_oneline1g_busy0 (M)[AC] load1=12.56 n=100 p50=5.551 ms p99=12.292 ms gate=6/9 ms (G)
TRACK G5_warm_log1g_busy0 (M)[AC] load1=12.52 n=100 p50=9.028 ms p99=22.825 ms gate=6/9 ms (G)
TRACK G5_warm_unicode_copy_busy0 (M)[AC] load1=12.52 n=100 p50=7.053 ms p99=11.270 ms gate=6/9 ms (G)
TRACK G8s_fresh_fragmented_status_1MiB_busy0 (M)[AC] load1=13.08 n=100 p50=0.495 ms p99=1767.672 ms gate=2/5 ms (G)
TRACK G8d_1MiB_warm_source (M)[AC] load1=13.08 n=100 p50=20.591 ms p99=1767.675 ms gate=10/50 ms (G)
TRACK G5_warm_sparse10g_busy1 (M)[AC] load1=10.20 n=100 p50=87.164 ms p99=1637.473 ms gate=6/9 ms (G)
TRACK G5_warm_oneline1g_busy1 (M)[AC] load1=8.56 n=100 p50=81.705 ms p99=1562.403 ms gate=6/9 ms (G)
TRACK G5_warm_log1g_busy1 (M)[AC] load1=7.08 n=100 p50=73.460 ms p99=1554.167 ms gate=6/9 ms (G)
TRACK G5_warm_unicode_copy_busy1 (M)[AC] load1=5.95 n=100 p50=49.969 ms p99=1713.709 ms gate=6/9 ms (G)
TRACK G8s_fresh_fragmented_status_1MiB_busy1 (M)[AC] load1=5.69 n=100 p50=110.420 ms p99=1610.579 ms gate=2/5 ms (G)
TRACK G5_warm_sparse10g_busy3 (M)[AC] load1=7.04 n=100 p50=99.128 ms p99=1487.430 ms gate=6/9 ms (G)
TRACK G5_warm_oneline1g_busy3 (M)[AC] load1=9.29 n=100 p50=239.708 ms p99=1228.041 ms gate=6/9 ms (G)
TRACK G5_warm_log1g_busy3 (M)[AC] load1=8.67 n=100 p50=263.491 ms p99=1053.222 ms gate=6/9 ms (G)
TRACK G5_warm_unicode_copy_busy3 (M)[AC] load1=7.70 n=100 p50=241.270 ms p99=1014.971 ms gate=6/9 ms (G)
TRACK G8s_fresh_fragmented_status_1MiB_busy3 (M)[AC] load1=7.22 n=100 p50=233.816 ms p99=968.962 ms gate=2/5 ms (G)
TRACK primitive_unchanged_save_enqueue_1MiB_busy0 (M)[AC] load1=6.25 n=100 p50=0.009 ms p99=0.017 ms gate=0/0 ms (G)
TRACK G8s_fresh_fragmented_status_1GiB_busy0 (M)[AC] load1=7.02 n=10 p50=0.599 ms p99=0.854 ms gate=2/5 ms (G)
TRACK G8d_1GiB_warm_source (M)[AC] load1=7.02 n=10 p50=3075.193 ms p99=3762.822 ms gate=1500/2500 ms (G)
```

Many loaded-box rows exceed gates; the benchmark initially overlapped `make check`. No benchmark rerun or cold verdict. Existing journal preparation, physical close, piece metadata, BULK-only open, whole-editor integration, and power-loss durability proposals remain open.