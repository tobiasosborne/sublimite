Implemented §1–§4 and §7–§9. §5 now refuses Keep on compromised mapped backing; full rebasing is proposed. §10 consolidates signal state into one service and proposes the required global exception.

Changed files: `src/file/file.c`, `file.h`, new `file_test.h`, `check_signal_globals.py`, `STATUS.md`, and `tests/file_test.c`. Decisions and complete evidence: [P1.7c.md](/home/tobias/Projects/editor/.wt/edit-4w1.41/docs/decisions/P1.7c.md).

Red runs, each exit 1:

```text
§1 FAIL (ok & 1) != 0
   FAIL (ok & 2) != 0
§2 signal chain ignore: FAIL status=139
   signal chain default: FAIL status=23296
   signal chain mask: FAIL status=256
   signal chain info: FAIL status=256
   signal chain reset: FAIL status=23552
§3 FAIL atomic_load(&worker_watch_calls) == 0
§4 FAIL wait_msg(FILE_MSG_SAVE_DONE, &m) && m.status == FILE_ERR_CHANGED
   FAIL got[8192] == 'x' && got[8193] == '\n'
   finding4 forced late fault: FAIL
§5 FAIL file_resolve_keep(f) == FILE_ERR_CHANGED
   FAIL file_changed(f)
§7 FAIL file_save_write(&a) == FILE_OK
§8 FAIL c.n == 0
§9 FAIL atomic_load(&sysconf_calls) == before
§10 finding10: FAIL mutable globals=4 g_installed,g_old_bus,g_once,g_slots
```

Final green runs, each exit 0:

```text
finding1: ok
finding2: ok
finding3: ok
finding4: ok
finding4 source identity: ok
finding4 invalidated generation: ok
finding4 forced late fault: ok
finding5: ok
finding7: ok
finding8: ok
finding9: ok
finding10: ok mutable globals=1 file_bus
file_test: ok
```

Verification:

```text
make all: exit 0 (gcc release)
make check: check: 33 test binaries passed
test_replay_cli: all passed
make fuzz: fuzz: 19 fuzzers built
TSan -no-pie, FT_CASE=3: finding3: ok; file_test: ok
```

The sandboxed check initially failed on IPC socket EPERM; the unrestricted rerun passed. ASan/UBSan used `detect_leaks=0`.

File fuzz, (M)[AC, Not charging; load1=7.58]:

```text
Done 9941893 runs in 121 second(s)
```

Bench ran exactly once, exit 0. All rows **TRACK (M)[AC, Not charging; load1=5.46]**, with no gate verdict:

```text
G5_sparse10g   p50=1523329 ns p99=4649840 ns
G5_oneline1g   p50=1422013 ns p99=3511985 ns
G5_log1g       p50=1473870 ns p99=1913460 ns
G5_small_copy  p50=1993174 ns p99=2466490 ns
G8s_save_ack   p50=14826 ns   p99=1858049 ns
G8d_1MB        p50=4.089 ms   p99=13.345 ms
G8d_1GB        elapsed=1318 ms
```

Open items: §5 full rebase/stable-generation design, coordinator adoption of §10’s narrow global exception, and coordinator LSan verification. Frozen headers, bench code, journal, existing kill-test functions, and later findings were left unchanged.