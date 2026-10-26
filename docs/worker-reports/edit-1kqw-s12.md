# edit-1kqw — W2.3 worker report

## Verdict and scope

Done within the assigned worktree scope, with declared Windows dependency
SKIPs. Linux release/full sanitizer check and final Windows release/sanitizer
check passed. Production Windows file/journal integration is a coordinator
merge boundary, explicitly skipped by the controller fixtures here.

Read the binding rules and brief first, then the required local context. No
other checkout was read or modified; no state-changing git command or bd was
run. `src/os/os.h` is unchanged. No editor/main implementation was changed.

## What changed

- savectl now uses os.h files, directories, mappings, randomness, sleep, native
  diagnostics and temporary-directory selection. Its private reload cache keeps
  exclusive creation/unlink fallback, clone-or-copy policy, positioned short-I/O
  loops, identity validation, and mapped backing lifetime. Windows accepts native
  absolute paths. Directory sync uses `os_dir_sync`; no read-only file handle is
  flushed. Save temporary creation remains the file module's responsibility.
- `socket.c` became `ipc_posix.c` with the entire original body unchanged inside
  `#if !defined(_WIN32)`. A byte comparison of the original saved body and the
  wrapped file passed. Linux parser semantics and wire layout remain unchanged.
- Windows IPC uses a user-SID-named pipe and `Local` mutex, a user-only DACL,
  remote-client refusal, and peer token SID checks. One service thread handles
  overlapped connections/reads/writes through an I/O completion port. Bounded
  receive slabs, callback/byte drain budgets, incomplete-request expiry, idle
  eviction, live wait tokens, disconnect publication, ACK/close replies and
  cancellation/reaping are implemented. The loop gets `ipc_server_wake`; callbacks
  remain on the loop thread. No UI wait for pipe I/O and no thread per client.
- Windows path parsing supports drive and UNC roots, existing-prefix
  canonicalization, missing final names and line/column suffixes. Binary stdin
  preserves CRLF/NUL/Ctrl+Z and restores the CRT descriptor mode.
- Windows launcher pair/adopt/close primitives are implemented; native Windows
  main/self-spawn integration remains W2.4.
- `mk/win.mk` adds only savectl/ipc to the module list and Advapi32 to import
  libraries. Windows suites enter through the existing test glob. The OS
  white-box direct-object link excludes savectl, whose file/journal dependencies
  are absent here. Controller test adapters switch off automatically once both
  production dependency modules join `WIN_MODULES`.

Design and the complete pre-edit call-family inventory:
`docs/decisions/edit-1kqw.md`. Raw inventory:
`build/worker-logs/inventory-before.txt`.

## Files touched

`src/savectl/savectl.c`, `src/savectl/savectl.h`, `src/ipc/ipc.h`,
`src/ipc/args.c`, `src/ipc/wire.c`, `src/ipc/socket.c` (removed),
`src/ipc/ipc_posix.c`, `src/ipc/ipc_win.c`, `mk/win.mk`,
`tests/win/ipc_win_test.c`, `tests/win/ipc_transport_test.c`,
`tests/win/savectl_fixture.h`, `tests/win/savectl_test.c`,
`tests/win/savectl_reload_test.c`, this report, the decision document, and the
worktree's HANDOFF/worklog entry. `args.c` needed actual portability work despite
being described as portable in the brief; its initial Windows compile proved
that boundary.

## Environment and evidence convention

All measurements: **(M) [AC, desktop Spiral]**. BAT0/status is absent, as the
binding rules say for this desktop. Benchmark helpers print `unknown` for that
absence; the stamp here is from the actual hardware/rules, not an invented
battery reading. `uptime` snapshots (M), copied from tool output:

```text
11:59:01 up  4:15,  1 user,  load average: 8.56, 5.41, 3.11
12:19:27 up  4:35,  1 user,  load average: 7.35, 5.58, 4.60
12:29:32 up  4:45,  1 user,  load average: 6.89, 7.62, 6.34
```

Counts below derive from the command's own verdict/RESULT lines. Test hang and
protocol bounds reuse existing IPC constants; they are fixture gates (G), not
new performance budgets. Unit conversions are (P): milliseconds × 1,000,000 =
nanoseconds. The allocation seam covers service + peer table + each receive
slab: 2 + IPC_RX_SMALL_COUNT + IPC_RX_BIG_COUNT = 2 + 16 + 2 = 20 allocations
(P: structure allocation sites; G: existing pool capacities).

## Red runs

### Windows migration compile

Before implementation, after adding savectl/ipc to `WIN_MODULES`:

```text
make TARGET=win -j64 win-compile
exit=2
src/savectl/savectl.c:8:10: fatal error: 'sys/mman.h' file not found
src/ipc/socket.c:4:10: fatal error: 'poll.h' file not found
src/ipc/args.c:55:20: error: call to undeclared function 'realpath'
src/ipc/args.c:135:12: error: call to undeclared function 'lstat'
src/ipc/args.c:166:73: error: implicit conversion loses integer precision
```

Full output: `build/worker-logs/win-red-compile.log`.

### Windows IPC contract test before transport implementation

```text
make TARGET=win -j64 build/win/rel/tests/ipc_win_test.exe
exit=2
ld.lld: error: undefined symbol: ipc_server_drain
ld.lld: error: undefined symbol: ipc_server_token_live
ld.lld: error: undefined symbol: ipc_server_report_closed
```

Full output: `build/worker-logs/win-ipc-red.log`. The test was written before
`ipc_win.c` and self-reexecutes via `os_proc_spawn` for actual second-process
handoff and single-instance refusal.

### Binary stdin regression

```text
tools/winrun.sh build/win/rel/tests/ipc_win_test.exe
exit=1
ipc_win_test:62: a.request.stdin_size==sizeof data && !memcmp(a.request.stdin_data,data,sizeof data)
```

The test deliberately gave the reader a text-mode CRT pipe containing CRLF and
Ctrl+Z. Full output: `build/worker-logs/win-stdin-red.log`.

### Setup allocation failure regression

```text
tools/winrun.sh build/win/san/tests/ipc_transport_test.exe
exit=1
ERROR: AddressSanitizer: access-violation on unknown address 0x000000000000
The signal is caused by a WRITE memory access.
#0 ... o_memset ... ucrtbase.dll
```

An injected failed service allocation exposed an unchecked pointer in the new
Windows implementation. Every setup allocation is now checked and cleaned up.
Full red output: `build/worker-logs/win-oom-red.log`.

## Green runs

### Linux release and full sanitizer check

```text
make -j64 all
exit=0
ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99 make -j64 check
exit=0
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
ipc_test: parser, wire, roundtrip, --wait, stdin, stale, race, fragments, tokens, endpoints, review-fix suites ok
savectl_reload_test: ok
savectl_test: ok
check: 104 test binaries passed
```

(M) Count is the full make recipe's own count; no Linux test expectations were
edited. Logs: `build/worker-logs/linux-all.log`, `linux-check.log`,
`linux-check.exit`; final release rebuild: `linux-all-final.log`.
The display-lock contract suite intentionally prints lock timeout/error lines
for its rejection fixtures and finishes PASS; these are not aggregate failures.

Initial sandboxed executions could not create Unix sockets (`EPERM`) and
LeakSanitizer reported that it could not inspect processes. The successful runs
lifted those sandbox restrictions, retaining `detect_leaks=1`. Leak detection
was never disabled. Windows interop initially failed with
`UtilBindVsockAnyPort: socket failed 1`; Windows runs likewise used the authorized
restriction lift, always through `tools/winrun.sh`.

### Windows targeted green checks

```text
tools/winrun.sh build/win/rel/tests/ipc_win_test.exe
exit=0
ipc_win binary stdin/CRT mode restoration: ok
ipc_win no server: IPC_IO ok
ipc_win second server same/second process: IPC_EXISTS ok
ipc_win --open open/line/column/stdin: ok
ipc_win --wait open/line/column/stdin: ok
ipc_win --immediate open/line/column/stdin: ok
ipc_win --reject open/line/column/stdin: ok
ipc_win ACK deadline without drain/cancel cleanup: IPC_TIMEOUT ok
ipc_win endpoint reuse after close: ok
ipc_win launcher adopt/close: ok
ipc_win launcher server death: ok
ipc_win_test: ok

tools/winrun.sh build/win/san/tests/ipc_transport_test.exe
exit=0
ipc_win setup allocation failures return IPC_IO/clean up: ok
ipc_win byte-fragmented request/malformed header: ok
ipc_win incomplete request deadline/cancel: ok
ipc_win wait disconnect/token retirement: ok
ipc_win bounded receive pool admission: IPC_BUSY ok
ipc_win bounded drain/resignal: ok
ipc_win crashed server leaves no named-object residue: ok
ipc_transport_test: ok

tools/winrun.sh build/win/rel/tests/savectl_test.exe
exit=0
savectl Windows native absolute path/lifecycle/content identity: ok
savectl Windows check/keep native identity: ok
savectl Windows invalid path/cache: ok
savectl_test: ok

tools/winrun.sh build/win/rel/tests/savectl_reload_test.exe
exit=0
savectl Windows NTFS clone fallback/copy/map/install/clamp: ok
savectl Windows empty reload: ok
savectl_reload_test: ok
```

The wait case verifies the client remains alive beyond its ACK timeout until
report_closed. Windows scratch files are created and removed by the Windows
process under its native temporary directory. Linux writes nothing to mounted
Windows drives. The kill/restart case removes its native marker afterward.
Logs: `win-ipc-green-rel.log`, `win-oom-green.log`,
`win-savectl-green-rel.log`, `win-savectl-reload-green-rel.log` under
`build/worker-logs/`.

### Windows aggregate

```text
make TARGET=win -j64 win-compile
exit=0
make: Nothing to be done for 'win-compile'.
make TARGET=win -j64 check
exit=0
Windows rel: 33/33 executables passed (RESULT exit=0)
Windows san: 33/33 executables passed (RESULT exit=0)
ASan fired: ok
Windows make check exit=0
```

(M) Counts derive from the final log's RESULT lines, separately for rel/san;
each variant has the same executable set. All new IPC and controller suites
returned exit=0 in both variants. Existing case-level dependency/privilege SKIPs
remain explicit; executable pass counts do not claim those cases ran.
The intentional sanitizer smoke failure produced its required ASan report.
Logs: `build/worker-logs/windows-check.log`, `windows-check.exit`,
`final-summary.txt`, and `win-green-compile.log`. A prior full run also passed;
the final clean run rebuilt/reran after the allocation regression fix.

Final verification uptime (M) [AC, desktop Spiral]:

```text
12:47:04 up  5:02,  1 user,  load average: 0.36, 1.73, 3.75
```

### Tracking benchmarks

```text
build/bench/ipc_bench
exit=0
ipc_bench memory TRACK (E): rx_pool=3145728 arena=3154088 bytes, no limit; as planned
BENCH name=ipc_handoff n=200 p50=465682 p99=714210 ci95=[458994,473452] gate_p50=0 gate_p99=0 pass=1 power=[unknown]
```

(M) [AC, desktop Spiral], load1=7.35. Units are ns from the existing harness;
200 is the benchmark's fixed population. This is descriptive, not a new gate.
The display guard redirected the inherited real display value to :99; no real
display was used. Full output: `build/worker-logs/ipc-bench.log`.

```text
build/bench/savectl_bench --track --guard --row 4KiB
exit=0
POPULATION bytes=4096 n=10000 workers=1 wall_ns=1045464643544
savectl_snapshot_enqueue_4KiB_regression: p50=23550 p99=82223 ns, TRACK
savectl_4KiB_durable_regression: p50=57583513 p99=126739264 ns, TRACK
journal_saved_4KiB: p50=100991530 p99=219340475 ns, TRACK
G8s saving-frame-submitted endpoint UNAVAILABLE: snapshot/enqueue row is a regression guard only
```

(M) [AC, desktop Spiral], load1=7.35 at the benchmark's stamp. Workload and
population derive from the existing `--guard --row 4KiB` selection and
`BENCH_INTERACTION_MIN_N`; times are the harness's measured nanoseconds. This is
a small-file regression workload, not the full G8 save workloads or a
frame-submission claim. Full output: `build/worker-logs/savectl-bench.log`.

## Found, not fixed / coordinator integration

- W2.4: `src/editor/open.c` registers `ipc_server_fd` with epoll;
  `src/main.c` closes `server.lock_fd` after fork. Use the new Windows wake and
  replace the main/launcher descriptor choreography in that bead. Native
  Windows editor/main flow is explicitly SKIP here; channel primitives pass.
- This base worktree has neither production file nor journal in `WIN_MODULES`.
  Windows controller fixtures use real savectl/OS/work/piece and a limited
  identity adapter, with unreachable source/save/journal calls failing the test.
  Save/journal and Linux syscall-interposition race suites explicitly SKIP.
  The coordinator must merge W2.1/W2.2 and run production controller save/journal
  integration; retain every parallel `mk/win.mk` addition. Align savectl's
  `os_stat` → `file_id` helper with W2.1's shared converter/final identity shape:
  this checkout's public file ID has only the low index, while os.h carries the
  full Windows identity. The make switch for real controller dependencies is
  already present, but cannot be exercised without those implementations.
- Existing Windows core and OS suite dependency/privilege SKIPs remain visible;
  they are not expanded or silently turned into passing assertions here.

No product choice is deferred for the transport: SID identity, restricted ACL,
one service thread/IOCP, wake delivery, and named-object lifetime are documented
in the decision file. The remaining coordinator decision is acceptance/landing
of the declared dependency SKIPs versus requiring the merged production
file/journal integration first.
