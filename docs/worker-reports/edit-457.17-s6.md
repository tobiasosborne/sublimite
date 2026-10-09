Completed edit-457.17. Changed `src/ipc/socket.c`, `src/ipc/ipc.h`, `tests/ipc_test.c`, `docs/decisions/P4.9.md`, and `src/ipc/STATUS.md`.

Tests now inherit a unique `EDIT_IPC_NAMESPACE`. The default endpoint remains unchanged. `callback_results` now drains until the callback runs; accepting a connection had incorrectly triggered blocking `waitpid`, causing `IPC_TIMEOUT`. Tests use generous hang bounds and assert the expected result kind.

Red (M)[AC], Full, load1=7.34:
```text
ipc_test parallel: 20 rounds x 4 processes = 80 runs, 8 failures
red exit=1
```

Controlled callback red (M)[AC], Full, load1=6.50:
```text
callback_results: reject=1 ipc_result=6 expected=8 (IPC_TIMEOUT=6)
callback red exit=1
```

Green release (M)[AC], Full, load1=5.03:
```text
ipc_test parallel: 20 rounds x 4 processes = 80 runs, 0 failures
green exit=0
```

Green ASan/UBSan (M)[AC], Full, load1=11.17:
```text
ipc_test parallel: 20 rounds x 4 processes = 80 runs, 0 failures
san parallel exit=0
```

Required checks, all on `DISPLAY=:99 EDIT_DISPLAY=:99`:

```text
make all exit=0                 (M)[AC], Full, load1=5.03
check: 33 test binaries passed  (M)[AC], Full, load1=6.32
test_replay_cli: all passed
make check exit=0
fuzz: 19 fuzzers built          (M)[AC], Full, load1=6.32
make fuzz exit=0
```

IPC fuzz (M)[AC], Full, load1=11.17:
```text
Done 2619395 runs in 61 second(s)
ipc fuzz exit=0
```

No sanitizer findings. Bench not rerun: startup/test changes only. No new open problems; coordinator leak-on verification remains because these sanitizer runs used `detect_leaks=0`.