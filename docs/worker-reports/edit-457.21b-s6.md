# edit-457.21b report (Sonnet worker) - COMPLETE

Files changed: fuzz/ipc_fuzz.c (rewritten), bench/ipc_bench.c, tests/ipc_test.c, docs/decisions/P4.9c.md (full),
docs/decisions/P4.9.md (stale statements fixed), src/ipc/STATUS.md. src/ipc/*.c/h: no change (temporary red edits were all reverted; verified by diff).
No bug found in src/ipc by the new oracle.

## 1. Fuzz oracle (fuzz/ipc_fuzz.c)
Model request from input -> encode -> one of 17 structural mutations (truncate, trailing bytes, magic, version, flag bits, reserved,
total/count/cwd/input/path-length fields, line/col zero or changed, missing '/', replaced NUL terminator, interior NUL, wait/stdin
flag toggles, payload byte). Outcome must be IPC_PROTOCOL (zeroed output) or an exactly equal request, else EDIT_ASSERT traps.
Encoder contract checks (LIMIT, INVALID, alias, determinism, new_instance unserialized). 1/32 of inputs go end to end through a real
abstract-namespace server (ACK code == model, callback sees the exact request, foreign-uid peer via EDIT_IPC_TEST_PEER_UID never
reaches the callback nor gets an ACK).
Red (regression injected in src/ipc, fuzzer rebuilt, then reverted):
- reserved bytes unchecked   -> fuzz/ipc_fuzz.c:71: assertion failed: rc==IPC_PROTOCOL
- encoder alias check gone   -> fuzz/ipc_fuzz.c:210: assertion failed: ipc_wire_encode(&b,(uint8_t *)inbuf,...)==IPC_INVALID
- SO_PEERCRED uid compare gone -> fuzz/ipc_fuzz.c:116: assertion failed: s.calls==0 && got!=8 && got!=-2
Green: `ASAN_OPTIONS=detect_leaks=1 ./build/fuzz/ipc_fuzz -max_total_time=120 -max_len=4096 <fresh corpus>`:
"Done 1508898 runs in 121 second(s)", 0 assertions/sanitizer findings (M)[AC] (BAT0 "Not charging"), load1=27.97 at start, 21.46 at end.
(An initial crash was my own oracle bug: reply magic is "EDIR", not "EDIP"; fixed.)

## 2. Bench (bench/ipc_bench.c)
Asserts pool == 3 MiB (16x64 KiB + 2x1 MiB), arena in [pool, pool+256 KiB], arena < 4 MiB; prints the row; any miss fails the exit status.
Existing hand-off p99 <= 10 ms (G) row retained.
Red (IPC_RX_BIG_COUNT temporarily 32, reverted): "ipc_bench: failed server.arena.size<4u*1024u*1024u" and
"rx_pool=34603008 arena=34611608 ... MISS".
Green, one run [AC] (Not charging), load1=17.85 (TRACK only, shared box):
ipc_bench memory (E/G): rx_pool=3145728 arena=3154088 bytes, gate arena<=3407872; ok
BENCH name=ipc_handoff n=200 p50=292686 p99=757640 ci95=[273977,317611] gate_p99=10000000 pass=1 power=[AC]   (M)
Coordinator: gate verdict on a quiet box.

## 3. ipc_test --parallel [N [ROUNDS]]
Default 4 x 1, N in 2..16. Each child runs the full suite with its own mkdtemp-derived EDIT_IPC_NAMESPACE, reports it over a pipe,
parent checks pairwise distinct and counts failures. `make check` still runs only the serial suite (ipc_test ASan ok in the check run).
Red (namespace forced to a constant "shared-ns", reverted):
  FAIL tests/ipc_test.c:501: ipc_server_init(&s,NULL)==IPC_OK
  FAIL tests/ipc_test.c:653: strcmp(ns[i],ns[j])!=0
Green: release `--parallel 4 1`: 4 runs, 0 failures, 4.3 s; `--parallel 8 3`: 24 runs, 0 failures;
ASan/UBSan leaks on `--parallel 4 2`: 8 runs, 0 failures, 8.6 s.

## 4. Docs
P4.9c.md: contract changes + one section per finding 1-4, 10-17 (verdict, fix, red/green from the 21 report). P4.9.md: lifecycle lock
timeout (1 s, IPC_TIMEOUT), 3 MiB pool/IPC_BUSY, bounded drain, request deadline/eviction, timerfd, token-live sweep, launcher pair for --wait.
STATUS.md: bounds, done/missing, integrator duties, verify commands (leaks=1, 120 s fuzz, --parallel).

## 5. Full runs (DISPLAY=:99 EDIT_DISPLAY=:99, nothing on :0)
- `make all` (gcc release): exit 0.
- `ASAN_OPTIONS=detect_leaks=1 make check` (clang ASan/UBSan): "check: 42 test binaries passed", replay CLI "all passed", exit 0 [AC] load1~5-20.
- `make fuzz`: "fuzz: 22 fuzzers built", exit 0.

## Open problems / notes for the coordinator
- /run/user/<uid> absent AND abstract name squatted -> IPC_EXISTS (no /tmp fallback); unchanged from the 21 report.
- Fuzz e2e coverage uses an abstract server (no files); the filesystem-endpoint credential path is covered only by ipc_test foreign_cred.
- Bench numbers were taken at load1 17-28 and are TRACK only.
- Integrator (457.16) duties: token_live sweep on wait_drops; pair+fork+adopt_wait for --wait on primary/isolated.
