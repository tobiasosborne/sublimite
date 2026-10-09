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
env DISPLAY=:99 EDIT_DISPLAY=:99 ./build/tests/ipc_test --parallel
env DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 ./build/san/tests/ipc_test --parallel
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

edit-457.17 test isolation: abstract fallback now supports the documented
`EDIT_IPC_NAMESPACE` test hook; the unset default and filesystem endpoints
retain their names. Tests always use a private per-process namespace, inherited
by forked clients, and cover independent abstract endpoints and hook bounds.
Callback regressions force an accepted incomplete peer before releasing the
client, drain until actual callback completion, and log unexpected ipc_result
values. A generous readiness/client deadline replaces scheduling-sensitive
startup waits; exit status still asserts the expected result kind.

Red release parallel loop: 8/80 failures (M)[AC], Full, load1=7.34.
Controlled callback red: IPC_TIMEOUT (6), expected IPC_REJECTED (8), child
exit 2 (M)[AC], Full, load1=6.50; accepting a peer was mistaken for sending ACK.
Green release parallel loop: 0/80 failures (M)[AC], Full, load1=5.03.
Green ASan/UBSan parallel loop: 0/80 failures (M)[AC], Full, load1=11.17.
Gcc `make all`: exit 0 (M)[AC], Full, load1=5.03.
Clang ASan/UBSan `make check`: 33 test binaries and replay CLI checks passed,
exit 0 (M)[AC], Full, load1=6.32; DISPLAY/EDIT_DISPLAY=:99, detect_leaks=0.
`make fuzz`: 19 fuzzers built, exit 0 (M)[AC], Full, load1=6.32.
IPC decoder fuzz: 2,619,395 runs in 61 seconds, exit 0, no sanitizer
findings (M)[AC], Full, load1=11.17; no persistent corpus generated.
Benchmark not rerun: startup/test-only change, no typing-path change.

Runtime rename (edit-457.18):
Filesystem and abstract socket names now use sublimite-<uid>, including
.lock paths and the existing EDIT_IPC_NAMESPACE suffix. Desktop entry and
installer use sublimite/sublimité. Verify ipc_test on :99 and
sh tools/test_runtime_identity.sh after make all. IPC/editor routing remains
the existing separate integration work.
Decision/evidence: ../../docs/decisions/rename-sublimite.md.
Final rename verification: gcc make all passed; clang ASan/UBSan make check
passed with leaks disabled; make fuzz built all fuzzers. Focused identity and
isolated desktop-install contracts passed. Complete red/green and stamped
fuzz results are in the decision document; no hot-path bench was rerun.

Amendment (edit-457.18): desktop Name/StartupWMClass use lower-case
`sublimité`/`sublimite`; the isolated installer contract passes. Socket and
binary names already use `sublimite`; no IPC source changes were needed.
Amendment evidence: ../../docs/decisions/rename-sublimite.md.
Amendment final verification: gcc make all, clang ASan/UBSan make check
(detect_leaks=0), make fuzz and the focused contracts passed; no new open
problem. Stamped X11 fuzz smoke evidence is in the decision document.
