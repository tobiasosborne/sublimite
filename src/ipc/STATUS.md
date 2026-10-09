# ipc — P4.9 / edit-457.9

Implemented: public caller-owned API, CLI canonicalization/positions/options,
binary stdin, strict bounded wire codec, nonblocking epoll server, blocking
startup client with ACK/closed responses, wait tokens, lifecycle-locked stale
socket recovery and startup arbitration, abstract fallback, credential checks,
user desktop/MIME installer, tests, benchmark, decoder fuzzer.

Standalone by coordinator decision; editor/tab dependency dropped. Main.c and
other modules are untouched. Editor/tab wiring and actually launching the
installed CLI remain the separate integration bead. See
[Integration contract](../../docs/decisions/P4.9.md) for exact ownership,
threading, callback lifetimes, startup policy and the five-line wiring sketch.

Bounds: whole request <= 1 MiB, <= 128 paths, path/cwd <= 4096 bytes including
NUL, <= 32 connected clients. No receive allocations after init. Incomplete
idle peers retain bounded slots until disconnect; oversize stdin returns an
error. These are explicit API limits, not unfinished implementation.

Verify (DISPLAY/EDIT_DISPLAY must stay :99):

```
env DISPLAY=:99 EDIT_DISPLAY=:99 make all
env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
env DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
env DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/ipc_test
env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/ipc_fuzz -max_total_time=60 -max_len=8192
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
env DISPLAY=:99 EDIT_DISPLAY=:99 ./build/bench/ipc_bench
sh -n tools/xdg-install.sh
```

Unix socket execution needs the approved local-socket environment; the default
sandbox returns EPERM. LSan is disabled for sandbox sanitizer runs; the
coordinator reruns with leaks on. Never invoke xdg-install.sh in any test.
Benchmark results are TRACK; coordinator supplies quiet-box verdicts.

Verification completed 2026-10-09:

- `make all`: exit 0, gcc release; final incremental run reports nothing to do.
- `make check`: exit 0; `check: 29 test binaries passed`, replay CLI passed.
  Final expanded IPC test also passed separately in both release and ASan/UBSan.
- `make fuzz`: exit 0; `fuzz: 16 fuzzers built`.
- Decoder fuzz: 2,648,714 runs in 61 seconds (M)[AC], power Full,
  pre-run load1=11.97; no crashes or sanitizer findings.
- Single TRACK bench: n=200, p50=248824 ns, p99=1912933 ns (M)[AC],
  power Full, load1=10.67; p99 <= 10000000 ns (G), pass=1.
- `sh -n tools/xdg-install.sh`: exit 0; installer not executed.

No module implementation work remains. Integration and coordinator leak/quiet
box checks are the deliberately deferred follow-up described above.
