Implemented standalone IPC: parsing, canonical paths, binary stdin, socket handoff, ACK/`--wait`, stale recovery, startup arbitration, and allocation-free server draining.

Changed `src/ipc/{ipc.h,args.c,wire.c,socket.c,STATUS.md}`, `tests/ipc_test.c`, `bench/ipc_bench.c`, `fuzz/ipc_fuzz.c`, `tools/{edit.desktop,xdg-install.sh}`, and [P4.9.md](/home/tobias/Projects/editor/.wt/edit-457.9/docs/decisions/P4.9.md). No other modules were edited.

Red run, before implementation:

```text
undefined reference to `ipc_parse_args'
undefined reference to `ipc_server_init'
collect2: error: ld returned 1 exit status
make: *** [Makefile:87: build/tests/ipc_test] Error 1
```

Green runs:

```text
ipc_test: parser, wire, roundtrip, --wait, stdin, stale, race, fragments, tokens, endpoints ok
make all: exit 0
check: 29 test binaries passed
test_replay_cli: all passed
fuzz: 16 fuzzers built
```

Final IPC tests passed under gcc and clang ASan/UBSan. Installer syntax passed; registration was not executed.

Fuzz: **2,648,714 runs in 61 seconds (M)[AC]**, power Full, load1=11.97; no crashes or sanitizer findings.

Single TRACK benchmark, **(M)[AC]**, power Full, load1=10.67:

```text
n=200 p50=248824ns p99=1912933ns
gate_p99=10000000ns (G) pass=1
```

Open work: editor/tab integration is deliberately deferred and documented. Limits are a 1 MiB frame and 32 connected clients. Coordinator leak checking and quiet-box gate verdict remain pending.