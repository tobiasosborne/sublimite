# v2-0fv.14 · base, trace, work and ipc on the one ASSERT

Applies A15 (docs/decisions/wave0-plan-amendments.md) to the four modules that
predate it: one `ASSERT` (src/os/os.h), always compiled in, for conditions only a
caller bug can produce; everything an input, file content, resource exhaustion or
timing can produce stays a return code. `BASE_ASSERT` is gone, no module source uses
`<assert.h>`, and the `__assert_fail *` grant is replaced by a `vendor_ts` grant.
v2-0fv.2 applied the same rule to the fault service.

## The test for each error return

A cause becomes an ASSERT on the precondition when every value that produces it comes
from the caller's own code: a NULL pointer, an alignment, a configuration constant, a
handle the module never issued, a request the module's own producers cannot build. A
cause stays a return code when there is a concrete input that produces it, named
below. The deleted return-code tests are the ones whose cause became an ASSERT; each
module gains one death test (tests/assert_death.h).

## base

Remaining:

| Return | Produced by |
|---|---|
| `base_arena_init` -1 | reservation failure, or a size whose page rounding overflows (sizes derive from input, e.g. trace_stats' `n`) |
| `base_arena_alloc` NULL | exhaustion of the arena |
| `base_pool_init` -1 | an unrepresentable `stride * capacity`, or reservation failure |
| `base_pool_get` NULL | the pool is full |

Now preconditions: `base_arena_init` size 0; `base_arena_alloc` on an uninitialized
arena or with a non-power-of-two alignment; `base_arena_reset_to_mark` with a mark ahead
of the cursor (a stale mark: silently ignored before); `base_pool_init` with zero block
size, zero capacity, or an alignment that is not a power of two up to the page size;
`base_pool_put` of a foreign, interior or unissued address. `base_pool_put` now returns
`void`: its `false` had no input that produces it. `BASE_LIKELY`/`BASE_UNLIKELY` and
base.h's `<stdio.h>`/`<stdlib.h>` existed only for `BASE_ASSERT` and are deleted; the
suites include what they use. The fuzzer drops its foreign and interior put operations
and rewinds only to a mark at or behind the cursor.

## trace

Remaining:

| Return | Produced by |
|---|---|
| `trace_thread_register` -1 | more than `TRACE_MAX_THREADS` live registered threads |
| `trace_dump` -1 | a failed or zero-length file write |
| `trace_fmt_load_dump` -1 | file content, a read error, or reservation failure |
| `trace_fmt_latencies` -1 | more complete frames than `cap` in the records, or reservation failure |
| `trace_fmt_pct` 0 for `n == 0` | an empty sample set |
| `trace_replay` -1 | a record timestamp whose scaled deadline is unrepresentable |

Now preconditions: `trace_thread_release` on an unregistered thread (a no-op before; work
releases only after a successful register); `trace_input_key` with NULL `utf8` and a
nonzero length (silently zeroed before); `trace_fmt_pct` outside 1..100;
`trace_replay` with NULL `opts`/`sink`, NULL `recs` for `n > 0`, or a speed that is not
positive and finite.

## work

Remaining:

| Return | Produced by |
|---|---|
| `work_pool_init*` -1 | a wake, mutex, condition or thread the OS refuses |
| `work_submit` `{0,0}`, `work_submit_batch` -1 | a full class queue, slot or identity exhaustion, or a pool left inactive by a failed init |
| `work_prioritize` -1 | an inactive pool, the refused handle `{0,0}`, or a stale identity (the lease was cancelled or its slot reused) |
| `work_mailbox_bind` -1 | the refused handle, an inactive pool, or a stale identity or generation |
| `work_publish` false | the job was cancelled, or the mailbox is full |
| `work_handle_finished` true, `work_cancel` no-op | the refused handle, or an inactive pool |
| drain/receive 0 | a nested call from a callback (documented reentrancy), or a stale handle |

Now preconditions: `n_bulk != 1` or `n_raster > WORK_MAX_RASTER`; a worker index of
`WORK_MAX_MAILBOXES` or more; a handle with slot `>= WORK_MAX_JOBS` or epoch `UINT32_MAX`
(submit never issues that epoch, so the handle is forged); a batch with more than
`WORK_MAX_JOBS` jobs, NULL arrays, a NULL `fn`, a class value outside the enum, or a
class the pool was not configured for (raster without raster workers, foreground on a
pool without the foreground lane); `work_prioritize` on a pool without the foreground
lane, or on a live raster lease; NULL callbacks. The caller chose the configuration and
the class, so none of these has an input. Two pieces of code go with no ASSERT in their
place: `work_submit`'s remapping of unknown class values to `WORK_RASTER`, and the
`p->shutting_down` disjunct in the UI-side checks, which is true only inside
`work_pool_shutdown` on the UI thread (afterwards `active` is false), where no UI entry
runs.

## ipc

Remaining (each `IPC_INVALID` left in the tree):

| Return | Produced by |
|---|---|
| `ipc_parse_args` `IPC_INVALID` | argc 0 (an exec with an empty argv on Linux before 5.18), an unknown option, a missing option value, a second `-`, a zero or overflowing coordinate, an empty path, a non-directory prefix, a trailing slash on a non-directory |
| `ipc_parse_args` `IPC_LIMIT`, `IPC_IO` | too many paths, an overlong argument or path, arena exhaustion; a failed cwd, stat or canonicalization |
| `ipc_args_read_stdin` `IPC_LIMIT`, `IPC_IO` | stdin larger than the wire allows, a read error |
| `ipc_args_set_activation` `IPC_IO` | arena exhaustion from repeated font options |
| `ipc_wire_decode` `IPC_PROTOCOL` | any malformed frame from a peer |
| `ipc_wire_encode` `IPC_LIMIT` | a request larger than `IPC_MAX_WIRE` or than the caller's capacity (the caller cannot size it in advance) |
| server/client init `IPC_INVALID` | a runtime directory that is relative, missing, foreign-owned, group/other-accessible or not a directory; a lock leaf or socket of the wrong kind or owner; an unreadable `XDG_RUNTIME_DIR`; an empty `EDIT_IPC_NAMESPACE` or `DISPLAY` |
| init/send `IPC_LIMIT`, `IPC_EXISTS`, `IPC_IO`, `IPC_TIMEOUT`, `IPC_REJECTED`, `IPC_BUSY`, `IPC_PROTOCOL` | environment lengths, a live incumbent, kernel failures, a slow, foreign or older peer |
| `ipc_server_report_closed` `IPC_INVALID` | a token whose wait client already disconnected or was answered |
| `ipc_server_adopt_wait` `IPC_LIMIT`, `IPC_IO` | a full peer table; 2^64 issued tokens (P); a refused adoption |

`ipc_server_adopt_wait`'s token exhaustion was `IPC_INVALID` and is now `IPC_LIMIT`,
like the full table beside it. It is the same class as A15's generation exhaustion: one
comparison, a return code in every build.

Now preconditions: NULL `out`, `argv`, `args`, server, `fds`, token pointer or callback;
a NULL `argv[i]` for `i < argc` (the kernel never passes one); `ipc_args_read_stdin`
without `-` or twice; an uninitialized server; token 0 (tokens start at 1); a negative
descriptor; `timeout_ms < -1`; NULL `wire` with a nonzero size; an `ipc_result` outside
the enum in `ipc_result_string` (received codes are range-checked and callback codes
clamped before they become one). Every former encoder `IPC_INVALID` is a precondition:
a request comes from `ipc_parse_args` or `ipc_wire_decode`, which produce only absolute
paths below `IPC_PATH_CAP`, coordinates of at least 1, stdin only with the flag, and
activation strings `ipc_args_set_activation` already filtered. Input aliasing the output
buffer is a caller bug too. The encoder now returns only `IPC_OK` or `IPC_LIMIT`.

## Tests and fuzzers

- tests/assert_death.h forks, runs the violation with stderr on a pipe, and requires
  death by SIGABRT and exactly `<file>:<line>: ASSERT failed: <expr>\n`. Each suite runs
  its case before any thread exists: `base_pool_put` of a foreign pointer,
  `trace_fmt_pct(…, 0)`, `work_pool_init(…, 2, 0)`, `ipc_wire_encode` of a relative
  cwd. The san and tsan lanes run them. `make all` builds the rel binaries, but no lane
  runs them (their lanes are san and tsan), so the rel runs below were by hand.
- `assert`, `BASE_ASSERT`, the work fuzzer's `__builtin_trap` `REQUIRE` and ipc_test's
  exiting `CHECK` became `ASSERT`. work_test's `CHECK` counts failures and continues, so
  it is a report, not an assert, and stays. bench/ipc_bench.c's exiting `REQUIRE` is
  untouched. As in the os suites, test oracles keep the call inside the `ASSERT`. The
  module sources have no side effects in an `ASSERT` argument.

## Symbols

`abort` is granted to `os` (the ASSERT) and `vendor_ts` only, as before; `nm -u` shows
it imported only by `libcore.a:os_posix.o` and tree-sitter's `lib.o` and `scanner.o`.
`__assert_fail` was granted to any module. Only tree-sitter imports it, so the grant is
now `vendor_ts`. A probe `assert()` in src/trace/trace_stats.c then fails check-symbols
with `[restricted] trace imports __assert_fail, granted only to vendor_ts`.
