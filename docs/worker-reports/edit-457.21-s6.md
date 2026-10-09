# edit-457.21 report (Sonnet worker) — PARTIAL, budget reached

Files: src/ipc/{ipc.h,socket.c,wire.c,args.c,STATUS.md}, tests/ipc_test.c, docs/decisions/P4.9c.md (short).
Fixed with red->green test each: §1, §2, §3, §4, §10, §11, §12, §13, §14, §15, §16. Test selector: `ipc_test <name>`.
Red (before fix, release): wire_alias "FAIL g.canary[i]==0xA5"; parser_paths "leaf/../victim rc=0 path=.../victim";
runtime_swap "FAIL lstat(moved_sock)..."; drain_budget "drain ran 24 callbacks in one call (budget 8)";
slot_exhaust "deadline timer never woke the loop"; lock_timeout Alarm clock (exit 142, init hangs);
foreign_cred "client foreign-server rc=6"; memory "ipc arena is 33561088 bytes"; wait_disconnect "token 1 still live after disconnect";
launcher "FAIL ipc_launcher_pair==IPC_OK" (stubs). wire_table (§17 decoder half) was already green: decoder was strict.
Green: `./build/tests/ipc_test` full suite ok in 4.0 s (release); ASan/UBSan `ASAN_OPTIONS=detect_leaks=1 ./build/san/tests/ipc_test` ok; `make all` exit 0.
NOT DONE: fuzz/ipc_fuzz.c exact-result oracle, bench/ipc_bench.c memory assertion, `make check`, `make fuzz`, 120 s fuzz,
--parallel loop (full suite is now ~4 s so 20 rounds ~80 s; watchdog alarm(120) per run is fine), full P4.9c.md, update of P4.9.md text
(its "no timeout / briefly blocks / 33 MB" statements are now stale).
Contract (additions only, listed in P4.9c.md): new constants, ipc_server fields, ipc_server_token_live, ipc_server_init_isolated,
ipc_launcher_pair/adopt_wait/launcher_wait. Behaviour: init can return IPC_TIMEOUT; BUSY reply under pool exhaustion; socket_path is a leaf name.
Integrator (457.16) must: sweep wait tokens with token_live when wait_drops rises; for --wait on primary/isolated use pair+fork+adopt_wait.
Open: /run/user/<uid> absent and abstract squatted -> IPC_EXISTS (no /tmp fallback); Makefile unchanged. No bench run (no stamps).
