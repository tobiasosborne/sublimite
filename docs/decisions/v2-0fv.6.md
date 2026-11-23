# v2-0fv.6 · L0 ipc harvest (args/wire/server + socket namespace)

Source pin `6fec2e8` (read-only oracle at
`/home/tobias/Projects/sublimite-v1-oracle`). Dossier:
`docs/decisions/wave1-L0-harvest.md` §0, §5, §7 L0-6. DAG: `base < ipc`
(amendment A1); ipc uses os and base, never trace or work.
`wire.c` and `result.c` are byte-identical to the pin (verified with
`diff`); the other files differ only as listed below.

## Decisions

- **Socket names from the single build identity.** Abstract name,
  filesystem leaf and lock leaf are built from `EDIT_APP_ID`
  (`sublimite-v2`), never spelled: formats are
  `EDIT_APP_ID "-%lu%s.sock"` (leaf), `EDIT_APP_ID "-%lu%s"` and
  `EDIT_APP_ID "-%lu-%s%s"` (abstract, with the `EDIT_IPC_NAMESPACE`
  hook; *superseded by v2-0fv.20:* the hook is gone, tests append their
  namespace through a link seam). The leaf keeps v1's shape — 4-char extension — so
  `start_endpoint`'s lock derivation (`memcpy(lockleaf+ln-4,"lock",4)`)
  stays valid and yields `<app-id>-<uid><suffix>.lock`.
  Length derivation (Law 4): leaf buffer is `endpoint.leaf[64]`, so a
  name fits iff `strlen(EDIT_APP_ID)` (12 (P), the build literal) + 1
  (`-`) + uid digits + display suffix + 5 (`.sock`) + 1 (NUL) <= 64.
  `display_suffix` caps its own output at 62 chars (input at most
  `(64-3)/2 = 30` chars, hex-doubled plus `-d`), so long displays hit
  the existing `snprintf` guard and return `IPC_LIMIT` — the same
  contract as v1, at 3 more chars of app id. No buffer was enlarged.
  The abstract name is bounded by `sun_path` (108 (P)) the same way.
  Oversized namespace/display cases from v1 pass at the new lengths.
- **Activation tokens kept** (v1 `bafe540`, explicit acceptance line):
  `ipc_args_set_activation`, the wire trailing block (flags bit 2,
  size in header bytes 6–7) and the client's plain resend on
  `IPC_PROTOCOL`. A token-less request is byte-identical to the
  pre-activation format: `t_wire_table`'s expected-bytes fixture
  (harvested) proves it, and `t_activation` proves round-trip,
  malformed-block rejection and the resend against a modelled
  previous-release server. The L5 caller that captures
  `XDG_ACTIVATION_TOKEN`/`DESKTOP_STARTUP_ID` at startup (v1
  `src/main.c`, 4 lines) belongs to the L5 bead; the API is complete.
- **`edit_*` → `base_*`** for the two arenas (`ipc_args`, `ipc_server`)
  and every call site, following v2-0fv.3. No aliases.
- **FSM-rule reading** (dossier §5.2). Peer progress is a
  single-client request pipeline over plain fields, not coordinating
  modal booleans across the server lifecycle, so it is harvested
  as-is rather than rewritten as an enum FSM. The field invariants
  are stated below and are what `process_peer` enforces.
- **Kernel boundary, reported exception.** Everything os offers is
  used through os: `os_cwd`, `os_path_canonical`, `os_stat_path`,
  `os_path_is_absolute`, `os_file_read` (stdin), `os_now_ns`
  (deadlines), `os_sleep_ns` (lock/retry waits), `os_env_get`
  (`XDG_RUNTIME_DIR`; `EDIT_IPC_TEST_PEER_UID` was removed by
  v2-0fv.13, `EDIT_IPC_NAMESPACE` and `EDIT_IPC_FALLBACK_DIR` by v2-0fv.20). os
  offered no socket, epoll, timerfd,
  peer-credential or uid API, so those imports are granted to `ipc`
  in `tools/system-symbols.txt`, one line with a reason each — the
  mechanism `docs/decisions/v2-he7.22.md` defines ("a worker landing
  a new module appends its own block"), with the missing os surface
  reported below for a coordinator decision. Three pure imports
  joined the `*` block: `__memcpy_chk`, `strnlen`, `strrchr`.
  `nm -g -P` over the four release objects confirms the grant set is
  exact: `args`/`wire`/`result` import only os, base and `*`
  symbols; only `ipc_posix.o` imports the ipc block
  (`build/ipc-evidence/nm-ipc.log`).
- **The one seam bypass in v1 `args.c` is closed** (dossier §5.2):
  the POSIX `canonical` used raw `realpath`+`stat` and the existence
  probe used raw `lstat`. Both now use the form v1's Windows leg
  already had (`os_path_canonical`, `os_stat_path` with
  `OS_STAT_NOFOLLOW`), with the same result mapping
  (ENOENT→absent, NOTDIR/INVAL→`IPC_INVALID`, else `IPC_IO`).
  `t_parser_paths` and `parser` cover the symlink/`..`/trailing-slash
  semantics and pass unchanged.
- **Env through `os_env_get` preserves outcomes.** Unset
  (`OS_E_NOENT`) still selects the default; an overlong
  `XDG_RUNTIME_DIR`/`EDIT_IPC_FALLBACK_DIR` gives `IPC_INVALID`
  (v1: the same, via failing `open`); an overlong namespace gives
  `IPC_LIMIT` (v1: the same, via the `snprintf` guard).
  *Superseded by v2-0fv.13:* the `EDIT_IPC_TEST_PEER_UID` hook is gone
  from shipped code. Tests reach the peer-identity decision through a
  link-time wrap of `os_stream_peer_uid` (`tests/ipc_peer_seam.h`, now
  `tests/ipc_seam.h`). *Superseded by v2-0fv.20:* the namespace and
  fallback hooks went the same way; the overlong-namespace outcome is gone.
- **Test fixture lives under `build/`.** `tests/ipc_isolation.h`
  keeps v1's structure but mkdtemps `build/ipc-test-XXXXXX` under the
  absolute cwd (never `/tmp`, never real XDG) and derives the
  namespace from the basename instead of v1's magic `+5` offset.
  Consequence: absolute fixture paths in this deep checkout exceed
  `sun_path` (110 > 108 chars (M) at this worktree), so the test's
  raw bind/connect helpers address sockets through
  `/proc/self/fd/<dirfd>/<leaf>` — the same mechanism production
  uses — instead of spelling the full path into `sockaddr_un`.
  `rm_rf` in the test no longer execs `rm(1)`; it reuses the
  fixture's recursive remove. `main` line-buffers stdout so forked
  clients cannot duplicate buffered parent lines in the log
  (observed 80 duplicates under TSan before, 1 after).
- **Bench is harness-free.** v2 has no `bench/harness.h`; like
  `base_bench`, `ipc_bench` is self-contained (os_now_ns, qsort,
  nearest-rank percentiles). The receive-pool assertion-of-record
  from v1 (3 MiB pool, "the 33.5 MB arena stays gone") is kept as a
  printed TRACK note that never fails the bench (v1 edit-lwq).

## State and invariants

No file-scope mutable state exists in the module (verified by
reading every translation unit); all state is caller-owned.

| State | Invariant |
|---|---|
| `ipc_args.arena` | Owns every string the request points at (cwd, paths, stdin, fonts, activation). Zero-init before parse; a failing parse frees it and zeroes the struct. |
| `ipc_request.count`, `paths[]` | `count <= IPC_MAX_PATHS`; entries `[0,count)` are absolute, NUL-terminated, with 1-based line/col. The decoder enforces the same on borrowed wire storage and zeroes its output on any failure. |
| `stdin_data`/`stdin_size` | `stdin_size != 0` only with `has_stdin`; size is bounded by the wire space left after header, cwd and paths (computed identically in parse, read-stdin and set-activation). |
| `activation_token`/`startup_id` | NULL or printable (`0x21..0x7e`), at most `IPC_ACTIVATION_CAP` with NUL. Present on the wire iff flags bit 2 is set and header bytes 6–7 are nonzero, equal, and `4 + token_n + startup_n`. |
| `new_instance` | Launch-local only; the encoder never serializes it and the decoder always yields false. |
| `font_family`/`font_file`/`font_fallbacks` | Launch-local, arena-owned, last flag wins, never serialized. |
| `ipc_server.fd` | The epoll fd after successful init; -1 in a reset server. All server calls belong to the one loop thread. |
| `ipc_server.listener` | The bound listening socket, or -1 for an isolated (--new-instance) server, which never binds. |
| `ipc_server.dir_fd` | The pinned, verified runtime directory (O_PATH) for filesystem endpoints; -1 for abstract. Every later lock/bind/chmod/unlink goes through it, so path replacement cannot redirect them. |
| `ipc_server.socket_path`, `socket_device`, `socket_inode`, `owns_path` | `owns_path` is true only after bind verified the leaf's device+inode; teardown unlinks only if `fstatat` still matches both, so a replaced name is never removed. Empty for abstract. |
| `ipc_server.lock_fd` | The lifecycle lock leaf, held unlocked between init and fini; both take it with a bounded deadline (`IPC_LOCK_TIMEOUT_MS`) and fini leaves the socket for stale-recovery rather than unlink blind. |
| `ipc_server.next_token`, peer `token` | Tokens are issued strictly increasingly from 1, never reused within a server's life, at most `IPC_MAX_CLIENTS` live. Token 0 is the listener's epoll tag, `UINT64_MAX` the timer's; neither is a peer token. |
| Peer `expected`/`received` | `expected == 0` until the 24-byte header is complete and validated (magic, `24 <= expected <= IPC_MAX_WIRE`); `received <= (expected ? expected : 24)`. |
| Peer `wire`/`wire_big` | A receive-pool slab held only between header validation and the open callback's return; released before the ACK is queued. NULL outside that window. Pool counts (`nsmall`, `nbig`) plus held slabs always sum to `IPC_RX_SMALL_COUNT`/`IPC_RX_BIG_COUNT`. |
| Peer `opened` | False while the request is incomplete (deadline and idle-eviction apply); set exactly once, when decode+callback have run or a reject reply is queued. Adopted launcher channels are born opened. |
| Peer `reply`/`reply_size`/`sent` | `sent <= reply_size <= 16`; bytes `[0,8)` are the ACK frame, `[8,16)` the closed frame, appended only when a waited request closes cleanly. A peer is dropped once its final reply is fully sent, unless it waits unclosed. |
| Peer `wait`/`closed` | `wait` mirrors the decoded request (true for adopted channels). `closed` is set only by `ipc_server_report_closed` on an opened, waited peer. `peer_waiting` (wait && opened && !closed && no non-OK ACK) is exactly `ipc_server_token_live`. |
| Peer `accepted_ms`/`active_ms` | Monotonic-ms stamps from `os_now_ns`; `accepted_ms` fixed at accept/adopt, `active_ms` refreshed on every received byte. The timerfd is armed iff an incomplete peer exists, at the earliest `accepted_ms + request_deadline_ms` (`rx.timer_ms`, 0 = disarmed). |
| `ipc_server.wait_drops` | Counts waited peers that disconnected before their closed reply; the loop must sweep token associations after any drain that raised it (header contract). |
| `ipc_server.drain_bytes` | Bytes received by the most recent drain call; bounded per call by `IPC_DRAIN_BYTES` together with the callback and accept budgets. |
| `ipc_server.arena` | Backs the peer table and the receive pool for the server's whole life; its size is the pool arithmetic in `setup_runtime`, asserted by `t_memory` (<= 4 MiB) and tracked by the bench. |
| Endpoint `abstract` | Chosen once in `resolve`: filesystem iff a runtime dir is given or `XDG_RUNTIME_DIR` is set. Peer-credential policy (test-uid hook, fallback) keys off this and nothing else. |

## v1 material left out (one line each)

### `src/ipc/ipc_win.c` (719 lines) and `src/ipc/crt_win.h` (35) — dropped whole
Windows named-pipe endpoint, SID ACL, self-spawn handoff and CRT-fd helpers; Law 5 keeps no Windows builds in MVP.

### `src/ipc/ipc.h`
- `ipc_server.win` member — Windows-private state with no Linux meaning.
- `ipc_server_wake`, `ipc_launcher_prepare`, `ipc_server_connect_wait` — Win32 loop and self-spawn declarations.
- Windows comment paragraphs — replaced by one pointer to v1 `ipc_win.c` at the pin.

### `src/ipc/args.c`
- `separators`, `root_size` and the Windows `canonical` — drive/UNC path semantics for a dropped platform (its os-based call shape is what the POSIX body now uses).
- CRT binary-mode stdin dance (`ipc_crt_mode`/`ipc_crt_read`) — POSIX fds need no translation.
- Raw `realpath`/`stat`/`lstat`/`read` and their headers — replaced by the os calls above.
- The `edit-6e2`/`edit-457.21` provenance tags in comments are kept where they explain a rule; nothing else was reworded.

### `src/ipc/ipc_posix.c`
- The whole-file `#if !defined(_WIN32)` guard — this is the only body now.
- Raw `clock_gettime`/`nanosleep`/`getenv` — via `os_now_ns`/`os_sleep_ns`/`os_env_get`.
- The three v1 identity literals — built from `EDIT_APP_ID` (even the correct v2 spelling as a literal would trip the isolation checker).

### `src/ipc/STATUS.md` (135 lines) — read, not carried
v1 bead log (`edit-jmn`, `edit-457.9/.21` bounds); this file is v2's record. The numeric bounds it justifies ride in `ipc.h` comments, unchanged.

### `tests/ipc_test.c`, `tests/ipc_isolation.h`, `fuzz/ipc_fuzz.c`, `bench/ipc_bench.c`
- The `#include "../src/main.c"` harness (`cli_state`, `cli_null_backend`, `cli_script_run`, `editor/private.h`) — forbidden in v2 tests; its cases move to L5 (below).
- `bench/harness.h` (403 lines) — not imported for one bench; `ipc_bench` carries the dozen lines it used.
- `/tmp/sublimite-ipc-XXXXXX` temp template and `runtime + 5` namespace derivation — replaced per above.
- `rm-1` exec in `rm_rf`; full-path `sockaddr_un` in test helpers — replaced per above.
- The `--parallel` runner is kept (pure fork+namespace machinery) and passes 2x1.

## Test census (v1 `tests/ipc_test.c`, zero unaccounted)

Kept in v2 `tests/ipc_test.c` against the public header (SAN + TSAN
lanes): `parser`, `wire_tests`, `roundtrip` x3 (plain/wait/stdin),
`t_result_text`, `stale`, `race`, `fragmented`, `wait_tokens`,
`callback_results` x2 (reject/close-in-callback),
`endpoint_validation` (incl. namespace empty/oversized/alternate),
`t_wire_alias`, `t_wire_table` (incl. the golden token-less bytes),
`t_parser_paths`, `t_runtime_swap`, `t_drain_budget`,
`t_slot_exhaust`, `t_lock_timeout`, `t_foreign_cred` (incl.
squatter fallback), `t_display`, `t_memory` (bounded admission),
`t_wait_disconnect`, `t_launcher` (pair/adopt/isolated at the ipc
layer), `t_activation` (whole). Added: `t_namespace` (this bead).
Helpers kept: `opened`, `request`, `raw_connect`, `drain_ready`
(now also the release Law-2 window: in the rel binary the live
guard counts drain allocations and requires 0), `count_only`,
`pump`, `spawn_client`, `serve_one`, `launcher_child`, `subdir`,
`touch_file`, `reap_ok`, `now_ms`.

Handed to the Wave 1 minimal-L5 bead as subprocess tests against the
built binary (never an include): `t_main_launcher` primary+isolated
(launcher fork split, adopt, all-member --wait, UI death, nonwait),
`t_cli_capacity` (edit-jmn CLI tab capacity through main),
`t_astra_many_open` in both modes (200-file CLI handoff; editor
epoll-retirement reload), `t_editor_tabs` (1000 editor tabs),
`t_fd_limit_startup`, `t_fd_per_file`, `t_fd_exhausted` (edit-m25u
descriptor accounting in main/editor), plus their harness
(`cli_state`, `cli_state_send/read`, `cli_command`,
`cli_null_backend`, `cli_script_run`, `text_file`, `open_fds`,
`open_fds_below`).

## Reported, not fixed

- **os has no socket surface. Resolved by v2-0fv.13.** At this
  bead, `socket/bind/listen/accept4/connect/send/recv/getsockopt/
  epoll_*/timerfd_*/socketpair/getuid` and the fd-identity operations
  on the pinned directory were granted to `ipc`. v2-0fv.13 added the
  os local stream endpoint and the `os_waitset` wait primitive,
  moved ipc onto them and deleted all 29 ipc grants. The restricted
  fixture's expected diagnostic for `read` is back to "granted only
  to os". See `docs/decisions/v2-0fv.13.md`.
- **`edit-jmn` existing-file EMFILE** (v1 `src/ipc/STATUS.md`):
  root cause is per-file watcher allocation in v1 `src/file`;
  inherited L2 business, untouched here.
- **L5 activation caller** does not exist yet (see Decisions).

## Red and green

Harvest order: verbatim POSIX bodies first (base rename + Windows
strip only), split test + new `t_namespace`, then the namespace fix
and os routing. Exact commands (HOME under `build/` throughout):

```sh
make -s build/san/tests/ipc_test
build/san/tests/ipc_test namespace
```

Red (verbatim v1 names), exit 1
(`build/ipc-evidence/namespace-red.log`):

```text
namespace: init rc=1 beside v1 decoy leaf
FAIL tests/ipc_test.c:795: rc==IPC_OK
```

`rc=1` is `IPC_EXISTS`: with v1 names the server collides with the
decoy listener at the v1 leaf — the forward-into-v1 hazard in
miniature. The full suite on the same verbatim build failed only
there (it is the last case; `build/ipc-evidence/full-red.log`),
so the harvest itself was sound before the fix.

Green (after `EDIT_APP_ID` + os routing), exit 0
(`build/ipc-evidence/namespace-green.log`, `full-green.log`):

```text
ipc_test: endpoint names carry the v2 app id; v1 decoys untouched
ipc_test: parser, wire, roundtrip, --wait, stdin, stale, race, fragments, tokens, endpoints, review-fix suites ok
check_isolation: ok (21 files under src/)
```

## Acceptance and measurement

All run in this worktree, exit 0 (`build/ipc-evidence/` logs):

```sh
make all
make check        # dag, no-c-includes, symbols (nm grants), isolation,
                  # 6 isolated layer links, ASan+UBSan (9 suites), Law-2
make check-tsan   # 3 threaded suites (os, base, ipc)
make fuzz-smoke   # base + ipc seed replays
build/rel/tests/ipc_test            # release binary: live malloc guard in drain_ready
build/san/tests/ipc_test --parallel 2 1
build/fuzz/ipc_fuzz -max_total_time=25 <copied corpus>   # 173766 runs, clean
```

`ipc_fuzz` covers wire decode (exact-result oracle), encoder
contract, end-to-end server + credential rejection (v1 parts 1–4),
and argv/stdin/activation parsing (new part 5 — v1's fuzzer had no
`ipc_parse_args` surface). Seeds: `fuzz/corpus/ipc/{wire,args,raw}`.

Bench (M) [AC], BAT0 `Charging`, load average 4.99 at run time —
a loaded box, so these are records, not gates (Law 1):

```sh
make build/rel/bench/ipc_bench && build/rel/bench/ipc_bench
```

```text
ipc_bench memory TRACK (E): rx_pool=3145728 arena=3154088 bytes, no limit; as planned
ipc_bench TRACK (M) status=Charging load1=4.99; no limit (no derivation); includes callback, ACK, client exit/reap
BENCH name=ipc_handoff n=200 p50=674800 p99=2102907 unit=ns
```

Source count (M, `wc -l src/ipc/*`):

```text
 150 src/ipc/ipc.h
 194 src/ipc/args.c
 104 src/ipc/wire.c
  17 src/ipc/result.c
 601 src/ipc/ipc_posix.c
1066 total
```
