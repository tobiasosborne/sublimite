# Wave 1 — L0 (`libcore.a`) harvest dossier

> Lane M4 research dossier for Wave 1. v1 source pinned read-only at
> `/home/tobias/Projects/sublimite-v1-oracle` (detached at `6fec2e8`,
> verified with `git -C … rev-parse HEAD`). All citations are FILE + SYMBOL
> at that path; line numbers, where given, are locators at `6fec2e8` only.
> `docs/PLAN_V2.md` (below: PLAN) line numbers were written against older
> v1 `c95b6eb`; §0.3 re-resolves every L0-relevant one to a symbol at
> `6fec2e8`. Line counts are measured with `wc -l` (M) at `6fec2e8` — the
> command is stated where the counts appear. No other number is asserted
> without its source beside it (CLAUDE.md Law 4); code constants are cited
> to the defining symbol, not re-derived here. No time estimates (Law 3/4).
> This dossier edits nothing else and runs no `bd`, no build, no v1 binary.

## 0. Cross-cutting facts every L0 bead inherits

### 0.1 L0 partial order and the one place v1 already violates it

PLAN §2 and the v2 checker agree. `mk/layers.mk` in v2:
`L0_MODS := os base trace work ipc`, `L0_LIB := libcore.a`.
`tools/check_dag.py` `INTRA_RULES` (v2):

```
os < base < trace < work
os < ipc
```

The checker transitively closes these rules and forbids any include
between two same-layer modules that are *not* related by them. Auditing
every quoted `#include` in v1 `src/os`, `src/base`, `src/trace`,
`src/work`, `src/ipc` at `6fec2e8` against that order:

| Edge (includer → included) | v1 site (FILE + SYMBOL context) | v2 verdict |
|---|---|---|
| `base → os` | `src/base/base.c`: file-scope include, used by `edit_arena_init`/`edit_pool_init` (`os_vm_reserve`, `os_page_size`) | OK (`os < base`) |
| `trace → os` | `src/trace/trace.c` (`trace_now_ns` → `os_now_ns`), `src/trace/replay.c` (`trace_replay` → `os_now_ns`/`os_sleep`) | OK (`os < … < trace`) |
| `work → os, base, trace` | `src/work/work.h` (`os_mutex`/`os_cond`/`os_thread`/`os_wake` fields of `work_pool`), `src/work/work.c` (file-scope includes; `worker_main` calls `trace_thread_register`) | OK (all `< work`) |
| `ipc → os` | `src/ipc/ipc.h`, `src/ipc/args.c`, `src/ipc/wire.c` (file-scope includes) | OK (`os < ipc`) |
| **`ipc → base`** | **`src/ipc/ipc.h`: `#include "base/base.h"`** — `ipc_args.arena` and `ipc_server.arena` are `edit_arena`; consumed by `ipc_parse_args`/`ipc_args_read_stdin` (`src/ipc/args.c`), `ipc_client_send_display` and `setup_runtime` (`src/ipc/ipc_posix.c`) | **VIOLATES v2 DAG as written**: `base` and `ipc` are unrelated in `INTRA_RULES`, so `check_dag.py` rejects this edge |

This is the single structural blocker in L0. It is not fixable by the ipc
worker alone, because the two candidate fixes are different decisions:

1. **Amend the order** to `os < base < ipc` (add `"base < ipc"` /
   `"os < base < trace < work"` + `"base < ipc"` in `check_dag.py` and
   PLAN §2). Still acyclic; `ipc` then depends on `base` exactly as v1
   does, and the harvest stays verbatim. Cost: `ipc` can no longer be
   built/tested before `base` lands (bead order in §7 already assumes
   this).
2. **Cut the edge**: replace `edit_arena` in `ipc_args`/`ipc_server`
   with `os_vm_reserve`-backed storage private to `ipc`. This duplicates
   arena logic inside L0 and changes `ipc.h`'s public struct layout —
   a rewrite of a module PLAN §3 classifies as "harvest + targeted fix",
   with no repro behind it (Law 1/8 argue against).

Recommendation: option 1, recorded as a coordinator decision in
`docs/decisions/` *before* the ipc bead opens, with PLAN §2 and
`tools/check_dag.py` edited in the same commit (docs in lockstep, Law 6).
The bead split in §7 is written assuming option 1; if the coordinator
chooses option 2, only the ipc bead's scope grows — no other bead changes.

Two softer order notes, not blockers:

- `trace` never includes `base`, although `base < trace` permits it. Keep
  it that way: `trace`'s only L0 dependency is `os_now_ns`/`os_sleep_ns`
  (`trace_now_ns`, `trace_replay`). In particular `trace_load.c`
  (`trace_fmt_load_dump`) allocates with raw `malloc`/`realloc` (header
  comment: "Off the typing path: uses malloc") and must not be "fixed"
  onto arenas as part of the harvest; it is a tools/test-side loader.
- Same-module private headers exist in v1 and are fine for the DAG
  (the checker keys on module directories): `src/trace/trace_fmt.h`
  (included by `trace.c`, `trace_load.c`, tests, fuzz, tools) and
  `src/ipc/crt_win.h` (Windows-only, dropped, §5). But CLAUDE.md code
  conventions say "one public header" per module. `trace_fmt.h` is a
  genuine second public surface (dump format + `trace_fmt_load_dump` /
  `trace_fmt_dump_free` + header-only gate arithmetic used by
  `tools/tracedump` and G1 tests). The trace bead (§7) keeps it as the
  module's documented format header and records the exception in its
  `docs/decisions/` note; folding it into `trace.h` would put
  `malloc`-ing inline helpers in every translation unit that only wants
  `trace_record`.

### 0.2 Naming: `module_` prefix vs v1's `edit_` prefix in `base`

CLAUDE.md: "`module_` prefix on every exported symbol." At `6fec2e8`:

- `os` exports `os_*` — conforming.
- `trace` exports `trace_*` / `trace_fmt_*` — conforming.
- `work` exports `work_*` — conforming.
- `ipc` exports `ipc_*` — conforming.
- `base` exports `edit_version`, `edit_arena_*`, `edit_pool_*`,
  `edit_cpu_has_*`, `edit_malloc_guard_*`, plus macros `EDIT_ASSERT`,
  `EDIT_LIKELY`, `EDIT_UNLIKELY` (`src/base/base.h`) — **not**
  `base_`-prefixed. Worse, PLAN §2 names the L2 edit kernel `edit`, so
  v2 will have a real `src/edit/` module whose natural prefix collides
  with `base`'s harvested `edit_*` names (no symbol collision today —
  v1's `src/editor/` uses `editor_*` — but a permanent readability trap).
- Header guards are likewise v1-flavoured: `EDIT_OS_H`, `EDIT_BASE_H`,
  `EDITOR_TRACE_TRACE_H`, `EDITOR_WORK_WORK_H`, `EDIT_IPC_H`.

Renaming `edit_arena` → `base_arena` etc. is mechanical but touches
every future caller; Wave 1 is the cheapest moment to do it because no
v2 caller exists yet. The base bead in §7 therefore includes the rename
as its one deliberate non-verbatim change (with no compatibility aliases
— CLAUDE.md "no fossils"), and the ipc/work beads consume the new
names. If the coordinator prefers verbatim names, drop that clause from
the base bead and record a naming waiver in `docs/decisions/` instead;
nothing else in this dossier depends on the choice. Either way the
decision must be explicit before the base bead opens, because PLAN §3
says `work` is "harvest verbatim" and `work` includes `base/base.h`
(it uses `EDIT_ASSERT`-family macros via that header chain — see §4).

### 0.3 PLAN line citations re-resolved to symbols at `6fec2e8`

PLAN was written against v1 `c95b6eb`. Each L0-relevant `file:line`
citation, re-resolved (method: `git show c95b6eb:<file>` at the cited
line to identify the symbol, then located that symbol at `6fec2e8`):

| PLAN citation | PLAN claim | Symbol at `6fec2e8` | Still true? |
|---|---|---|---|
| `os_posix.c:909` (§4.1) | fault-slot reuse ABA site for the pinned lease | `os_fault_register` in `src/os/os_posix.c` (the `SLOT_FREE → SLOT_RESERVED → SLOT_LIVE` claim loop over `os_fault_service.slots[]`); the line number happens to still land on the function's first line | Yes — and there is still **no** pin/gen API; `os_fault_pin`/`os_fault_unpin`/`os_fault_poll` do not exist anywhere in v1 (grep: zero hits). §4.1's lease is new v2 code, not a harvest (§1.5) |
| `work.h:185` (§4.7) | "physical work-lease retirement (`work.h:185`) owns resource cleanup" | `work_handle_finished` declaration + its contract comment in `src/work/work.h` ("UI-only physical lease acknowledgement… Cancellation alone does not finish a running lease") | Yes, unchanged between `c95b6eb` and `6fec2e8` (diffed) |
| `Makefile:156` (§5) | `-Wl,--wrap=...` syscall-injection precedent | The `file_acquire_test` link rule in v1 `Makefile`: `LDLIBS += -Wl,--wrap=pread,--wrap=os_file_open_at` (comment: "File acquisition syscall injection is confined to this regression binary") | Yes. Note it wraps an **L0 symbol** (`os_file_open_at`) — direct precedent for rewriting L0 tests with `--wrap` instead of `#include "*.c"` |
| `tests/ipc_isolation.h:56` (§1, §6 Wave 0) | disposable-profile fixture for oracle runs | `test_ipc_isolate` in `tests/ipc_isolation.h`: `mkdtemp("/tmp/sublimite-ipc-XXXXXX")`, then `setenv("XDG_RUNTIME_DIR", …)` (the cited line) and `setenv("EDIT_IPC_NAMESPACE", isolation->runtime + 5, 1)`, cleanup via `on_exit(test_ipc_isolation_cleanup, …)` owner-pid-checked | Yes. Two harvest notes: (a) the `+5` skips `"/tmp/"` to derive the abstract-namespace string from the temp path — brittle but self-contained; (b) the temp prefix spells `sublimite-ipc-` — it lives in `tests/`, which `tools/check_isolation.py` does not scan (it scans `src/` only), but the v2 copy should still use a v2-flavoured prefix so a leaked temp dir is attributable |
| `x11/clip.c:5` ↔ `wayland/dispatch.c:4` (§2) | the `private.h` cycle the DAG checker exists to prevent | L3 modules, out of L0 scope; cited here only as the checker's motivating example | Not re-verified (out of lane) |

PLAN §2's L0 one-liners also check out against `6fec2e8`, with the
qualifications developed per-module below: "POSIX vm_reserve/commit,
clock, file/dir/mmap, pinned os_fault_service, wake/poll, namespaced
app_dir CONFIG+DATA" (§1), "arenas, slabs, live counting malloc guard,
atomic/pthread_once cpu-detect" (§2 — the cpu-detect half is the *fix*,
v1 is racy, §2.5), "trace (with ring release on pool teardown)" (§3 —
release does not exist in v1), "work (single-producer mailboxes +
physical lease retirement)" (§4 — accurate; per-worker SPSC rings plus a
foreground lane PLAN does not mention), "ipc (POSIX Unix socket with
namespaced leaf + abstract name)" (§5 — the namespace exists but spells
v1's name).

### 0.4 How v1 builds these files (so the harvest knows what is live)

v1 `Makefile` sets `SRCS := $(sort $(wildcard src/*/*.c))` — every
`src/<module>/*.c` compiles into every flavour (rel/san/fuzz objects).
Platform files self-guard: `src/os/os_win.c`, `src/ipc/ipc_win.c`,
`src/base/malloc_guard_win.c` are the Windows bodies, while
`src/ipc/ipc_posix.c` opens with `#if !defined(_WIN32)`,
`src/base/malloc_guard.c` likewise ("Linux only: the Windows guard is
malloc_guard_win.c"), and `src/work/work_posix.c` likewise. On Linux the
`*_win.c` files therefore contribute empty translation units. v2 must
**not** carry the empty-TU pattern: Windows files are dropped outright
(§1.4/§2.4/§5.4), per CLAUDE.md Law 5 ("keep only `os.h`/`plat.h`
portability seams; no Windows builds in MVP") and PLAN §3 "Delete / do
not carry over: Windows `mk/win.mk`/`win32/`/`d3d11/`/`dwrite_win.c`".

v2 landing zone today: `src/os/`, `src/base/`, `src/trace/`, `src/work/`,
`src/ipc/` exist; only `src/os/` has content — the smoke stub
`os_app_id()` in v2 `src/os/os.h`/`src/os/os.c`, which returns the
Makefile-injected `EDIT_APP_ID` (`APP_ID := sublimite-v2`, passed as
`-DEDIT_APP_ID=\"$(APP_ID)\"` in v2 `Makefile` `WARN`). The os bead
replaces that stub; `os_app_id()` itself has no v1 counterpart and
should be retired in favour of building paths from `EDIT_APP_ID`
directly where the harvested code needs the leaf (see §1.5), or kept as
the single accessor if the worker prefers — either is DAG-clean since
it lives in `os`, the lowest module.

### 0.5 Globals audit (CLAUDE.md: only PLAN-named globals)

File-scope mutable state in L0 at `6fec2e8`, complete list:

| Global | FILE + SYMBOL | PLAN-named? |
|---|---|---|
| `os_fault_service` (fault slots, previous `sigaction[2]`, `pthread_once_t once`, `page_size`, `installed`) | `src/os/os_posix.c`, `static fault_service os_fault_service` | **Yes** — PLAN §2 "pinned `os_fault_service`", §4.1; CLAUDE.md globals line; the v1 comment at the fault service already calls it "the sanctioned os_fault_service (CLAUDE.md globals line, decision doc §8.1)" because the signal ABI gives the handler no context argument |
| Trace rings: `g_rings[TRACE_MAX_THREADS]`, `g_next_ring`, `g_in[]`, `g_in_total`, `g_startup[]`, `_Thread_local tl_ring` | `src/trace/trace.c` | **Yes** — CLAUDE.md names "trace ring"; v1 comment in `trace.c`: "The trace ring is the one permitted global." |
| Malloc guard: `guard_on`, `guard_count`, `_Thread_local guard_exempt` | `src/base/malloc_guard.c` (in the `__GLIBC__` interposition branch) | **Yes, as "allocator hook"** — CLAUDE.md globals line names "allocator hook"; the guard *is* that hook (it interposes `malloc`/`calloc`/`realloc`) |
| CPU-detect caches: two function-local `static int cached = -1` | `src/base/base.c`, inside `edit_cpu_has_avx2` and `edit_cpu_has_popcnt` | Not named — and they are the PLAN §3 targeted fix (replace with atomic/`pthread_once`, §2.5). After the fix they become once-initialised state under `os_once`/`_Atomic`, which the base bead's decision note should name explicitly so the globals list stays closed |
| — | `src/work/work.c`, `src/ipc/*.c` | No file-scope mutable state (all state in caller-owned `work_pool` / `ipc_server` / `ipc_args`). Conforming by construction |

No other globals exist in L0. In particular `os_posix.c` outside the
fault service is stateless (every other `static` is a helper function:
`map_errno`, `fail`, `clock_ns`, `from_stat`, `open_flags`, `lock_fd`,
`root_join`, `page_range`, …).

### 0.6 Test-harness coupling that spans modules

- `tests/os_test.c` states its own contract in `src/os/os.h`'s header
  comment: "The conformance suite tests/os_test.c uses only this
  header, C11 and base.h, and runs unchanged on both [platforms]." It
  includes exactly `os/os.h` + `base/base.h`. This is the model
  citizen: harvestable almost verbatim (minus Windows cases).
- `tests/work_foreground_test.c` is **not** an L0 test despite its name:
  it includes `editor/private.h`, `findui/findui.h`, `font/font.h`,
  `savectl/savectl.h`, `lineidx/lineidx.h` — it is a real-client
  integration test (v1 `src/work/STATUS.md` calls them "foreground
  real-client tests"). It cannot land in Wave 1's L0; it belongs to
  whichever later wave re-creates those clients. The work bead must
  not list it as acceptance; §4.3 splits out what it covers that
  `work_test.c` does not.
- `tests/ipc_test.c` is half L0 suite, half L5 harness: its preamble
  `#define main ipc_cli_main` + `#include "../src/main.c"` (plus
  `editor/private.h`) exercises "main's real startup/ownership code
  with a null renderer" (its own comment). v2 forbids
  `#include "*.c"` in tests (`tools/check_no_c_includes.py`) and v2's
  `main.c` is L5, which an L0 test must never reach into. §5.3 splits
  the file's cases into the L0-keepable majority and the
  main-harness minority that must wait for / be rebuilt against v2's
  L5 via a subprocess (`os_proc_spawn` of the built binary), not an
  include.
- Only **one** `#include "*.c"` exists across all L0 tests/fuzz/bench
  (the `ipc_test.c` one above; verified by grep over the §1–§5 test
  inventories). The rest of v1's many `#include "*.c"` tests are in
  higher layers. L0 is therefore cheap to make lint-clean.

## 1. `os` — `src/os/`

PLAN disposition (§3): **harvest + targeted fix** — "app-id namespace +
pinned `os_fault` lease". PLAN §2: "POSIX vm_reserve/commit, clock,
file/dir/mmap, pinned os_fault_service, wake/poll, namespaced app_dir
CONFIG+DATA". This is the foundation bead: every other L0 module and
every higher layer includes `os/os.h`.

### 1.1 Files and line counts

Measured with `wc -l src/os/*` (M) at `6fec2e8`:

| File | Lines (wc -l, M) | v2 disposition |
|---|---|---|
| `src/os/os.h` | 808 | Harvest, then strip/narrow `_WIN32` branches (§1.4) and re-namespace app-dir docs (§1.5) |
| `src/os/os_posix.c` | 1070 | Harvest as the (only) implementation; rename or keep the `_posix` suffix — worker's choice, but keep FILE+SYMBOL traceability in the commit message |
| `src/os/os_win.c` | 1835 | **Drop** (Windows body) |
| `src/os/utf8.manifest` | 12 | **Drop** (Windows CRT UTF-8 manifest; referenced by `os_proc_spawn`'s comment in `os.h`) |
| `src/os/utf8.rc` | 3 | **Drop** (Windows resource script embedding the manifest) |

No `STATUS.md` in `src/os/` (unlike trace/work/ipc). The design record
is v1 `docs/decisions/edit-177r.2.md` (cited throughout `os.h` as
"decision doc"), including §8.1 (one fault-service installer per
process; `file_bus` deletion sequencing) and §9 (migration rules where
an `os_*` outcome differs from the raw POSIX call it replaced). The os
worker should read it before touching the fault service; it is the
reason several `os.h` contracts look Windows-shaped (§1.4).

### 1.2 Public API (`src/os/os.h`, guard `EDIT_OS_H`)

One public header. It includes only `<stdbool.h>`, `<stddef.h>`,
`<stdint.h>`, `<stdio.h>` and, on non-Windows, `<pthread.h>` (the
POSIX struct layouts embed `pthread_t`/`pthread_mutex_t`/
`pthread_cond_t`/`pthread_once_t` by value — callers allocate the
structs, "no malloc in the layer", per the header's rules block). The
header's preamble is itself part of the contract and must be harvested
with it: `os_err` return codes with native code left for
`os_native_error()`; UTF-8 paths at the API; the closed list of entries
allowed to allocate (`os_thread_start`, `os_env_set`,
`os_dir_iter_open`, `os_proc_spawn`, `os_file_to_stdio`, …) vs the
never-allocating typing-path set (clocks, `os_thread_self`, `os_yield`,
mutex/cond ops, wake signal/drain, `os_file_pread`,
`os_file_pwrite`, `os_file_pwrite_once`) which Law 2 suites count;
per-entry EINTR/short-transfer retry policy; non-inheritable handles.

Exported symbols by group (all in `src/os/os.h`, implemented in
`src/os/os_posix.c` unless noted):

- **Errors**: `os_err` enum (`OS_OK`, `OS_E_NOENT` … `OS_E_INTR`),
  `os_err_name`, `os_native_error`. Limits: `OS_PATH_MAX`,
  `OS_NAME_MAX`, `OS_PATH_SEP` (each carries its (P) derivation in the
  header comment — keep the comments, Law 4).
- **Clocks**: `os_now_ns` (monotonic), `os_thread_cpu_ns`,
  `os_thread_cpu_resolution_ns`, `os_slice_ns`, `os_wall_ns`,
  `os_sleep_ns`, `os_yield`. (`os_slice_ns` exists because the Windows
  thread-CPU clock cannot end a short slice; on POSIX it is the same
  clock as `os_thread_cpu_ns` — keep the seam, it costs one function.)
- **Threads/sync**: `os_thread` + `os_thread_start`/`os_thread_join`/
  `os_thread_self`; `os_mutex` (`OS_MUTEX_INIT`, init/destroy/lock/
  unlock); `os_rmutex` (recursive; header cites piece's pool lock as
  the motivating caller); `os_cond` (init/destroy/wait/signal/
  broadcast); `os_once` (`OS_ONCE_INIT`, `os_once_run`).
- **Wake**: `os_wake` + `os_wake_init`/`os_wake_close`/
  `os_wake_signal`/`os_wake_drain`/`os_wake_wait`, platform accessor
  `os_wake_fd` (POSIX eventfd). Protocol in the header: drain *before*
  consuming announced state. `os_wake_signal` is async-signal-safe on
  POSIX.
- **Metadata**: `os_kind`, `os_identity` + `os_identity_equal`,
  `os_stat` (signed-ns timestamps, (P) range derivation in comment).
- **Files**: `os_file` (`OS_FILE_INIT`, `os_file_is_open`), open flags
  `OS_OPEN_READ/RDWR/CREATE/EXCL/NOFOLLOW/NONBLOCK/WRITE/TRUNC`,
  `os_file_open`/`os_file_open_at`/`os_file_open_anon`/
  `os_file_close`/`os_file_dup`, `os_file_read`/`os_file_write`/
  `os_file_pread`/`os_file_pwrite`/`os_file_pwrite_once` (the last is
  the journal typing-path single-attempt write; header cites
  `journal.h` contract), `os_file_stat`, `os_file_set_size`,
  `os_file_set_sparse` (POSIX no-op `OS_OK`), `os_file_sync`/
  `os_file_sync_data`, `os_file_set_mode`, `os_file_clone` (FICLONE,
  `OS_E_NOTSUP` fallback is caller's copy), `os_file_to_stdio`,
  `os_lock` + `os_file_try_lock`/`os_file_unlock` (flock semantics on
  the open-file object; the session-lease handoff depends on
  duplicate-shares-lock — do not simplify), platform accessors
  `os_file_fd`/`os_file_from_fd`.
- **Directories**: `os_dir` (`OS_DIR_INIT`, `os_dir_is_open`,
  `os_dir_open`/`os_dir_open_at`/`os_dir_close`, `os_dir_sync`,
  `os_dir_try_lock`/`os_dir_unlock` — identity-keyed, never
  path-keyed), `os_stat_at`/`os_stat_path` (+ `OS_STAT_NOFOLLOW`),
  `os_mkdir_at`/`os_mkdir`, `os_rmdir`, `os_unlink_at`/`os_unlink`,
  `os_rename_mode` (`OS_RENAME_REPLACE`/`OS_RENAME_NOREPLACE`) +
  `os_rename_at`/`os_rename`, `os_dir_iter` + `os_dir_iter_open`/
  `os_dir_next`/`os_dir_rewind`/`os_dir_iter_close`, accessor
  `os_dir_fd`.
- **Paths/env**: `os_path_is_absolute`, `os_path_canonical`, `os_cwd`,
  `os_host_name` (added post-`c95b6eb`, §6), `os_app_dir_kind`
  (`OS_APP_CONFIG`/`OS_APP_DATA`) + `os_app_dir`, `os_temp_dir`,
  `os_env_get`/`os_env_set`.
- **Memory**: `os_page_size`, `os_map_granularity`,
  `os_phys_mem_bytes`, `os_map` + `os_map_file`/`os_unmap`/
  `os_map_prefetch`, `os_vm_reserve`/`os_vm_commit` (POSIX: validates
  page range, else no-op)/`os_vm_decommit` (`MADV_DONTNEED`)/
  `os_vm_release`.
- **Fault service**: `OS_FAULT_SLOTS` (defined as 256 in `os.h`;
  comment: "Capacity OS_FAULT_SLOTS registrations, the file module's
  FILE_GUARD_SLOTS (M: src/file/file.c)"), `os_fault_register`,
  `os_fault_epoch`, `os_fault_retire`, `os_fault_test_trigger`
  (test-only native-fault delivery).
- **Random/process**: `os_random` (`getrandom`), `os_proc` +
  `os_proc_how`/`os_proc_status`, `os_proc_spawn`/`os_proc_kill`/
  `os_proc_wait`/`os_proc_id`/`os_proc_self_path`, and POSIX-only
  `os_fd_limit_raise` (soft `RLIMIT_NOFILE` raise; (M)/(P) derivations
  in its comment — keep).

Private helpers in `os_posix.c` (not exported, harvest with the file):
`map_errno`, `fail`/`fail_with`, `copy_out`, `bad_name`, `clock_ns`,
`thread_main`, `ts_ns`, `from_stat`, `open_flags`, `finish_open`,
`lock_fd`/`unlock_fd`, `readable_directory`, `rename_fds`,
`root_join`, `page_range`, and the fault-service internals
`fault_slot`/`fault_service`, `fault_chain`, `fault_recover`,
`fault_handler`, `fault_install`.

### 1.3 Include dependencies

`src/os/os_posix.c` includes only `os/os.h` plus system headers
(`<linux/fs.h>` for FICLONE, `<sys/eventfd.h>`, `<sys/mman.h>`,
`<sys/random.h>`, `<sys/syscall.h>` for `rt_tgsigqueueinfo`, …).
`os.h` includes no other module header. **`os` depends on nothing** —
it is the DAG root, exactly as `os < base` requires. No violation, no
adaptation needed beyond include-path spelling (v1 uses `"os/os.h"`
with `src/` on the include path; v2's build must keep that convention
or the harvest touches every file).

Reverse note for later waves (not L0's problem, but the os worker will
be asked): v1 `os.h` comments name callers in L1/L2 (`piece` pool lock
for `os_rmutex`, `journal` for `os_file_pwrite_once`, `lineidx` slice
clock for `os_slice_ns`, `file` for the fault service). Those are
comments, not includes; keep them — they are the contract's rationale.

### 1.4 Windows-only parts to drop

Files: `src/os/os_win.c` (1835 lines, wc -l M), `src/os/utf8.manifest`,
`src/os/utf8.rc` — drop entirely, do not port, do not stub.

Inside the harvested files, CLAUDE.md Law 5 keeps "only `os.h` /
`plat.h` portability seams". Concretely for `os.h`, every
`#if defined(_WIN32)` branch (there are branches at the `pthread.h`
include, `OS_PATH_SEP`, and in the layouts/initialisers/accessors of
`os_thread`, `os_mutex`, `os_rmutex`, `os_cond`, `os_once`, `os_wake`,
`os_file`, `os_dir`, `os_dir_iter`, `os_map`, `os_proc`, plus
`os_wake_handle`/`os_file_handle`/`os_dir_handle` declarations and the
`#if !defined(_WIN32)` guard on `os_fd_limit_raise`) is seam-shaped:
the *types and function signatures* are the seam, the Windows layout
bodies are not needed for v2 to compile. Two acceptable harvest
shapes, worker picks one and records it:

- **Narrow the seam** (recommended): keep every platform-neutral type,
  enum and signature exactly; replace each `_WIN32` layout with the
  POSIX layout unconditionally (i.e. delete the dead branches). The
  seam survives as "no POSIX type leaks above `os.h` except through
  the named platform accessors (`os_file_fd`, `os_wake_fd`, …)".
  Header comments describing Windows behaviour (reparse points,
  `FILE_RENAME_POSIX_SEMANTICS`, placeholder mapping recipe, dirlock
  files under `%LOCALAPPDATA%\sublimite\dirlocks\`) should be trimmed
  to a one-line "Windows: see v1 `os_win.c` at `6fec2e8`" pointer per
  section, not deleted silently — they encode why some POSIX-visible
  semantics (e.g. `os_file_set_sparse` existing at all, `OS_E_RANGE`
  for over-long mappings) are in the contract.
- Keep the branches verbatim and let the Windows layouts sit dead.
  This preserves a future Windows port's starting point but leaves
  ~20 conditional blocks nobody compiles — CLAUDE.md "no fossils"
  argues against, and `-Werror` cannot check what never compiles.

Either way these symbols have **no Windows-only existence** and must
not be dropped as "Windows parts": `os_file_set_sparse` (POSIX no-op),
`os_map_granularity`, `os_slice_ns`, `os_thread_cpu_resolution_ns` —
they are cheap POSIX functions whose *reason* is Windows; deleting
them would break harvested callers in L1/L2 (`os_slice_ns` for
lineidx slices per the header comment) for zero gain.

`src/os/os_posix.c` itself contains no `_WIN32` branches (it is the
POSIX body whole). Nothing to strip inside it except the app-id
literals (§1.5).

### 1.5 Targeted fixes, located by symbol

**(a) App-id namespace → `sublimite-v2`.** The complete set of
identity literals in `os` (grep `sublimite` over `src/os/`):

- `os_app_dir` in `src/os/os_posix.c`: fallback suffixes
  `"/.config/sublimite"` (`OS_APP_CONFIG`) and
  `"/.local/share/sublimite"` (`OS_APP_DATA`), and the XDG suffix
  `"/sublimite"` appended via `root_join`. These three literals are
  the entire POSIX app-identity in L0.
- `src/os/os.h` doc comments on `os_app_dir` and `os_dir_try_lock`
  spell the v1 paths (comments only; `check_isolation.py` ignores
  comments, but update them anyway — docs in lockstep).
- `src/os/os_win.c` and `src/os/utf8.manifest` spell it too — dropped
  with the files (§1.4).

v2 mechanism already exists: `EDIT_APP_ID` is injected by the Makefile
and `tools/check_isolation.py` rejects any `src/` string literal
containing `/sublimite`, `sublimite.` or `sublimite-`, or equal to the
bare leaf `sublimite` (adjacent literals are concatenated first, so
`"sub" "limite"` tricks fail too). The fix is therefore: build the
three suffixes from `EDIT_APP_ID` (e.g. `"/.config/" EDIT_APP_ID`
shape) **without ever spelling the v1 leaf in a literal** — note that
even the *correct* v2 string `"/.config/sublimite-v2"` as a literal
would trip the checker's `/sublimite` needle; concatenation with the
macro is mandatory, not stylistic. Acceptance must include a test
that sets `XDG_CONFIG_HOME`/`XDG_DATA_HOME`/`HOME` to a temp root and
asserts the returned paths end in the v2 leaf and never equal the v1
paths, plus the non-absolute-XDG-ignored and no-root → `OS_E_NOENT`
rules already specified in the `os_app_dir` header comment.
`config/runtime.c` — the third path PLAN §1/§6 names alongside
`OS_APP_DATA`/`OS_APP_CONFIG` and `ipc_posix.c` — contains **no**
app-dir literal at `6fec2e8` (grep finds only a `"sublimite: %s: …"`
diagnostic prefix in `src/config/runtime.c`); it presumably reaches
the config dir through `os_app_dir`. That is L4's bead to confirm; for
L0 the namespace surface is exactly `os_app_dir` + `ipc` (§5.5).

**(b) Pinned `os_fault` lease (PLAN §4.1) — NEW code, not present in
v1.** What v1 has at `6fec2e8`:

- `fault_slot` in `src/os/os_posix.c`: one `_Atomic uint32_t state`
  packing phase (`SLOT_FREE`/`SLOT_RESERVED`/`SLOT_LIVE`/
  `SLOT_RETIRING` in the low bits, `SLOT_PHASE` mask) plus reader pins
  in steps of `SLOT_READER`; plain `addr`/`len` (immutable while
  LIVE); `_Atomic uint64_t epoch`.
- `os_fault_register`: `pthread_once(&os_fault_service.once,
  fault_install)`, then claims the first FREE slot, **resets `epoch`
  to 0**, publishes LIVE with a release store, returns the bare slot
  index via `int *slot`.
- `os_fault_epoch(int slot)`: acquire-loads the slot's epoch.
- `os_fault_retire(int slot)`: CASes LIVE→RETIRING (blocking new
  reader pins), spins (`sched_yield`) until in-flight
  `fault_recover` readers drain, then release-stores FREE — the slot
  is immediately reusable by the next `os_fault_register`, which
  zeroes its epoch.
- `fault_recover` (signal context): pins a LIVE slot (CAS
  `state += SLOT_READER`, acquire), checks address containment,
  **release-increments `epoch` before** `mmap(MAP_FIXED)`-ing zero
  pages over `[faulting page, view end)`, unpins (release). Comment:
  "Count BEFORE the zero pages become visible to readers."

The ABA PLAN §4.1 wants eliminated is visible right there: a consumer
holding only `int slot` (e.g. an async `lineidx` worker with a
`piece_snapshot`) cannot distinguish "my view, epoch 0" from "slot
retired, reused for a different view, epoch reset to 0" — `epoch`
monotonicity is per-registration, not per-slot-lifetime. v1 survives
this by lifetime discipline in L2 `file` (backing refcounts retire the
slot only at final release; v1 `docs/reviews/edit-pf3l.md` audits
exactly this and calls post-release queries "an invalid object
access, not a supported way to query historical fault state"). PLAN
§4.1 replaces discipline with mechanism for v2's L1-direct use:
"`os_fault_service` exposes a lightweight pinned lease
(`os_fault_pin(slot, gen)` / `os_fault_unpin` + `os_fault_poll`) in
L0, surfaced to L1 through `piece_snapshot`. Retaining a pin keeps
the backing mapping slot pinned against reuse … until `lineidx` or
an async worker releases it".

Harvest guidance for the os worker (design constraints, not a design):

- The existing reader-pin mechanism (`SLOT_READER` counts in `state`,
  retirement waiting for drain) is the obvious substrate: a lease pin
  is a long-held reader pin taken from thread context instead of
  signal context. Whatever shape is chosen, `fault_recover` runs in a
  `SIGBUS` handler — the pin/gen check on that path must stay
  async-signal-safe (lock-free atomics only; `fault_install` already
  refuses to install if the slot atomics are not lock-free — extend
  that check to any new atomic).
- `(slot, gen)` implies a per-slot generation that increments on each
  registration and is returned by `os_fault_register` (signature
  change) or a companion query. `os_fault_poll` (PLAN's name) is the
  consumer-side "did my pinned view fault / is my gen still current"
  check that replaces bare `os_fault_epoch(slot)` reads for retained
  snapshots. Existing `os_fault_epoch`/`os_fault_retire` callers in
  future L2 `file` harvest expect the v1 signatures; the worker
  either keeps them and layers pin/gen beside them, or changes them
  and owns the (small, known) caller updates when `file` lands.
  Record the choice in the bead's decision note — L1's `piece` bead
  (§7, cross-ref) codes against it.
- Red test first (Law 9): register view A, pin `(slot, gen)`, retire,
  re-register different view B reusing the slot, assert the stale pin
  cannot observe B as A (poll reports gen mismatch / pin held the
  slot against reuse, per the chosen semantics), and that unpin then
  allows reuse. This test **fails against harvested-verbatim v1 code**
  (no such API) and against any naive gen-less pin — that is the red
  run. Pair it with the existing `os_test.c` fault cases (recovery,
  prefix preservation, successive splits, capacity → `OS_E_NOSPC`,
  unregistered-fault chaining via `os_fault_test_trigger`) so the
  lease cannot silently break recovery.
- TSan: PLAN §5/§6 puts threaded suites under `make check-tsan`;
  the fault service's signal-handler atomics will need the
  verify/annotate pass PLAN Wave 1 budgets for ("including explicit
  Wave 1 budget to verify/annotate pools and signal handlers
  cleanly"). The os bead's acceptance includes TSan-clean or
  annotated-with-rationale for its own multithreaded cases
  (concurrent first registration "raced at a barrier" per the `os.h`
  comment is already a v1 suite case).

**(c) THP `MADV_NOHUGEPAGE` on `os_vm_reserve` — post-`c95b6eb` fix,
keep.** §6 details commit `59c0621`; the code is the
`#ifdef MADV_NOHUGEPAGE` block inside `os_vm_reserve` in
`src/os/os_posix.c`. One `madvise` at map time; without it
`os_vm_decommit`'s page-granular `MADV_DONTNEED` cannot return memory
backed by a huge page. Harvest verbatim including the comment.

**(d) `os_host_name` — post-`c95b6eb` addition, keep.** Declaration in
`src/os/os.h`, body in `src/os/os_posix.c` (`gethostname` +
`ENAMETOOLONG → OS_E_RANGE` + NUL check). Added by `7c0e9ea` (§6) for
local file-URI classification; harmless, caller is outside L0, keep
so the harvest does not silently shrink the API PLAN's later waves
were written against.

No other PLAN targeted fix names `os`. In particular there is no
v1 "poll" deficiency to fix: PLAN §2's "wake/poll" describes
`os_wake` + `os_wake_wait` (poll(2)-backed, timeout rounded up to
whole ms per the header) as-is.

### 1.6 Tests / fuzz / bench

Measured with `wc -l` (M) at `6fec2e8` (full command in §2.6 preamble
— same invocation listed all L0 test files):

- **`tests/os_test.c` — 1927 lines (wc -l, M).** The conformance
  suite. Includes only `os/os.h` + `base/base.h` (+ libc). No
  `#include "*.c"`. It self-executes children via `os_proc_spawn`
  role arguments (the `os.h` comment: "crash, kill and data-loss
  suites re-execute themselves with a role argument instead of
  fork") — including the unregistered-fault-must-die child case.
  Harvest as v2 `tests/os_test.c` with three adaptations: (i) delete
  or `#if`-out Windows-only expectations (the file was written to run
  unchanged on both platforms; v2 keeps the POSIX expectations and
  drops `tests/win/` companion cases entirely); (ii) app-dir cases
  updated for the v2 leaf + the new namespace test in §1.5(a);
  (iii) new pin/gen ABA cases from §1.5(b). Because it also links
  `base/base.h`, its full green run needs the base bead's header at
  minimum — §7 sequences accordingly (os worker can still red/green
  the fault+namespace cases against a stub-free subset by linking
  only `libcore.a` objects present; the bead text must say how).
- **`tests/journal_os_test.c`, `tests/lineidx_os_test.c`,
  `tests/file_os_cases.inc`** exist in v1 `tests/` — they are L2/L1
  suites exercising `os` through their own modules, **not** L0
  acceptance. Noted so the os worker does not claim them.
- **Fuzz: none.** There is no `fuzz/os_fuzz.c` in v1 (`fuzz/` listing,
  §0 preamble). Law 1 requires `fuzz/<m>_fuzz.c` "if it parses or
  mutates". `os` mostly wraps syscalls, but `os_fault_test_trigger` +
  the fault service mutate mappings, and `os_path_canonical`/
  `os_app_dir` parse environment strings. The honest harvest
  position: v1 has a gap here. The os bead (§7) either adds a
  minimal `fuzz/os_fuzz.c` (suggested surface: `os_app_dir` /
  `os_path_is_absolute` / open-flag validation over fuzzed strings,
  syscall effects confined to a temp dir) or records in its decision
  note why `os` is fuzz-exempt under Law 1's "parses or mutates"
  clause. Do not silently inherit the gap.
- **Bench: none module-local.** No `bench/os_bench.c` in v1. `os`
  costs appear inside other benches (base arena/pool, work, ipc).
  Law 1 benches are tracked numbers, not gates; the os bead needs no
  bench, but must not regress the never-allocate typing-path set —
  which is a test (`os_test.c` + malloc-guard counting), not a bench.

Syscall/fault injection precedent for the rewritten tests: v1
`Makefile` line for `file_acquire_test`
(`-Wl,--wrap=pread,--wrap=os_file_open_at`, §0.3) proves the
`--wrap` pattern against an `os_*` symbol; `os_test.c` itself needs
no wrapping (it tests the real syscalls in a temp dir), and
`os_fault_test_trigger` exists precisely so recovery can be tested
without truncating real files. No `#include "*.c"` rewrite is needed
in this module's tests.

## 2. `base` — `src/base/`

PLAN disposition (§3): **harvest + targeted fix** — "atomic/
`pthread_once` CPU detect". PLAN §2: "arenas, slabs, live counting
malloc guard, atomic/pthread_once cpu-detect". (PLAN's "slabs" is
v1's `edit_pool`, a fixed-size-block pool with an intrusive free
list, *not* a kernel-style slab cache — worth knowing so the worker
does not go looking for a second allocator. There are exactly two
allocators in the file: `edit_arena` and `edit_pool`.) `base` is the
Law-2 substrate: CLAUDE.md Law 2, "Pools and arenas from `src/base`
only."

### 2.1 Files and line counts

Measured with `wc -l src/base/*` (M) at `6fec2e8`:

| File | Lines (wc -l, M) | v2 disposition |
|---|---|---|
| `src/base/base.h` | 87 | Harvest (+ §0.2 rename decision, + CPU-detect fix notes) |
| `src/base/base.c` | 142 | Harvest (+ CPU-detect fix, §2.5; drop internal `_WIN32` commit blocks, §2.4) |
| `src/base/malloc_guard.c` | 79 | Harvest as-is (POSIX/glibc interposition) |
| `src/base/malloc_guard_win.c` | 83 | **Drop** (Windows `--wrap` twin; v1 `mk/win.mk` `WIN_WRAP`) |

No `STATUS.md` in `src/base/`.

### 2.2 Public API (`src/base/base.h`, guard `EDIT_BASE_H`)

One public header; includes `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`,
`<stdio.h>`, `<stdlib.h>` and **no module header** (the `os/os.h`
dependency is implementation-only, in `base.c` — good DAG hygiene to
preserve: consumers including `base/base.h` do not transitively get
`os.h`'s `pthread.h`).

- Macros: `EDIT_LIKELY`/`EDIT_UNLIKELY` (`__builtin_expect`),
  `EDIT_ASSERT` — header comment: "The only sanctioned abort()
  site." (CLAUDE.md: "no `abort()` outside `ASSERT`" — this macro is
  that ASSERT; keep it the sole site, and note it `fprintf`s to
  `stderr` before aborting, which is the one sanctioned exception
  shape to "no `printf` on the UI thread" since the process is
  dying. Keep as-is; do not "fix".)
- `edit_version()` — returns the string literal `"0.0.1-dev"` in
  `base.c`. Harvest note: the *value* is a v1 dev string; the v2
  worker should return a v2-appropriate literal and say so in the
  commit (or drop the function if no v2 caller needs it — but
  `tests/base_test.c` `test_version` exercises it, so keep).
- **Arena**: `edit_arena` (`base`/`size`/`used`, all public fields),
  `edit_arena_mark_t`, `edit_arena_init` (0/-1; reserves via
  `os_vm_reserve`, size aligned up to page), `edit_arena_alloc`
  (power-of-two align, NULL on exhaustion, overflow-checked bump),
  `edit_arena_mark`, `edit_arena_reset_to_mark`, `edit_arena_reset`,
  `edit_arena_free`. Not thread-safe, no locking — single-owner by
  convention everywhere in v1; the header does not state this, the
  harvest should add the sentence.
- **Pool**: `edit_pool` (`base`/`stride`/`capacity`/`fresh`/`live`/
  `free_head`/`map_bytes`, public), `edit_pool_init` (block_align
  power of two, capped at 4096 by validation in `base.c`;
  block_size clamped up to `sizeof(void *)` for the intrusive list),
  `edit_pool_get` (free-list first, then lazy bump carve, NULL when
  full), `edit_pool_put` (validates the pointer is a block of this
  pool, returns false otherwise — including the `live == 0` double-
  put guard), `edit_pool_count_live`, `edit_pool_free`. No decommit
  API: freed blocks recycle through `free_head`; memory returns to
  the OS only at `edit_pool_free` (`os_vm_release`) or implicitly
  never. This is why the THP fix in `os_vm_reserve` (§1.5(c))
  matters to pool *release* measurements — see §6.
- **CPU dispatch**: `edit_cpu_has_avx2()`, `edit_cpu_has_popcnt()` —
  header: "cached after first call". §2.5 is devoted to them.
- **Malloc guard**: `edit_malloc_guard_active()`,
  `edit_malloc_guard_begin()`, `edit_malloc_guard_end()` (returns
  count since begin), `edit_malloc_guard_exempt_begin()` /
  `edit_malloc_guard_exempt_end()` (calling-thread-only nesting
  exemption; header restricts it to exempt *library* calls —
  libxcb/libxkbcommon named in `malloc_guard.c` — "never our own
  code"; other threads keep counting). Implemented in
  `src/base/malloc_guard.c`: on glibc non-sanitizer builds it
  defines the real `malloc`/`calloc`/`realloc` symbols forwarding to
  `__libc_malloc`/`__libc_calloc`/`__libc_realloc` (no dlsym
  bootstrap), counting via relaxed atomics (`count_call`); under
  ASan/TSan (`EDIT_GUARD_ASAN`) or non-glibc it compiles to inert
  stubs with `edit_malloc_guard_active() == false`. That inertness
  contract is load-bearing for v2: CLAUDE.md Law 2 requires the
  release lane to **fail closed** if the guard reports inert
  (`active && count == 0` required) — the v1 header comment says
  "the test skips the count check if not [active]", which is the v1
  behaviour v2 deliberately inverts on the release lane. The base
  bead's acceptance must show the guard *counting* on the release
  build (a deliberate `malloc` inside a begin/end window increments
  the count), not merely returning zero.

Note the guard does not interpose `free`, `aligned_alloc`, `strdup`
etc. — counting only the three allocation entries is the v1 design
(the Windows twin wrapped `free` as well, per v1 `mk/win.mk`
`WIN_WRAP := -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free`;
dropped with the file). Do not extend the interposed set in the
harvest bead; if a typing-path allocation ever slips through an
uncounted entry, that is a Law-1 perf/fuzz repro for its own bead.

### 2.3 Include dependencies

`src/base/base.c`: `"base/base.h"`, `<string.h>`, `"os/os.h"` (for
`os_page_size`, `os_vm_reserve`, `os_vm_commit`, `os_vm_release`).
`src/base/malloc_guard.c`: `"base/base.h"`, `<stddef.h>` only.
`base.h`: no module includes. Sole edge `base → os`: **conforms**
(`os < base`). Nothing in `base` includes `trace`/`work`/`ipc`.

### 2.4 Windows-only parts to drop

- File: `src/base/malloc_guard_win.c` (83 lines, wc -l M) — drop.
  (Its existence is referenced by a comment in `malloc_guard.c`;
  update the comment to point at v1 history or delete the sentence.)
- Inside `src/base/base.c`: two `#if defined(_WIN32)` blocks —
  in `edit_arena_alloc` (commit the touched pages via
  `os_vm_commit` before returning them) and in `edit_pool_get`
  (same, on the bump-carve path). On POSIX, `os_vm_reserve` mappings
  are already writable (`MAP_NORESERVE`, lazily backed), so these
  blocks are dead. **Harvest decision**: delete both blocks, or
  keep an unconditional `os_vm_commit` call (POSIX `os_vm_commit`
  validates the range and returns `OS_OK`, so the call is a cheap
  no-op that keeps the code correct if an `os_win.c` ever returns).
  Deleting is cleaner under "no fossils"; keeping the call is one
  branch and preserves the allocate-then-commit shape PLAN's seam
  story assumes. Either is acceptable — record it. What must not
  happen: keeping `#if defined(_WIN32)` blocks nobody compiles.
- `base.h` arena comment ("Windows: reserve up front, commit
  allocation pages…") — trim with whichever choice above; keep the
  Linux sentence.

### 2.5 Targeted fix: atomic/`pthread_once` CPU detect, by symbol

The defect, exactly. `edit_cpu_has_avx2` and `edit_cpu_has_popcnt`
in `src/base/base.c` each contain:

```c
static int cached = -1;
if (cached < 0) {
    __builtin_cpu_init();
    cached = __builtin_cpu_supports("avx2") ? 1 : 0;   /* resp. "popcnt" */
}
return cached == 1;
```

Three concrete problems:

1. **Data race.** `cached` is a plain `int` read/written unsynchronised.
   Callers at `6fec2e8`: `src/find/literal.c` (chooses
   `scan_avx2` vs `scan_sse2` per literal object — find runs on
   workers, i.e. first call can genuinely race across `work`
   threads), plus `tests/base_test.c` `test_cpu`. In later waves
   `scan`/`raster_kernel` dispatch will call these from workers too.
   Benign on x86-64 in practice (same value written), UB by the C11
   memory model, and a guaranteed TSan report — PLAN §5 runs TSan
   on threaded suites and Wave 1 budgets annotation effort; fixing
   beats annotating.
2. **Redundant duplicated logic + double `__builtin_cpu_init()`.**
   Each function caches separately; first calls to both re-run
   `__builtin_cpu_init`. One detect computing both flags is strictly
   better and is what "cached after first call" in the header
   implies.
3. **No publication ordering.** Even ignoring the race, nothing
   orders the write of `cached` against a reader on another thread.
   An `_Atomic` with relaxed ops would fix the UB minimally, but the
   clean harvest is PLAN's named mechanism.

Fix shapes (PLAN §3 names both; worker picks one):

- **`os_once` (preferred):** a static `os_once` (`OS_ONCE_INIT`) +
  `cpu_detect(void)` computing both flags into plain statics;
  accessors call `os_once_run` then read. Uses the L0 seam directly
  below (`os < base`), goes through `pthread_once` on POSIX, costs
  one acquire-ish branch per call after first. New static state
  (once control + two flags) joins the closed globals list (§0.5) —
  name it in the decision note.
- **`_Atomic int` cache with acquire/release (or relaxed +
  `atomic_thread_fence`):** keeps the self-contained shape, no
  `os.h` need in `base.h` (the fix lives entirely in `base.c`,
  which already includes `os/os.h`). Must handle two threads both
  detecting concurrently (harmless if both store the same value —
  but say so, and make the store release / load acquire).

Red test first (Law 9): TSan build, N threads released from a
barrier each calling both accessors as their first base call —
reports the race on `cached` against verbatim v1 code (red), clean
after the fix (green). `tests/base_test.c` `test_cpu` today only
asserts self-consistency (`edit_cpu_has_avx2() ==
edit_cpu_has_avx2()`) and prints the flags; keep those assertions,
add the barrier race case. Also assert both accessors agree across
threads (every thread observes identical results) — that assertion
is meaningful only after the fix, which is the point.

No other PLAN fix names `base`. Do not let the bead grow a new
allocator: PLAN §2's "slabs" = `edit_pool` (see preamble), and L1's
prepaid piece-node pools size themselves from `edit_pool`
capacity — the sizing derivation is PLAN §4.1's, implemented in the
`piece` bead, not here.

### 2.6 Tests / fuzz / bench

All L0 test line counts in this dossier were measured in one
invocation, `wc -l tests/os_test.c tests/base_test.c
tests/trace_test.c tests/trace_pairs_test.c tests/work_test.c
tests/work_foreground_test.c tests/ipc_test.c tests/ipc_isolation.h
fuzz/ipc_fuzz.c fuzz/trace_fuzz.c fuzz/work_fuzz.c bench/base_bench.c
bench/ipc_bench.c bench/trace_bench.c bench/work_bench.c` (M) at
`6fec2e8`.

- **`tests/base_test.c` — 156 lines (wc -l, M).** Cases:
  `test_version`, `test_arena`, `test_pool`,
  `test_guard_exempt` (with `exempt_alloc` worker helper — proves
  exemption is per-thread and other threads keep counting),
  `test_cpu`. Includes `base/base.h` + `os/os.h`. No `#include
  "*.c"`. Harvest nearly verbatim: add the §2.5 barrier case, the
  guard-actually-counts assertion (§2.2), and (if §0.2 rename is
  approved) the name updates. Thin spots the worker may strengthen
  *within* bead scope: arena mark/reset-to-mark and pool
  foreign-pointer `edit_pool_put` rejection are the two public
  behaviours most worth an explicit case each if absent — check
  before adding, do not duplicate.
- **Fuzz: none.** No `fuzz/base_fuzz.c` in v1. But `base` is the
  clearest Law-1 "mutates" case in L0 (arena bump/mark/reset, pool
  get/put with pointer validation). Unlike `os` (§1.6), an exemption
  argument would be weak: the operation space is small, pure, and
  fast. The base bead (§7) **adds** `fuzz/base_fuzz.c`: seeded
  operation sequences over one arena + one pool, model-checking
  `live` counts, alignment of every returned pointer, non-overlap
  of live blocks, and `put` rejection of foreign/interior pointers.
  Red-green: the fuzzer must run (and pass) against the harvested
  code before the CPU fix, so any later failure is attributable.
- **`bench/base_bench.c` — 43 lines (wc -l, M).** Harvest as
  `bench/base_bench.c`; per Law 1 it prints p50/p99 and exits 0,
  numbers stamped with power state when recorded in the bead. It
  includes only `base/base.h` + `<time.h>` — note it does *not* use
  `bench/harness.h` (unlike trace/work/ipc benches); keep whichever
  shape keeps it compiling, do not refactor benches across beads.

## 3. `trace` — `src/trace/`

PLAN disposition (§3): **harvest + targeted fix** — "ring release on
teardown". PLAN §2: "trace (with ring release on pool teardown)".
The task brief phrases the same fix as "trace ring release
ordering"; §3.5 shows both halves (release *existence* and release
*memory-ordering*) are real and distinct in the v1 code. `trace` is
the G1 instrument: Law 2/G1 suites and PLAN's "trace-ring profile
naming the hot module" (Law 1) read these rings, so its dump format
is a cross-wave contract, not a private detail.

### 3.1 Files and line counts

Measured with `wc -l src/trace/*` (M) at `6fec2e8`:

| File | Lines (wc -l, M) | v2 disposition |
|---|---|---|
| `src/trace/trace.h` | 155 | Harvest (public API); grow release entry (§3.5) |
| `src/trace/trace_fmt.h` | 167 | Harvest as the documented format header (§0.1 note) |
| `src/trace/trace.c` | 222 | Harvest (+ release, §3.5) |
| `src/trace/trace_load.c` | 77 | Harvest (tools/tests loader; malloc-ing, off typing path) |
| `src/trace/replay.c` | 30 | Harvest (`trace_replay`) |
| `src/trace/STATUS.md` | 7 | Read, do not carry over verbatim (v1 bead log: retained-startup-stamps feature, `edit-2ts`); v2 bead writes its own decision note |

`src/trace/STATUS.md` records the retained startup stages
(`edit-2ts`): first-write stamps surviving ring wrap, round-tripping
through the existing dump format, printed by `tools/tracedump` in v1.
That tool (`tools/`) is outside L0's bead — but the format it reads
is `trace_fmt.h`'s, so format stability is what lets Wave 0/4 reuse
v1 tooling. **Do not change the dump format in the harvest.**

### 3.2 Public API

**`src/trace/trace.h`** (guard `EDITOR_TRACE_TRACE_H`; includes
`<stdint.h>`, `<stdio.h>`, `<stddef.h>` only):

- Capacity constants: `TRACE_RING_CAP` (65536u, defined in the
  header; power of two — `ring_put` masks with `TRACE_RING_CAP -
  1`), `TRACE_MAX_THREADS` (16u), `TRACE_INPUT_CAP` (16384u).
  These are format-visible (`trace_fmt.h` asserts ring cap and
  record sizes); changing them is a format change.
- `enum trace_ev`: pipeline stamps `TRACE_T0_INGRESS` …
  `TRACE_T6_PRESENT_COMPLETE`, `TRACE_COMPOSE_BEGIN/END`, startup
  stamps `TRACE_STARTUP_PROCESS_START` …
  `TRACE_STARTUP_EGL_LOAD_END`, `TRACE_RENDER_REJECTED`,
  `TRACE_EV_COUNT`; `TRACE_STARTUP_END` alias. Header rule:
  "Append only: existing dump event IDs remain unchanged." L3/L4/L5
  beads append; nobody renumbers.
- `trace_rec` (`ns`, `frame_id`, `ev`, `thread` — 16 bytes,
  asserted in `trace_fmt.h`).
- Lifecycle/record: `trace_init` (once, before threads),
  `trace_thread_register` (claims a ring for the calling thread,
  idempotent per thread, returns index 0..15 by `TRACE_MAX_THREADS`
  or -1), `trace_record` (stamps `os_now_ns` via `trace_now_ns`;
  silently drops if unregistered), `trace_record_at` (explicit
  timestamp; startup events divert to retained stamps; includes a
  legacy `TRACE_T1_DEQUEUE`/`frame_id == 0` → display-connect
  retention shim — harvest with its comment, it exists so a native
  protocol module did not have to change), `trace_startup_get`,
  `trace_now_ns`, `trace_dump(FILE *)` (binary format, §3.3),
  `trace_reset` (clears events, **keeps registrations**).
  Contract (header): single writer per ring (owning thread only),
  no locks, no allocation after `trace_init`; `trace_dump`/
  `trace_reset` only while no thread is recording.
- Input log: `enum trace_in_kind`, `trace_input_rec` (48 bytes,
  `_Static_assert` in the header; zero-filled for byte-exact
  dumps), appends `trace_input_key`/`_pointer`/`_wheel`/`_resize`/
  `_focus`/`_clipboard`/`_filechange`, queries
  `trace_input_count`/`trace_input_dropped`/`trace_input_copy`
  (oldest first). Single producer (UI thread), overwrite-oldest.
  Note the wheel comment ties units to `PLAT_WHEEL_UNIT` in v1
  `src/x11/plat.h` — a *comment-only* upward reference; when L3
  `plat` lands, retarget the comment. No include, no DAG issue.
- Replay: `trace_input_sink`, `trace_replay_opts` (`fast`,
  `speed`), `trace_replay` — implemented in `src/trace/replay.c`
  (absolute monotonic deadlines, `os_sleep_ns` loop; clamps scaled
  gaps with an in-code bound comment).

**`src/trace/trace_fmt.h`** (guard `EDITOR_TRACE_TRACE_FMT_H`;
includes `"trace.h"` same-module): the `EDTRACE1` dump layout
(version, nrings, rec_size, ring_cap; per-ring blocks oldest-first;
retained startup stamps prepended to ring zero at dump time;
optional trailing `INPT` section, `TRACE_FMT_INPT_TAG`, "Nothing
may follow the section"), `trace_loaded`/`trace_dump` struct typedef
(in the header the struct tag is `trace_dump`, the typedef consumers
use is `trace_loaded` — harvest exactly, tools depend on the name),
`trace_fmt_load_dump`/`trace_fmt_dump_free` (implemented in
`src/trace/trace_load.c`, full validation: magic, version, sizes,
seq continuity `seq == dropped + index`, monotonic `t0_ns`,
key `utf8_len` bounds; rejects without OOB access), legacy
`trace_fmt_load` inline, and header-only gate arithmetic
(`trace_fmt_firsts`, `trace_fmt_latencies`, `trace_fmt_pct`
nearest-rank percentile, `trace_fmt_startup_report`) used by G1
tests and tracedump. These inlines `malloc` — off-path by design;
they are why `trace_fmt.h` must stay separate from `trace.h`
(§0.1).

Implementation state (`src/trace/trace.c`): private `trace_ring`
(`total`, `index`, `recs[TRACE_RING_CAP]`), globals per §0.5, and
`ring_put` — plain (non-atomic) field writes then `r->total++`.
`trace_input_*` appends go through `in_slot` (memset, seq from
`g_in_total`, increment). The retained startup stamps are the only
atomics on the record path (`g_startup[]`, first-write CAS in
`startup_put`; loads in `trace_startup_get`/`trace_dump`).

### 3.3 Include dependencies

`trace.c`: `"trace.h"`, `"trace_fmt.h"` (same module), `"os/os.h"`.
`replay.c`: `"trace.h"`, `<math.h>`, `"os/os.h"` (links `-lm` needs
of `isfinite` are the build's problem, note for the bead).
`trace_load.c`: `"trace_fmt.h"` only (+ libc via it).
`trace_fmt.h`: `"trace.h"` only. `trace.h`: no module includes.
Edges: `trace → os` only. **Conforms** (`os < base < trace`; the
`base` edge is permitted but unused — keep unused). Note v1 files
include same-module headers bare (`"trace.h"`) while cross-module
consumers write `"trace/trace.h"` — both resolve under a `src/`
include path; keep the convention.

Upward consumers to be aware of (not dependencies): `work`
(`worker_main` registers), `main` (`trace_init` + register), bench/
test/tools. Nothing in `trace` knows they exist. Correct direction;
preserve.

### 3.4 Windows-only parts to drop

None. There is no `*_win.c` in `src/trace/`, no `_WIN32` branch in
any trace file (grep-verified). The dump format is documented as
"native little-endian, x86 only" in `trace_fmt.h` — a format
statement, not a portability seam to maintain; keep the sentence.

### 3.5 Targeted fix: ring release (existence) + release ordering

The v1 lifecycle, precisely:

- `trace_thread_register` (`trace.c`): if `_Thread_local tl_ring`
  is set, return its index; else `atomic_fetch_add(&g_next_ring,
  1)`, fail with -1 past `TRACE_MAX_THREADS`, cache
  `tl_ring = &g_rings[idx]`. **There is no unregister/release.**
  `tl_ring` is never cleared; `g_next_ring` never decreases except
  in `trace_init` (which also zeroes totals but does **not** — and
  cannot — clear other threads' `tl_ring`).
- `trace_reset` zeroes totals/stamps and explicitly *keeps*
  registrations.
- `work` teardowns register rings forever: `worker_main` in
  `src/work/work.c` calls `trace_thread_register()` as its first
  act (after publishing `worker_ids`), and `work_pool_shutdown`
  joins the threads — their rings are never returned. Consequence:
  against v1 `trace`, a process that inits/joins work pools
  repeatedly exhausts `TRACE_MAX_THREADS` rings and then records
  *silently nothing* for new workers (`trace_record` drops for
  unregistered threads; registration returns -1 and `work` ignores
  the return). v1 gets away with it because the editor creates its
  pool essentially once. PLAN §2's "(with ring release on pool
  teardown)" is the requirement that v2 not inherit the leak —
  Wave 1's headless editor + test suites will init/shutdown pools
  repeatedly in one process (`work_test.c` already does), so this
  is trial-use-affecting under Law 8 (silently missing G1 evidence
  = a lane passing on absent instrumentation; CLAUDE.md "fail
  closed" cuts the same way).
- Ordering half: `ring_put` publishes records with plain stores and
  a plain `total++`. `trace_dump` reads `r->total` and the records
  as plain loads. Correctness today rests entirely on the header's
  quiescence contract ("must be called while no thread is
  recording") plus thread *joins* as the implicit synchronisation.
  A release operation only makes teardown safe if it also
  **orders** the releasing thread's prior `ring_put` writes before
  the ring's return to the free pool — i.e. the free-list/counter
  update that frees the ring must be a release operation, and the
  claim side (next registrant, or `trace_dump` after joins) must
  acquire it. Similarly `g_in`/`g_in_total` (UI-only producer) and
  `g_startup[]` (already atomic, default seq_cst ops in the code as
  written) need no change; the worker must not "optimise"
  `g_startup` orderings while in there.

Fix shape (constraints for the worker; final design in the bead's
decision note):

- Add `trace_thread_release(void)` (name indicative; PLAN says
  "ring release", no symbol name is pinned): calling thread returns
  its ring for reuse, clears its `tl_ring`, and release-orders all
  its prior records. Registration then needs a free-ring mechanism
  (bitmap/free-stack under atomics) instead of the monotonic
  `g_next_ring` counter — or a generation-stamped ring claim so a
  recycled index cannot be confused with its previous owner in
  dumps (`trace_rec.thread` stores the ring index; `trace_dump`'s
  `used = atomic_load(&g_next_ring)` logic and its ring-zero
  startup-stamp prepend both assume dense, never-reused indices —
  the worker must rework `trace_dump` enumeration to "rings ever
  claimed and not yet superseded" semantics and state them).
- `work` calls it: `worker_main` exit path (after the final
  mutex unlock, before thread return) releases its ring. That call
  is in the **work** bead's file, so either the trace bead lands
  first with `work` still verbatim (release unused until work
  lands — acceptable, tests in trace's own suite cover it), or the
  two beads coordinate the one-line call. §7 sequences trace before
  work; the work bead text names the added call as its only
  non-verbatim line. Also `main`/L5 registrations never release
  (process-lifetime UI thread) — fine, state it.
- Keep failure loud: registration exhaustion after release exists
  means a real over-subscription (`TRACE_MAX_THREADS` live threads)
  — `trace_thread_register` returning -1 must become visible
  somewhere the bead defines (at minimum, `work` stops discarding
  the result with `(void)`; what it does instead — assert in test
  builds, count a dropped-registration diagnostic — is the
  worker's recorded choice. Silent nothing-recording is the v1
  failure mode being removed; do not reintroduce it politely.)
- Red test first (Law 9), two cases: (i) loop of register/record/
  release across more sequential threads than `TRACE_MAX_THREADS`,
  asserting every thread got a ring and its records are dumpable —
  against verbatim v1 code this cannot even compile (no release),
  so the red run is the equivalent exhaustion demo (register from
  `TRACE_MAX_THREADS + k` joined threads, observe -1/dropped
  records) checked in as the failing spec, then green via release;
  (ii) TSan: producer records, releases; second thread claims the
  recycled ring and records; main dumps after joins — must be
  race-free *without* relying on TSan annotations over the new
  claim/release path. Existing `trace_test.c` multithread cases
  (`thread_a`/`thread_b`, wrapped-ring and round-trip tests) are
  the regression floor.

Do **not** use this fix to make `trace_record` lock-free-readable
concurrently with `trace_dump` (that would need per-record sequence
locks or atomic totals on the hot path — a different, unrequested
design with typing-path cost). The quiescence contract stays; release
only extends *when* quiescence can be re-established after teardown.

### 3.6 Tests / fuzz / bench

- **`tests/trace_test.c` — 371 lines (wc -l, M).** Cases include
  ring append/wrap (`test_ring_append_and_wrap` over `build_mixed`
  with `thread_a`/`thread_b` producers), seq monotonicity/fields,
  dump round-trips (plain + wrapped), legacy-v1 dump loading
  (`test_legacy_v1_loads` — direct evidence the format is a compat
  contract; keep this case green in v2, it is the cheapest guard
  against accidental format drift), malformed-dump rejection
  (loader validation), gate arithmetic via `check_gate`, and
  `test_no_malloc` (Law-2 counting over record paths). Includes
  `"../src/trace/trace.h"` and `"../src/trace/trace_fmt.h"` by
  relative path (plus `base/base.h`, `os/os.h`) — normalise to
  `"trace/trace.h"` form under v2's include path. No `#include
  "*.c"`. Add §3.5's two cases here.
- **`tests/trace_pairs_test.c` — 29 lines (wc -l, M).** Small
  companion (includes `os/os.h` + libc); harvest with the suite if
  it compiles against public headers only (verify in the bead; its
  size says helper-or-smoke test, classify it in the bead report
  rather than dropping it unexplained).
- **`fuzz/trace_fuzz.c` — 26 lines (wc -l, M).** Includes
  `"trace/trace_fmt.h"` — i.e. it fuzzes the dump **loader**
  (`trace_fmt_load_dump` validation), the module's one
  hostile-input surface. That satisfies Law 1's "if it parses"
  precisely. Harvest verbatim; seed corpus = the malformed cases
  already enumerated in `test_malformed_rejected` (worker extracts
  minimal seeds; PLAN §5 `make fuzz-smoke` runs committed minimal
  corpora).
- **`bench/trace_bench.c` — 104 lines (wc -l, M).** Includes
  `"../src/trace/trace.h"` + `bench/harness.h`. Harvest; the
  number that matters historically is `trace_record` cost on the
  typing path — if §3.5's release work touches `ring_put` at all,
  the bead records before/after bench output (M, power-stamped per
  Law 1) to show the hot path did not change. `ring_put` should
  not need to change; say so explicitly if it does not.

## 4. `work` — `src/work/`

PLAN disposition (§3): **harvest verbatim** (work is in the verbatim
row with `scan`, `indent`, `dnd`…). PLAN §2: "work (single-producer
mailboxes + physical lease retirement)". "Verbatim" needs three
qualifications, each small and named in §4.5 — none changes
semantics: (i) one added trace-ring release call (§3.5), (ii) the
`base` rename fallout if §0.2's rename is approved, (iii) deletion of
the Linux-compat `work_pool_eventfd` shim *or* its deliberate
retention. Everything else — the pool, mailboxes, epochs, selective
receive, foreground lane — ports untouched. This module carries the
most concurrent-correctness weight in L0; verbatim is the safe
disposition and this dossier endorses it.

### 4.1 Files and line counts

Measured with `wc -l src/work/*` (M) at `6fec2e8`:

| File | Lines (wc -l, M) | v2 disposition |
|---|---|---|
| `src/work/work.h` | 253 | Harvest verbatim (public structs are the API, §4.2) |
| `src/work/work.c` | 626 | Harvest verbatim + §4.5(i) one call |
| `src/work/work_posix.c` | 9 | Decision (§4.5(iii)): drop the shim or keep the file |
| `src/work/STATUS.md` | 114 | Read, do not carry over (v1 bead log spanning `edit-4w1.56`, P1.8d `edit-4w1.51`/`edit-e6x.25`, … — it documents *why* selective receive, continuations and the foreground lane exist; the worker reads it so "verbatim" is informed, then writes a v2 decision note) |

There is no `work_win.c`: `work.c` is written against `os.h`
(`os_thread`, `os_mutex`, `os_cond`, `os_wake`) and compiled for
both v1 Targets. The only platform file is `work_posix.c`, entirely
given over to `work_pool_eventfd` (returns `os_wake_fd(&p->wake)`,
comment: "Compatibility for Linux event loops until their W2
migration").

### 4.2 Public API (`src/work/work.h`, guard `EDITOR_WORK_WORK_H`)

One public header, and unusually the **structs are public by
design**: header comment: "No allocation here; the caller owns
storage aligned to `_Alignof(work_pool)` (64 bytes). Heap callers use
`aligned_alloc(_Alignof(work_pool), sizeof *p)`." Includes
`<stdatomic.h>`, `<stdbool.h>`, `<stddef.h>`, `<stdint.h>` and
`"os/os.h"` (for `os_mutex`/`os_cond`/`os_thread`/`os_wake` members).

- Compile-time capacities (defined in the header; structural, do
  not retune in the harvest — Law 4, any change needs its own
  derivation): `WORK_MAX_RASTER`, `WORK_MAX_WORKERS`
  (`1 + WORK_MAX_RASTER`), `WORK_MAX_JOBS` ("in-flight (queued +
  running) jobs"), `WORK_MAILBOX_CAP` ("messages per worker, power
  of two"), `WORK_MSG_DATA`, `WORK_RASTER_RESERVE` ("one full
  worker batch + completion"), `WORK_DRAIN_MAX_MESSAGES`,
  `WORK_DRAIN_BUDGET_NS` (comment tags it (G), "between-callback UI
  slice budget" — a v1 gate value, carried with its tag),
  `WORK_FOREGROUND_RESERVE`, `WORK_FOREGROUND_WORKER`,
  `WORK_MAX_MAILBOXES`.
- `enum work_class` (`WORK_BULK`, `WORK_RASTER`, `WORK_FOREGROUND`)
  — **takes `WORK_` names, not PLAN-invented ones**; PLAN §4.3's
  "`WORK_BULK`" matches v1 exactly. Dispatched under explicit class
  queues/conds (`class_queue`/`class_condition` in `work.c`); note
  for the FSM lint: class handling in `worker_main` is a chain of
  comparisons deriving the local `cls`, not a `switch` over the enum. Under
  `-Wswitch-enum` that compiles (no switch to check), but if a
  fourth class is ever added the compiler will not find this site.
  Harvest as-is; note in the bead report as a known non-FSM site,
  do not refactor in a verbatim bead.
- Messages/jobs: `work_msg` (`kind`, `generation`, plus
  publish-filled `slot_`/`epoch_`, `data[WORK_MSG_DATA]` — header
  calls it the 64-byte message), `work_ctx` (`pool`, `slot`,
  `epoch` snapshot, `generation`, `worker`, `arg`, worker-private
  `continue_`), `work_job` (`fn`, `arg`, `generation`, `cls`),
  `work_handle` (`slot`, `epoch` where 0 = invalid; "Slots retire
  before epoch wrap", "identities never wrap" within a pool
  lifetime).
- Public layout types: `work_slot` (atomics `epoch`, `cancel_ns`,
  `busy`, `pending`, `finished_epoch`, plus UI-only selective-
  receive state workers never read), `work_mailbox`
  (cacheline-aligned `_Atomic head`/`tail`, `msgs[]`, UI-only
  `received[]` holes from selective receive), `work_queue`,
  `work_pool` (slots, per-worker mailboxes, class queues,
  `os_mutex mu`, `os_cond cv[2]`, threads, `wctx`, atomic
  `worker_ids`, `wake`, dropped counters, and the optional
  foreground lane: `foreground_queue/_cv/_thread/_wctx/_mb` +
  flags).
- Lifecycle (UI thread only; header pins the whole protocol —
  serialize lifecycle/submit/cancel/drain on the UI thread, never
  overlap init/shutdown with use, callbacks may submit/cancel,
  nested drains return 0, inactive pool makes submit/cancel
  harmless and shutdown idempotent): `work_pool_init`
  (`n_bulk` in [1,1], `n_raster` in [0, `WORK_MAX_RASTER`]; failure
  releases everything and leaves an inactive reusable pool),
  `work_pool_init_foreground` (plus dedicated foreground worker/
  mailbox; boundary rules in the header — foreground = bounded CPU
  continuations polling cancellation, blocking I/O belongs to
  `WORK_BULK`; legacy init refuses `WORK_FOREGROUND` submissions),
  `work_pool_shutdown` (cancels, drops queued, joins; undrained
  messages stay reserved-and-stale, drain legal after shutdown),
  `work_pool_wake` (borrowed `os_wake`, NULL while inactive),
  `work_pool_worker_id`, and non-Windows `work_pool_eventfd`
  ("Linux compatibility for callers migrating in W2", §4.5(iii)).
- Submit/cancel (UI only, no allocation): `work_submit` (invalid
  handle with epoch 0 on full/shutdown/identity exhaustion),
  `work_submit_batch` (atomic all-or-nothing, per-class FIFO,
  handles filled before any dequeue), `work_prioritize`
  (BULK/FOREGROUND lease upgrade preserving per-lease result order
  across the mailbox handoff; -1 for inactive/stale/raster),
  `work_cancel` (logical: queued removed at once, running hold
  their slot until return; raster-reserve arithmetic in the header
  — raster workers configured ⇒ bulk cannot use the final
  `WORK_RASTER_RESERVE` slots), `work_handle_finished` (physical
  acknowledgement: true for invalid handle/after shutdown/once the
  worker returned; cancellation alone does not finish — this is
  PLAN `work.h:185`, §0.3, and PLAN §4.7 makes L5's cleanup depend
  on it).
- Worker side (inside a job only): `work_should_stop` (header
  requires polling at least every 5 ms CPU — a v1 contract value
  stated in the header comment, carried as-is), `work_continue`
  (cooperative requeue of the same lease at the back of its class
  FIFO; finished stays false; worker may change, ordering
  preserved by waiting out the old worker's reservations),
  `work_cancel_time_ns`, `work_publish` (false = dropped stale or
  mailbox-full; drops counted in `dropped_stale`/`dropped_full`).
- UI receive: `work_mailbox_drain` (bounded by
  `WORK_DRAIN_MAX_MESSAGES` + `WORK_DRAIN_BUDGET_NS`;
  partial drains re-arm the wake), `work_mailbox_drain_bounded`
  (explicit count + absolute deadline, 0 disables deadline),
  `work_mailbox_pending`, `work_mailbox_receive_bounded` /
  `work_mailbox_receive` (selective receive matching slot+epoch+
  application generation, foreign messages keep reservations and
  order, per-handle cursor progress), `work_mailbox_bind`
  (registered per-identity dispatch so a shared-pool drain cannot
  steal another client's result).

Implementation map (`src/work/work.c`, all non-exported helpers):
`now_ns` (over `os_now_ns`), `mailbox_notify`, `class_queue`/
`class_condition`, `mailbox_count`/`mailbox_at`,
`receive_cursor`, `worker_main` (the worker loop: publish
`worker_ids` (release), `trace_thread_register`, cond-wait/dequeue
under `mu`, deferred-invocation check on `pending` (acquire),
completion stores `finished_epoch` then `busy` (both release)),
`pool_init` (shared body of both inits), then the public functions
in header order, `mailbox_clear_notification`, `mailbox_reclaim`,
`mailbox_consume`, `mailbox_end_drain`. Memory ordering is explicit
throughout (audited: publish writes the message then release-stores
`tail`; consume acquire-loads `tail`; epoch bumps are `acq_rel`;
`cancel_ns` is written before the epoch bump per `work_slot`'s
comment, read relaxed — that one relaxed pairing is v1's documented
choice, "written before epoch" ordering lives in the epoch's
acq_rel op; do not strengthen/weaken casually in a verbatim
harvest, and TSan is the arbiter, §4.6).

### 4.3 Include dependencies

`work.h`: `"os/os.h"`. `work.c`: `"work/work.h"`,
`"base/base.h"`, `"trace/trace.h"`, `"os/os.h"`.
`work_posix.c`: `"work/work.h"`. Edges `work → os, base, trace`:
**conforms** exactly to `os < base < trace < work` (this chain is
why the order exists). Uses of `base` in `work.c` are the
`EDIT_*` macro family from `base/base.h` (verify in the bead if the
§0.2 rename lands — the include itself must stay, since `ipc`-style
DAG trouble would apply in reverse if work dropped the include but
kept using the macros through another header).

The foreground lane deserves one explicit sentence because PLAN
never mentions it: `WORK_FOREGROUND` + `work_pool_init_foreground`
+ `work_prioritize` + `work_continue` continuations are load-
bearing in v1 (`src/work/STATUS.md`: "The editor and jump bench
enable the lane"; lineidx builds and copied-file jumps ride it),
and PLAN §4.3's lineidx regrowth and §4.7's lease retirement
silently presuppose this machinery. Verbatim includes the lane.
Do not let "PLAN didn't mention it" become a reason to strip it —
that would convert a harvest into a downgrade with no repro.

### 4.4 Windows-only parts to drop

- No `_win` implementation file. Two header-level branches:
  `work.h` guards `work_pool_eventfd` with `#if !defined(_WIN32)`
  (keep/drop with §4.5(iii), the guard goes either way since v2
  has no Windows build to exclude), and `work_posix.c` wraps its
  body in `#if !defined(_WIN32)` (same).
- Test-side only: `tests/work_test.c` includes
  `"win/work_hooks.h"` in its Windows leg, and `tests/win/…`
  contains Windows suites. v2 drops `tests/win/` wholesale (PLAN
  §3 deletes Windows builds); the work bead's harvested
  `work_test.c` keeps only the POSIX leg (§4.6).

### 4.5 Targeted adjustments (the three non-verbatim touches)

PLAN names **no** targeted fix for `work` — no §4 seam, no §3 fix
parenthetical. The three touches below come from *other* modules'
fixes landing adjacent to it; the bead report must list them as
the complete diff-from-verbatim manifest (a reviewer should be
able to diff v2 `work.c` against v1 `work.c` at `6fec2e8` and see
exactly these, nothing else):

(i) **Trace ring release call (§3.5).** In `worker_main`, after the
    shutdown break/unlock at loop exit, call the trace release
    entry the trace bead added; and stop discarding
    `trace_thread_register`'s result silently (handle -1 as the
    trace bead's decision note specifies). One call + one checked
    return.

(ii) **`base` rename fallout (§0.2), only if approved.** Pure
    token substitution for any `EDIT_*`/`edit_*` names work uses.
    If the rename is declined, zero lines here.

(iii) **`work_pool_eventfd` (`src/work/work_posix.c`,
    `work_pool_eventfd` in `work.h`).** It is a v1 migration shim —
    "Compatibility for Linux event loops until their W2 migration",
    where *v1's* W2 (os-layer migration) is not v2's Wave 2. In v2,
    event loops are born on `os_wake` (`work_pool_wake` +
    `os_wake_fd`), so the shim has no customer. **Recommendation:
    drop both the file and the declaration.** Counter-argument the
    worker should weigh and record: harvested v1 test/bench code
    uses the eventfd directly (`tests/work_test.c` includes
    `<sys/eventfd.h>`/`<poll.h>`, `bench/work_bench.c` likewise
    polls) — dropping the accessor means those harvested tests
    switch to `os_wake_fd(work_pool_wake(p))`, a small, mechanical,
    *test-only* change that actually validates the production wake
    path. Dropping is right; just count the test edits in scope.

Explicit non-goals for the work bead (verbatim means verbatim):
do not retune capacities, do not replace the condvar sleep with
the wake eventfd (idle workers sleeping on `os_cond` is documented
design — "no wakeups when nothing is queued"), do not merge the
foreground mailbox into the per-worker array, do not touch
ordering annotations except under TSan evidence (§4.6).

### 4.6 Tests / fuzz / bench

- **`tests/work_test.c` — 1538 lines (wc -l, M).** The main suite.
  Includes `os/os.h`, `work/work.h`, `base/base.h`,
  `trace/trace.h`, `tests/core_sync.h` (v1 test harness header —
  the bead must harvest or replace it; classify its contents in
  the report since trace/work tests share it), POSIX headers
  (`<sys/eventfd.h>`, `<poll.h>`, `<dlfcn.h>`, …), and in its
  Windows leg `"win/work_hooks.h"`. No `#include "*.c"`. Harvest
  the POSIX leg; drop the `win/` include and any Windows-only
  cases; apply §4.5(iii) test edits if the shim drops. This suite
  plus shut-down/re-init cycling is what makes §3.5's ring release
  *necessary* — once release lands, add the pool-cycling trace
  assertions (records present after many pool lifetimes) here or
  in `trace_test.c`, worker's choice, named in one bead or the
  other, not neither.
- **`tests/work_foreground_test.c` — 319 lines (wc -l, M). NOT an
  L0 test** (§0.6): includes `lineidx/lineidx.h`,
  `editor/private.h`, `findui/findui.h`, `font/font.h`,
  `savectl/savectl.h`. What it uniquely covers (real clients
  driving `WORK_FOREGROUND`/`work_prioritize`/continuations under
  blocking bulk I/O, per `src/work/STATUS.md`) must be re-created
  when those clients exist (Wave 1 lineidx at the earliest). The
  work bead's acceptance therefore cannot claim foreground
  coverage from this file; instead the bead ensures
  `work_test.c`'s own foreground cases (init_foreground,
  prioritize, continue — present in v1 `work_test.c`; worker
  enumerates them in the report) are the L0 floor, and files a
  named gap: "real-client foreground integration returns with
  lineidx (L1) / findui (L4)".
- **`tests/config_worker_test.c`, `tests/hl_worker_test.c`** —
  real-client suites for other modules riding `work`; not L0,
  noted for the same reason as §1.6's `*_os_test` files.
- **`fuzz/work_fuzz.c` — 112 lines (wc -l, M).** Includes
  `work/work.h` + `trace/trace.h`. `src/work/STATUS.md` records the
  final seeded form: "two raster workers and validates handoff
  between all producer identities." Harvest verbatim (trace
  include becomes *more* honest after §3.5 — the fuzzer registers
  rings per worker thread). Law 1 satisfied: work mutates, and
  this is its mutator fuzzer. Seed corpus: v1 kept seeds under
  `/tmp/edit-corpus/work-fuzz` (STATUS.md) — not committable from
  there; PLAN §5 `fuzz-smoke` needs committed minimal seeds, so
  the bead commits a minimal seed set (or records that the
  harness is self-seeding/deterministic if it is).
- **`bench/work_bench.c` — 382 lines (wc -l, M).** Includes
  `work/work.h`, `trace/trace.h`, `bench/harness.h`. Harvest;
  it is the pre/post evidence that (i)'s extra release call and
  any TSan-driven ordering touch did not move submit/drain costs
  (Law 1: tracked, (M), power-stamped, exit 0 — not a gate).

Verification lanes for this module specifically: PLAN §5 names
`work` first among `make check-tsan` suites, and Wave 1's exit
gate (PLAN §6) requires TSan green on `work`. Also Law 2: `work`
itself allocates nothing (caller-owned pool), and v1
`docs/reviews/work-scan-1.md` already contains the shape of the
required evidence ("an executed release counting test covering
repeated submit/cancel/drain/reuse… Assert that the allocator
guard is active and that the UI-side allocation count stays
zero") — the work bead should ensure that release-build guard
case exists in the harvested suite, failing closed per CLAUDE.md
(`active && count == 0`), with worker fixtures themselves
allocation-free as that review specifies.

## 5. `ipc` — `src/ipc/`

PLAN disposition (§3): **harvest + targeted fix** — "app-id socket
namespace". PLAN §2: "ipc (POSIX Unix socket with namespaced leaf +
abstract name)". PLAN §1 makes this existential: v2 must never
"forward into v1" — a v2 second launch connecting to v1's socket
would hand its file arguments to Tobias's live daily editor. The
namespace fix here is, with §1.5(a), the core isolation work of
Wave 1's L0.

### 5.1 Files and line counts

Measured with `wc -l src/ipc/*` (M) at `6fec2e8`:

| File | Lines (wc -l, M) | v2 disposition |
|---|---|---|
| `src/ipc/ipc.h` | 164 | Harvest (+ namespace docs, + `base` edge §0.1, + drop `_WIN32`-only members §5.4) |
| `src/ipc/args.c` | 307 | Harvest (strip `_WIN32` legs §5.4; keep activation capture §6) |
| `src/ipc/wire.c` | 104 | Harvest verbatim (codec; keep trailing activation block §6) |
| `src/ipc/result.c` | 17 | Harvest verbatim (`ipc_result_string`) |
| `src/ipc/ipc_posix.c` | 595 | Harvest (+ namespace literals §5.5) |
| `src/ipc/ipc_win.c` | 719 | **Drop** (Windows named-pipe body) |
| `src/ipc/crt_win.h` | 35 | **Drop** (Windows CRT-descriptor helper included by `ipc_win.c`/`args.c`) |
| `src/ipc/STATUS.md` | 135 | Read, do not carry over (records `edit-jmn` partial state, P4.9/`edit-457.9` design + bounds, `edit-457.21` hardening §§, and the rename-to-`sublimite` decision pointer `docs/decisions/rename-sublimite.md`) |

STATUS.md flags one open v1 issue the ipc worker must *see but not
fix* in this bead (Law 3): `edit-jmn` is "partial; existing-file
handoff still blocked" — the blocker is in `src/file` (an inotify
instance per file → `EMFILE` on mass open), explicitly out of L0
scope; the L0 bead report cites it as inherited-known, belonging
to L2 `file`'s harvest.

### 5.2 Public API (`src/ipc/ipc.h`, guard `EDIT_IPC_H`)

One public header; includes `"base/base.h"` (the §0.1 edge),
`"os/os.h"`, `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`.

- **Bounds** (all defined in the header with rationale comments —
  carry verbatim, Law 4 derivations ride in the comments and in
  STATUS.md/`edit-457.21` review refs): `IPC_MAX_PATHS`,
  `IPC_PATH_CAP`, `IPC_MAX_WIRE`, `IPC_MAX_CLIENTS`,
  `IPC_ACTIVATION_CAP` (comment derives it from token shapes:
  "compositors issue tokens of a few dozen characters… 512 leaves
  an order of magnitude of room" — an (E)-flavoured bound in v1's
  comment; carry it as v1 stated it, do not re-derive),
  `IPC_DRAIN_BYTES`/`IPC_DRAIN_CALLBACKS`/`IPC_DRAIN_ACCEPTS`,
  `IPC_REQUEST_DEADLINE_MS`, `IPC_EVICT_IDLE_MS`,
  `IPC_LOCK_TIMEOUT_MS`, `IPC_RX_SMALL_SIZE`/`IPC_RX_SMALL_COUNT`/
  `IPC_RX_BIG_COUNT` (receive-pool shape; STATUS.md: "the original
  33.5 MB arena is gone and ipc_bench asserts it stays gone").
- **Results**: `enum ipc_result` (`IPC_OK`, `IPC_EXISTS`,
  `IPC_INVALID`, `IPC_LIMIT`, `IPC_IO`, `IPC_PROTOCOL`,
  `IPC_TIMEOUT`, `IPC_BUSY`, `IPC_REJECTED`),
  `ipc_result_string` (`src/ipc/result.c`; static text, no alloc).
- **Requests/args**: `ipc_path` (`path`, 1-based `line`, `col`),
  `ipc_request` (`paths[IPC_MAX_PATHS]`, `count`, `cwd`,
  `stdin_data`/`stdin_size`, `wait`, `has_stdin`,
  `new_instance`, post-`c95b6eb` `activation_token`/`startup_id`
  (§6)), `ipc_args` (request + owning `edit_arena` + launch-local
  never-serialized `font_family`/`font_file`/`font_fallbacks`).
  Functions in `src/ipc/args.c`: `ipc_parse_args` (argv parse;
  existing whole filename wins over `file:line:col` suffix
  parsing; `-` unique; `--` ends options; `--font-*` last-wins),
  `ipc_args_read_stdin`, `ipc_args_set_activation` (bad env
  values leave fields absent; only arena exhaustion fails),
  `ipc_args_fini`.
- **Wire**: `ipc_wire_encode` / `ipc_wire_decode` in
  `src/ipc/wire.c` (decoder borrows wire storage, checked trailing
  NULs, zeroes output on failure; encoder never serializes
  `new_instance`; activation strings ride a trailing block flagged
  by flags-byte bit 2 with size in header bytes 6–7, requests
  without them byte-identical to the previous format, client
  resends plain on `IPC_PROTOCOL` from an older server — the full
  compat contract is in the `ipc.h` comment, keep it).
- **Server/client**: `ipc_token`, opaque `struct ipc_peer`/`struct
  ipc_rx` forward decls, public `ipc_server` (fds, `edit_arena`,
  `peers`, `next_token`, `socket_path[108]` "leaf name inside
  dir_fd… abstract: empty", `socket_device`/`socket_inode`,
  `owns_path`, `dir_fd`/`timer_fd`, tunable
  `request_deadline_ms`/`evict_idle_ms`, `wait_drops`,
  `drain_bytes`, `rx`; Windows-only `win` pointer, §5.4),
  `ipc_open_callback`, `ipc_server_init` /
  `ipc_server_init_display` (lossless bounded DISPLAY encoding in
  the endpoint name; different displays never share a primary),
  `ipc_server_init_isolated` (--new-instance: never binds),
  `ipc_server_fd` (the epoll fd), `ipc_server_drain`
  (nonblocking, budgeted), `ipc_server_report_closed`,
  `ipc_server_token_live` (with the caller's sweep obligation
  documented on it), `ipc_server_fini`, launcher quartet
  `ipc_launcher_pair` / `ipc_server_adopt_wait` /
  `ipc_launcher_wait` (main splits launcher/UI for --wait),
  `ipc_client_send` / `ipc_client_send_display` (startup-only
  blocking handoff; ACK then optional undeadlined wait).
  Test hook documented on `ipc_server_init`:
  `EDIT_IPC_NAMESPACE` env → abstract name
  `sublimite-<uid>-<namespace>`; empty → `IPC_INVALID`, overlong →
  `IPC_LIMIT`; filesystem endpoints ignore it; must be set before
  IPC calls and held stable. **This hook survives the namespace
  fix** (§5.5) — it is how tests get private endpoints.

Implementation map (`src/ipc/ipc_posix.c`, `#if !defined(_WIN32)`
whole file): private `ipc_peer` (per-client wire/head/reply state,
`accepted_ms`/`active_ms`), `ipc_rx` (small/big receive slab
pools + armed deadline), endpoint machinery — `endpoint` struct,
`milliseconds`, `own_peer` (`SO_PEERCRED`, `EDIT_IPC_TEST_PEER_UID`
test hook for modelling a foreign squatter, abstract only),
`display_suffix` (hex-encodes DISPLAY), `resolve_dir` (opens and
*verifies* the runtime dir once — owner uid, no group/other mode
bits — pins `dir_fd` (O_PATH) so later ops cannot be redirected by
rename; builds the leaf + `/proc/self/fd/<fd>/<leaf>` addr),
`resolve` (runtime dir vs abstract selection),
`resolve_fallback` (private FS endpoint when the abstract name is
foreign-squatted; `EDIT_IPC_FALLBACK_DIR` hook or
`/run/user/<uid>`) — client side `ready`, `connect_socket`
(peer-cred checked), `transfer`, `reply`/`receive_reply` ("EDIR"
frames), `deliver`, `ipc_client_send_display` (incl. the
plain-resend compat path) — server side `rx_acquire`/`rx_release`,
`peer_waiting`, `drop_peer` (bumps `wait_drops` for vanished wait
clients), `interest`/`flush_peer`/`reject_peer` (epoll interest +
reply flushing), `process_peer` (budgeted header→alloc→decode→
callback state machine; bounded admission `IPC_BUSY` when the rx
pool is out — **this is a multi-state lifecycle implemented as
structured straight-line code over peer fields, not an enum FSM**;
CLAUDE.md requires explicit FSMs for *multi-mode lifecycles*
handled by switch under `-Wswitch-enum`. Judgment call for the
worker to record: v1 models peer progress via `opened`/
`expected`/`received`/`reply_size`/`sent`/`closed` fields with
tight invariants audited in `edit-457.21`; converting to an enum
FSM is a rewrite of the module's most-reviewed code for lint
elegance. Recommendation: harvest as-is, record the FSM-rule
reading (single-client request pipeline with boolean *flags*, not
coordinating modal booleans across the server lifecycle) in the
decision note; revisit only on a repro, Law 8), `expire_peers`/
`rearm_timer` (timerfd deadlines), `take_slot` (idle-eviction),
`ipc_server_drain` (two epoll batches per call — the `batch < 2`
bound is v1's, carry it), `lock_with_deadline`, `reset_server`/
`teardown` (identity-checked socket unlink: `fstatat` device+inode
must still match before `unlinkat`), `setup_runtime` (epoll +
timerfd + arena-carved peers/rx pool), `start_endpoint`
(lifecycle lock leaf derived from the socket leaf by overwriting
its suffix with "lock" — see §5.5, this string surgery is why leaf
*shape* is contract), stale-socket probe/adopt logic (connect
probe distinguishes live primary (`IPC_EXISTS`) from
`ECONNREFUSED` stale socket (unlink + bind); abstract retry loop;
foreign-squatter fallback), and the init/isolated/launcher/client
publics at the tail.

`args.c` internals worth naming: arena-backed string snapshot of
cwd/paths, the `-` (stdin) handling with `ipc_args_read_stdin`
taking a raw `int fd` (POSIX-shaped public signature — on v2 it
stays POSIX; the Windows CRT-fd dance via `crt_win.h` drops,
§5.4), suffix `path:line:col` parsing with existing-file-wins.
Existence checking lives in `canonical` in `src/ipc/args.c`, which
exists in two legs: one already uses `os_stat_path`, the POSIX leg
calls raw `stat()` (`<sys/stat.h>`) — a small seam bypass the
harvest can close by keeping the `os_stat_path` form; record it if
touched, it is the only raw-syscall reach-around spotted in ipc's
POSIX code outside `ipc_posix.c` itself.

### 5.3 Include dependencies

`ipc.h`: `"base/base.h"`, `"os/os.h"`. `args.c`: `"ipc.h"`
(same-module bare spelling), `"os/os.h"`, Windows-gated
`"crt_win.h"`/`<io.h>`. `wire.c`: `"ipc.h"`, `"os/os.h"`.
`result.c`: `"ipc/ipc.h"`. `ipc_posix.c`: `"ipc.h"` + POSIX
system headers only (epoll, timerfd, socket, un). Edges:
`ipc → os` **conforms**; `ipc → base` **violates the DAG as
written** (§0.1 — the dossier's headline blocker, resolution
there). No `ipc → trace/work` edge exists (ipc has its own
epoll/timerfd; it does not use `work` threads — "All server calls
belong to the loop thread").

Consumer note: PLAN §4.7 has L5 handling "IPC `file:line:col`
jumps … as `BUF_OPENING` async states" — the *parsing* of
`file:line:col` lives here in `ipc_parse_args`/`ipc_path`; the
async *opening* is L5's. No change needed in L0 for that seam.

### 5.4 Windows-only parts to drop

- Files: `src/ipc/ipc_win.c` (719 lines, wc -l M) and
  `src/ipc/crt_win.h` (35 lines) — drop. With them go the
  SID-scoped named pipe + Local mutex endpoint, remote-client
  refusal, and the self-spawn handoff.
- In `src/ipc/ipc.h`: `ipc_server.win` (`#if defined(_WIN32)`
  member — remove; struct layout is v2-internal since no v2 code
  exists), and the whole `#if defined(_WIN32)` declaration block:
  `ipc_server_wake`, `ipc_launcher_prepare`,
  `ipc_server_connect_wait` — remove. The `ipc_server_init`
  comment's Windows paragraphs (SID pipe, "Named objects leave no
  stale files", runtime-as-namespace on Windows) — trim to a v1
  pointer like §1.4.
- In `src/ipc/args.c`: every `#if defined(_WIN32)` leg (file-scope:
  `crt_win.h`/`<io.h>` includes, plus branches in the stat/stdin/
  path handling — there are several, grep the file). The POSIX
  legs are the harvest; `ipc_args_read_stdin(ipc_args *, int fd)`
  keeps its `int fd` (real POSIX fd in v2, no CRT translation).
- `src/ipc/wire.c` and `result.c`: no `_WIN32` branches (wire is
  platform-neutral bytes; the resend-plain compat logic lives in
  `ipc_posix.c`'s `ipc_client_send_display` and in the dropped
  win file — harvest keeps the POSIX copy only).
- Identity literals inside dropped code (for the isolation audit's
  completeness): `ipc_win.c` spells `sublimite-…` pipe/mutex names
  and `\\.\pipe\sublimite-launch-…` endpoints. Dropped with the
  file; listed here so nobody re-greps v1 later and thinks v2
  missed a live site.

### 5.5 Targeted fix: ipc namespace → `sublimite-v2`, by symbol

Every string that makes v1's endpoint findable by another process
is built in **two functions** in `src/ipc/ipc_posix.c`:

1. **Filesystem leaf** — `resolve_dir`: leaf =
   `"sublimite-%lu%s.sock"` (uid + display suffix from
   `display_suffix`) inside the verified runtime dir. The leaf is
   copied to `ipc_server.socket_path` for identity-checked unlink
   in `teardown`, and `start_endpoint` derives the lock leaf by
   copying the leaf and overwriting its last 4 characters —
   `memcpy(lockleaf + ln - 4, "lock", 4)` in `start_endpoint` —
   turning `<leaf>.sock` into `<leaf>.lock` (i.e.
   `sublimite-<uid><suffix>.lock`).
   **Any new leaf must keep a 4-char replaceable extension of the
   same shape, or `start_endpoint`'s derivation must be updated in
   the same diff** — the test in the bead must assert both leaf
   and lock leaf names, not just connectivity.
2. **Abstract name** — `resolve` (no runtime dir):
   `sun_path+1` = `"sublimite-%lu%s"` or with the test hook
   `"sublimite-%lu-%s%s"` (namespace from `EDIT_IPC_NAMESPACE`).
   Abstract name has no file to inspect; `tests/ipc_test.c` +
   `test_ipc_isolate` are the verification vehicle (§5.6).

Plus fallback endpoint (`resolve_fallback`) which re-enters
`resolve_dir` — inherits the leaf fix automatically. And the
`ipc.h` comment spelling the default names (comments don't trip
the checker; update anyway).

The v2 build mechanism forbids the naive patch: `check_isolation.py`
flags `/sublimite`, `sublimite.`, `sublimite-` in *any* `src/`
string literal — and `sublimite-v2-…` **contains** `sublimite-`.
So the new names must be *built from* `EDIT_APP_ID`, never spelled:
e.g. format strings shaped `EDIT_APP_ID "-%lu%s.sock"` and
`EDIT_APP_ID "-%lu%s"` (with/without `-%s` namespace segment).
Resulting on-wire names will read `sublimite-v2-<uid>….sock` /
abstract `sublimite-v2-<uid>…` — visibly distinct from every v1
name in `ls $XDG_RUNTIME_DIR` and `/proc/net/unix`, never
connectable by v1, never binding over v1. Lengths grow by the
length delta between the two app ids (derivable at build time
from `EDIT_APP_ID`, no invented number): leaf buffers are
`endpoint.leaf[64]`, `lockleaf[64]`, `ipc_server.socket_path[108]`,
abstract bounded by `sockaddr_un.sun_path` — all already
`snprintf`-guarded with `IPC_LIMIT` on truncation. The bead must
re-check worst cases with display suffixes at those bounds —
existing `ipc_test.c` oversized-namespace/display cases are the
floor; keep them passing at the new lengths. Do not enlarge
buffers without noting the new worst-case derivation (Law 4):
the derivation is `strlen(EDIT_APP_ID)` + uid digits + suffix cap
already enforced by `display_suffix` (`cap < 3 || n > (cap-3)/2`
over its 64-byte out buffer).

Deliberate non-changes (state them in the bead, isolation is
about *identity*, not wire compat): the wire magic/frames
(`"EDIP"` request header check in `process_peer`, `"EDIR"`
replies in `reply`) stay as-is — endpoints can no longer collide,
so cross-version wire compat is moot *except* the
activation-block resend-plain path (§6), which is about older
*v2* servers once v2 ships. `EDIT_IPC_NAMESPACE`,
`EDIT_IPC_TEST_PEER_UID`, `EDIT_IPC_FALLBACK_DIR` hooks keep their
names (tests set them; renaming the `EDIT_` prefix is cosmetics
with 40+ references across suites — out of bead scope, Law 3).

Red test first (Law 9): against verbatim-harvested code, a test
that inits a server in a temp runtime dir and asserts the leaf
starts with the v2 app id **fails** (v1 leaf), and a cross-check
that a client built against v1 names gets `IPC_IO`/no-listener
on the v1 leaf while the v2 leaf serves — green after the fix.
That red→green pair, plus `check_isolation.py` green over `src/`,
is the bead's isolation evidence. Note the subtle dual: v2 must
also not *adopt* a live v1 primary's stale-socket path — with
distinct leaves, `start_endpoint`'s probe/unlink simply never
sees v1's socket. The test should create a decoy file/socket at
the **v1** leaf name in the temp dir and assert v2 init leaves it
untouched (exists afterwards, byte-identical): that is the
"never retire live v1" property in miniature (PLAN §1's journal
version of the same rule belongs to L2).

### 5.6 Tests / fuzz / bench

- **`tests/ipc_test.c` — 1258 lines (wc -l, M).** Must be *split*,
  not harvested whole. Structure at `6fec2e8`: it includes
  `ipc/ipc.h` + `ipc_isolation.h`, then `editor/private.h`, then
  `#define main ipc_cli_main` / backend stubs
  (`cli_null_backend`, `cli_script_run`)/ `#include
  "../src/main.c"` — the only `#include "*.c"` in L0's test
  estate (§0.6). Two populations of cases:
  - **Pure-L0 majority** (endpoint/namespace validation incl.
    `EDIT_IPC_NAMESPACE` empty/oversized/alternate cases, wire +
    result-string cases, peer-credential rejection + abstract→FS
    fallback (`EDIT_IPC_TEST_PEER_UID` forked client), display-
    endpoint separation, lifecycle/stale-socket, activation-block
    compat (`t_activation` per commit `bafe540`'s message, §6),
    `--wait` token mechanics at the ipc layer). Keep in v2
    `tests/ipc_test.c` linking `libcore.a` only.
  - **Main-harness minority** (CLI tab capacity `edit-jmn`
    counts, `edit-m25u` RLIMIT_NOFILE startup replication, launcher
    split/adopt/UI-death through real `main`). These test *L5
    main's* use of ipc. Under v2 rules they cannot include
    `main.c`; they either move to the L5/Wave-1-mini-app bead
    driving the built `sublimite-v2` binary as a subprocess (the
    honest equivalent — `os_proc_spawn` exists for exactly this),
    or are deferred with a named bead reference. What they must
    not do is hold the L0 bead hostage or sneak `editor/` includes
    into an L0 suite. The worker enumerates every case in the file
    into keep/move in the bead report — a silent drop of any case
    is a review failure.
  `CLIENT_TIMEOUT_MS` and the wait timeouts in the file are v1
  test values; carry them with the file, do not re-derive.
- **`tests/ipc_isolation.h` — 63 lines (wc -l, M).**
  `test_ipc_isolate` / `test_ipc_isolation_cleanup` /
  `test_ipc_remove_contents` (§0.3). Harvest into v2 `tests/`
  with: temp template renamed off `"/tmp/sublimite-ipc-XXXXXX"`
  to a v2-attributable template (tests are not scanned by
  `check_isolation.py`, but a leftover dir should say whose it
  is; keep the `+5`-style namespace derivation consistent with
  the new template or replace it with an explicit suffix —
  fragile string surgery in a *test fixture* is fixable cheaply:
  build the namespace from `mkdtemp`'s returned basename by
  pointer arithmetic on the actual `/`, not a magic offset).
  Used by both `ipc_test.c` and `bench/ipc_bench.c`.
- **`fuzz/ipc_fuzz.c` — 223 lines (wc -l, M).** The largest L0
  fuzzer; STATUS.md calls it the "decoder fuzzer". Includes
  `ipc/ipc.h` + socket headers: it drives wire decoding and,
  per its socket includes, server-path bytes. Harvest verbatim;
  Law 1 satisfied twice over (ipc parses — args + wire).
  Worker verifies whether `ipc_parse_args` (argv strings) is
  inside its surface; if not, adding argv fuzz ops is in-scope
  strengthening for a *parsing* module, red-green separable.
  No `#include "*.c"`.
- **`bench/ipc_bench.c` — 58 lines (wc -l, M).** Includes
  `ipc/ipc.h`, `tests/ipc_isolation.h` (as
  `"../tests/ipc_isolation.h"`), `bench/harness.h`. Carries
  STATUS.md's assertion-of-record that the receive pool stays at
  its pooled size ("ipc_bench asserts it stays gone", of the old
  giant arena) — keep that assertion working through the harvest;
  it is a regression tripwire disguised as a bench (Law 1 benches
  exit 0 and print numbers; an assertion inside is v1's choice,
  preserve it knowingly).

Syscall-injection note for the split suite: cases needing to fail
specific syscalls (bind/lock/connect paths in `start_endpoint`)
should use `-Wl,--wrap` per PLAN §5 (the §0.3 `Makefile:156`
precedent wraps `pread` and `os_file_open_at` the same way), or
the existing env hooks — never a return to including
`ipc_posix.c` to reach `resolve`/`start_endpoint` statics. If a
kept v1 case *did* depend on main-include visibility into
internals, that case belongs to the move population, and the
report says why.

## 6. Cross-cutting: v1 fixes after `c95b6eb` the harvest must keep

PLAN was written against `c95b6eb` ("Session 27: HANDOFF (v1
reached…)"). Between `c95b6eb` and the oracle pin `6fec2e8` there
are 46 commits (M: `git rev-list --count c95b6eb..HEAD` at the
pin). Harvesting *at the pin* keeps all of them automatically —
this section exists so no worker "helpfully" reverts to PLAN-era
code, and so reviewers know which post-PLAN behaviours are
deliberate. L0-touching commits in that range (M: `git log
--oneline c95b6eb..HEAD -- src/os src/base src/trace src/work
src/ipc`, plus the L0 test/bench paths):

1. **`59c0621` (`edit-b6ai.1`) — THP: no huge pages on `os_vm`
   reservations. KEEP (POSIX, production).** Change: in
   `os_vm_reserve` (`src/os/os_posix.c`), one
   `madvise(p, len, MADV_NOHUGEPAGE)` under `#ifdef
   MADV_NOHUGEPAGE` right after the reserve `mmap`, with the
   comment "decommit is page-granular (MADV_DONTNEED): under
   THP=always a huge page would keep memory resident after a
   release (edit-b6ai.1). Once, at map time; best effort." The
   commit message records the measurement that justifies it —
   resident undo-pool bytes after release going to 0 B in both
   release and sanitizer builds (numbers as recorded in the commit
   message at `59c0621`; treat as (M) on that box, do not copy
   them into v2 gates). Mechanism, for the os worker:
   `os_vm_decommit` is `madvise(MADV_DONTNEED)`; a THP-backed
   range cannot be returned page-by-page, so pools/arenas built
   on `os_vm_reserve` appeared to leak RSS after release on
   THP=always systems. The same commit's `tests/savectl_test.c`
   THP measurement guard is L2's, and its `tests/keys_test.c`
   libxkbcommon-version tolerance is L4's — both ride along with
   their own modules. v1 decision record:
   `docs/decisions/edit-b6ai.1.md`. Acceptance hook for the os
   bead: on a THP=always box the RSS-after-release property is
   the regression test's *intent*; on the coordinator's box,
   whatever its THP mode (check
   `/sys/kernel/mm/transparent_hugepage/enabled` and record it,
   the power-state-stamp discipline of Law 1 applies to the
   environment generally), the bead at minimum keeps the
   `madvise` line and comment verbatim and says which mode the
   green run used.

2. **`bafe540` (`edit-b6ai.8`) — second-launch handoff raises the
   running window: activation tokens over IPC. KEEP the ipc half
   (POSIX); the window-raising half is L3/L5.** L0 changes in this
   commit: `src/ipc/ipc.h` (`IPC_ACTIVATION_CAP` + its derivation
   comment; `ipc_request.activation_token`/`startup_id`;
   `ipc_args_set_activation` declaration; wire-compat comment),
   `src/ipc/args.c` (`ipc_args_set_activation` — copies
   `XDG_ACTIVATION_TOKEN`/`DESKTOP_STARTUP_ID` into the arena,
   unusable values simply leave the fields absent),
   `src/ipc/wire.c` (trailing activation block, flags bit 2,
   bytes 6–7 size; absence = byte-identical old format),
   `src/ipc/ipc_posix.c` (client resends without the block when
   an older server answers `IPC_PROTOCOL`, in
   `ipc_client_send_display`), `src/ipc/ipc_win.c` (dropped with
   the file, §5.4 — its `AllowSetForegroundWindow` half is moot),
   `tests/ipc_test.c` (`t_activation` + main-harness activation
   cases; split per §5.6). Design record:
   `docs/decisions/edit-b6ai.8.md`. Why the harvest keeps it
   rather than reverting to PLAN-era wire: (a) it is the current
   production behaviour of the daily editor — v2 feature parity
   (PLAN §1 "v1 feature parity minus debt") includes
   raise-on-handoff once L3 `plat_activate` exists; (b) the
   trailing-block design makes keeping it free for L0 — no L0
   consumer is obliged to *act* on the tokens, only to carry
   them; (c) reverting would fork the wire format from the
   oracle's for zero isolation benefit (endpoints already cannot
   collide, §5.5). The `src/main.c` 4-line touch (capturing the
   env at startup) belongs to the L5 bead — the ipc worker keeps
   `ipc_args_set_activation` API-complete and notes the pending
   caller.

3. **`7c0e9ea` (`edit-177r.9`, Windows link-and-run epic) — POSIX
   residue: `os_host_name`. KEEP the 9 POSIX lines; drop the
   rest with the Windows files.** In L0 this commit changed only:
   `src/os/os.h` (+2 lines: `os_host_name` declaration, "for local
   file URI classification"), `src/os/os_posix.c` (+7 lines: the
   `gethostname` body), `src/os/os_win.c` (dropped),
   `tests/os_test.c` (+5 lines, its case — keep with the suite).
   Its other L0-adjacent promises ("portable locks and leases,
   T6 mailbox") were realised in Windows bodies and higher
   modules; nothing in `work`/`base`/`trace` POSIX code changed.
   Listed so nobody expects a hidden work/behaviour delta here.

4. **`210938b` (Session 29, "monolith box builds (gcc 16 / clang
   23)") — toolchain-portability touches in L0 tests/bench. KEEP
   automatically.** Within L0 test/bench paths: `tests/os_test.c`
   (7 lines: "once_flag clash with <threads.h>", unused-sink
   fixes, per the commit message) and `bench/base_bench.c` (1
   line, unused sink). No production L0 code changed. Value to
   v2: these are exactly the warnings-as-errors (`-Wall -Wextra
   -Werror -Wshadow -Wconversion`) fixes a newer toolchain
   demands; harvesting at the pin inherits them. If v2's box runs
   the CLAUDE.md-named gcc 13 / clang 18 they are inert; either
   way, do not resurrect the clashed identifiers while editing
   the suites.

Everything else in `c95b6eb..6fec2e8` (42 further commits by the
same count: Omarchy theme/font/editor integration `edit-b6ai.2/.3/
.11`, Wayland primary selection `edit-b6ai.9`, WSL-share probes
`edit-177r.21/.22`, beads/worklog exports, …) touches no L0
production file — verified by the pathspec log above returning
only the commits listed. In particular `src/base/`,
`src/trace/` and `src/work/` production code is **bit-identical**
between PLAN's base and the pin (no commits in range), so PLAN's
claims about them need no post-PLAN correction — only `os`
(THP + host name) and `ipc` (activation) moved.

One dating caveat for reviewers: the oracle's commit dates in
this range postdate PLAN's writing but the *content* order above
is topological (`git log` order at the pin), which is what the
harvest consumes. State the pin (`6fec2e8`) in every bead; never
harvest from a floating v1 `main`.

## 7. Proposed bead split — L0 part of Wave 1

Conventions (CLAUDE.md Laws 1, 3, 9): one bead per worker, then
stop; workers do not run `bd`/`git` (the coordinator creates these
beads from the list below and commits per bead as
`<wave>.<n>: <summary> (bd-xyz)`); scope and acceptance only, no
deadlines, no invented numbers (Law 4 — every threshold below is
either a v1 code constant cited by symbol or the repo-wide
`make check` contract). Bead numbers are dossier-local labels
(`L0-1`…), not `bd` ids; the coordinator maps them onto Wave 1's
numbering (PLAN §6 Wave 1 also contains L1/L2/minimal-L5 beads —
only the L0 slice is split here; cross-layer dependencies are
named where they exist).

Shared acceptance block (applies to **every** bead below; per-bead
acceptance lists only what is *additional*):

- Red-green evidence in the bead report (Law 9): the failing run
  of the new/changed test *before* the fix (against the stub tree
  or verbatim-harvested code) and the passing run after, pasted
  outputs, not summaries.
- `make check` green in the v2 tree (ASan+UBSan lane, release
  Law-2+G1 lane as far as landed suites reach, structural lints:
  `check_dag.py`, `check_no_c_includes.py` == 0,
  `check_isolation.py`, `--whole-archive` L0 isolated link of
  `libcore.a`).
- Builds warning-free under `-std=c11 -Wall -Wextra -Werror
  -Wshadow -Wconversion -Wswitch-enum`.
- No `#include "*.c"` anywhere in `tests/`/`fuzz/`; syscall/fault
  injection only via `-Wl,--wrap` or documented env hooks.
- No Windows file, `_WIN32` branch, or `mk/win.mk` reference lands.
- No v1 app-identity literal lands in `src/` (isolation checker);
  any test that needs runtime/config/data dirs uses per-test
  temp dirs (`test_ipc_isolate` pattern), never the developer's
  real `XDG_*` and never v1's profile (CLAUDE.md: v1 is the daily
  editor; oracle runs use disposable `XDG_DATA_HOME`,
  `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`).
- `docs/decisions/<bead>.md` records every design choice deviating
  from verbatim harvest; `docs/worklog/` + `HANDOFF.md` updated by
  whoever closes the session (Law 6).
- `STATUS.md` in the module directory if the bead ends incomplete
  (Law 3), naming what is missing; tree builds regardless.
- Power state checked before any bench number is recorded
  (`/sys/class/power_supply/BAT0/status`, stamped [bat]/[AC] +
  load, Law 1); bench numbers are records, never gates (the sole
  enforced guard remains G1 + the live malloc guard).

Ordering rationale in one paragraph: `os` is the DAG root and
unblocks everything; `base` next (only needs `os`); `trace` after
`os` (technically parallel-safe with `base` — disjoint files, no
include between them — but sequence them anyway under
one-bead-per-worker to keep `make check` attribution clean);
`work` after `base`+`trace` (hard dependency both ways: includes
both headers, and §3.5's release call lands in `worker_main`);
`ipc` last (needs `os`, needs `base` under the §0.1 amendment,
needs `ipc_isolation` fixture discipline from Wave 0). The
fault-lease bead (L0-2) is split from raw `os` harvest because it
is new design with signal-handler constraints, not porting — the
coordinator may merge L0-1+L0-2 for one strong worker, but the
red ABA test must then still land *before* the lease code in the
same bead's commit sequence (Law 9 evidence order preserved in
the report).

### L0-1 — Harvest `os` (POSIX syscall boundary + app-dir namespace)

- **Scope (files):** v2 `src/os/os.h`, `src/os/os_posix.c` (or
  `os.c`, replacing the `os_app_id` smoke stub in v2
  `src/os/os.c`/`os.h`); v2 `tests/os_test.c` (POSIX leg);
  `docs/decisions/<bead>.md`. From v1 at pin: `src/os/os.h`,
  `src/os/os_posix.c`, `tests/os_test.c`. Explicitly not:
  `os_win.c`, `utf8.manifest`, `utf8.rc`, `tests/win/*`.
- **Work:** verbatim POSIX harvest; `_WIN32`-branch narrowing per
  §1.4 (choice recorded); app-id literals in `os_app_dir` rebuilt
  from `EDIT_APP_ID` (§1.5(a)); THP block in `os_vm_reserve` and
  `os_host_name` kept (§6 items 1, 3); header comments' v1 paths
  updated; fault service harvested **unchanged** in this bead
  (lease is L0-2).
- **Dependencies:** none within Wave 1 (needs only Wave 0's build
  skeleton + `EDIT_APP_ID` plumbing, both landed). Unblocks
  L0-2…L0-6 and all of L1.
- **Acceptance (additional):** `os_test` green in ASan+UBSan lane
  (temp-dir confined; self-spawned child roles incl. the
  unregistered-fault-dies case working via `os_proc_spawn`);
  new namespace test (§1.5(a)) red on verbatim code / green after;
  grep audit shown in report: zero `sublimite` identity literals
  in `src/os/` source (comments updated), `check_isolation.py`
  green; fuzz position resolved per §1.6 (minimal
  `fuzz/os_fuzz.c` **or** recorded Law-1 exemption reasoning —
  coordinator picks at bead creation; silence is not an option);
  `os_fault_*` behaviour cases from v1 `os_test.c` all present
  (enumerate in report).

### L0-2 — Pinned `os_fault` lease (PLAN §4.1)

- **Scope (files):** v2 `src/os/os.h` (lease declarations),
  `src/os/os_posix.c` (`fault_slot`/`os_fault_service` +
  `os_fault_register`/`os_fault_epoch`/`os_fault_retire` region
  only), v2 `tests/os_test.c` (new lease cases),
  `docs/decisions/<bead>.md` (API shape + signal-safety argument
  + what L1 `piece_snapshot` will call).
- **Work:** implement `(slot, gen)` generation + pin/unpin/poll
  per §1.5(b) constraints (lock-free-checked atomics in
  `fault_install`; epoch-increment-before-zero-pages ordering in
  `fault_recover` preserved); keep or supersede
  `os_fault_epoch` plain reads with the decision recorded for
  L2 `file`'s future harvest.
- **Dependencies:** L0-1. Unblocks L1 `piece` (fault-pin hook,
  PLAN §2/§4.1) — the L1 dossier/bead must cite the final symbol
  names from this bead's decision note, not PLAN's provisional
  `os_fault_pin(slot, gen)` spelling if it changed.
- **Acceptance (additional):** red ABA spec first (§1.5(b)):
  register/pin/retire/reregister-same-slot test demonstrating the
  stale-pin hazard against L0-1 code (as a failing/expected-fail
  run pasted in the report), green after; concurrent-first-
  registration barrier case still green; recovery/split/capacity
  (`OS_FAULT_SLOTS` → `OS_E_NOSPC`) cases still green;
  `make check-tsan` clean-or-annotated-with-rationale for the
  multithreaded os cases (PLAN §5 Wave-1 signal-handler budget);
  no allocation added to any fault-path entry (guard-counted
  window around register/epoch/retire in a test, release build,
  guard proven active).

### L0-3 — Harvest `base` (arena/pool/malloc-guard + CPU-detect fix)

- **Scope (files):** v2 `src/base/base.h`, `src/base/base.c`,
  `src/base/malloc_guard.c`; v2 `tests/base_test.c`,
  **new** v2 `fuzz/base_fuzz.c` (+ minimal committed seeds if
  the harness wants files), v2 `bench/base_bench.c`;
  `docs/decisions/<bead>.md`. From v1: `src/base/{base.h,base.c,
  malloc_guard.c}`, `tests/base_test.c`, `bench/base_bench.c`.
  Not: `malloc_guard_win.c`.
- **Work:** verbatim harvest; `_WIN32` commit blocks in
  `edit_arena_alloc`/`edit_pool_get` resolved per §2.4 (recorded
  choice); CPU detect fixed per §2.5 (`os_once` preferred) with
  the once-state added to the globals record; §0.2 rename
  (`edit_*`→`base_*`, incl. guards/macros) **iff** the coordinator
  approved it before bead start — otherwise record the waiver;
  `edit_version` value made v2-appropriate; arena single-owner
  sentence added to the header.
- **Dependencies:** L0-1 (`os_vm_*`, `os_page_size`; `os_once` if
  that fix shape is chosen). Unblocks L0-5, L0-6, and every later
  arena/pool consumer; also unblocks the *full* `os_test` green
  run (`os_test.c` links `base/base.h`, §1.6 — coordinator notes
  which bead first shows the combined green).
- **Acceptance (additional):** `base_test` green (san lane);
  barrier CPU-detect case red under TSan on verbatim code /
  green+TSan-clean after (§2.5); guard-proves-active case on the
  **release** build (`edit_malloc_guard_active()` true and a
  deliberate allocation inside the window counted > 0, then the
  Law-2 window over arena/pool ops counts == 0 — fail-closed
  shape per CLAUDE.md, not v1's skip-if-inert);
  `test_guard_exempt` per-thread semantics preserved;
  `base_fuzz` runs in `fuzz-smoke` (model-checked per §2.6);
  `base_bench` prints p50/p99, exit 0, numbers (M)-stamped in
  the report.

### L0-4 — Harvest `trace` (rings + dump format + ring release)

- **Scope (files):** v2 `src/trace/trace.h`,
  `src/trace/trace_fmt.h`, `src/trace/trace.c`,
  `src/trace/trace_load.c`, `src/trace/replay.c`; v2
  `tests/trace_test.c`, `tests/trace_pairs_test.c` (classified
  per §3.6), `fuzz/trace_fuzz.c` (+ minimal seeds from the
  malformed-case list), `bench/trace_bench.c`;
  `docs/decisions/<bead>.md` (two-header exception §0.1, release
  semantics + dump enumeration after reuse §3.5).
  Test-harness: `tests/core_sync.h` if `work_test`/`trace_test`
  need it (coordinate with L0-5 so it lands once).
- **Work:** verbatim harvest (format byte-stable — `EDTRACE1`,
  `INPT`, event IDs append-only); include spelling normalised to
  `"trace/…"`; ring release designed + implemented per §3.5
  (release-ordered free, recycled-ring dump semantics, loud
  exhaustion); `trace_record_at` legacy shim kept with comment.
  `worker_main` call-site update is **not** in this bead (L0-5).
- **Dependencies:** L0-1 only (`os_now_ns`/`os_sleep_ns`).
  File-disjoint from L0-3 (parallel-safe if the coordinator
  staffs two workers). Unblocks L0-5's touch (i) and all G1
  instrumentation in later waves.
- **Acceptance (additional):** `trace_test` green incl.
  `test_legacy_v1_loads` against a v1-produced dump fixture
  (oracle-generated under Wave-0 isolation, committed as a
  fixture with provenance noted — format-compat is the contract);
  exhaustion red spec (joined-thread registrations past
  `TRACE_MAX_THREADS` drop records on verbatim code) → release
  cases green (sequential register/record/release beyond cap,
  all records dumpable); TSan-clean claim/recycle/dump-after-join
  case (§3.5(ii)); `test_no_malloc` still proves allocation-free
  record path; `trace_fuzz` in `fuzz-smoke`; `trace_bench`
  before/after if `ring_put` changed at all (expectation:
  unchanged — say so).

### L0-5 — Harvest `work` verbatim (pool, mailboxes, foreground lane)

- **Scope (files):** v2 `src/work/work.h`, `src/work/work.c`
  (+ §4.5 manifest touches only), `src/work/work_posix.c` only
  if §4.5(iii) decides keep; v2 `tests/work_test.c` (POSIX leg),
  `fuzz/work_fuzz.c` (+ committed minimal seeds / self-seeding
  note §4.6), `bench/work_bench.c`, shared `bench/harness.h`
  + `tests/core_sync.h` if not already landed by L0-4;
  `docs/decisions/<bead>.md` (diff-from-verbatim manifest +
  non-FSM class-dispatch note §4.2 + foreground-gap pointer).
  Explicitly not: `tests/work_foreground_test.c` (L1/L4
  real-client suite, §4.6), `tests/win/*`.
- **Work:** byte-verbatim port; apply *only* §4.5 (i) trace
  register-result check + release at `worker_main` exit, (ii)
  rename tokens if L0-3 renamed, (iii) eventfd-shim decision
  (recommended drop + test migration to
  `os_wake_fd(work_pool_wake(p))`). Report contains the actual
  `diff` against v1 `src/work/work.c` at `6fec2e8` (or its
  summary by hunk) proving nothing else moved — including no
  capacity or memory-ordering change.
- **Dependencies:** L0-1, L0-3, L0-4 (hard). Unblocks L1
  lineidx/find workers, L2 savectl/journal threading suites,
  PLAN Wave-1 `make check-tsan` gate's first member.
- **Acceptance (additional):** `work_test` green (san lane) with
  foreground cases enumerated in the report (init_foreground /
  prioritize / continue / batch / selective-receive / bind
  coverage as present); `make check-tsan` green on `work`
  (Wave 1 exit-gate member, PLAN §6; any annotation carries its
  rationale); release-build Law-2 counting case present
  (submit/cancel/drain/reuse window, guard active && count == 0,
  allocation-free fixtures — §4.6 citing v1
  `docs/reviews/work-scan-1.md`); pool-lifetime-cycling trace
  assertion lands here or in L0-4 (report cross-references which);
  `work_fuzz` in `fuzz-smoke` (two-raster-worker handoff form);
  `work_bench` (M)-stamped, exit 0; named gap filed for
  real-client foreground tests' return (bead reference in the
  decision note, coordinator opens it against L1).

### L0-6 — Harvest `ipc` (args/wire/server + socket namespace)

- **Scope (files):** v2 `src/ipc/ipc.h`, `src/ipc/args.c`,
  `src/ipc/wire.c`, `src/ipc/result.c`, `src/ipc/ipc_posix.c`;
  v2 `tests/ipc_test.c` (**pure-L0 population only**, §5.6 split
  enumerated in the report), `tests/ipc_isolation.h`,
  `fuzz/ipc_fuzz.c`, `bench/ipc_bench.c`;
  `docs/decisions/<bead>.md` (FSM-rule reading §5.2, leaf/lock
  shapes §5.5, split manifest). Not: `ipc_win.c`, `crt_win.h`,
  main-harness ipc cases (handed to the Wave-1 minimal-L5 bead
  as subprocess tests — coordinator opens/extends that bead; this
  bead lists exactly which cases moved).
- **Work:** verbatim POSIX harvest; `_WIN32` members/legs removed
  per §5.4; namespace literals in `resolve`/`resolve_dir` rebuilt
  from `EDIT_APP_ID` per §5.5 (leaf, abstract name, lock-leaf
  derivation kept consistent); activation carrying
  (`ipc_args_set_activation`, wire trailing block, plain resend)
  kept per §6 item 2; env hooks (`EDIT_IPC_NAMESPACE` etc.)
  unchanged; `edit-jmn` file-blocker cited as inherited L2
  business, unfixed.
- **Dependencies:** **coordinator decision §0.1 first** (DAG
  amendment `base < ipc` in `tools/check_dag.py` + PLAN §2, or
  the cut-the-edge rewrite — bead text must state which world it
  lives in); then L0-1 + L0-3 (arena type in `ipc.h`). Needs no
  trace/work. Unblocks Wave-1 minimal L5 launch/handoff and
  Wave 0/4 contract launches under isolation.
- **Acceptance (additional):** namespace red→green (§5.5):
  leaf/abstract names carry the v2 id, v1-named decoy socket in
  the same temp runtime dir untouched by init/teardown, no
  connection possible at v1 names; `check_isolation.py` green
  with `src/ipc/` present; length-bound cases (oversized
  namespace/display → `IPC_LIMIT`/`IPC_INVALID`) green at the new
  name lengths with buffer derivations stated (§5.5);
  credential/fallback case (`EDIT_IPC_TEST_PEER_UID`) green;
  activation-block round-trip + plain-resend case green (L0 half
  of `t_activation`); wire golden-bytes case: encode of a
  token-less request byte-identical to the pre-activation format
  (fixture derived from the `ipc.h` contract, provenance noted);
  `ipc_fuzz` in `fuzz-smoke` (+ whether `ipc_parse_args` joined
  its surface, §5.6); `ipc_bench` green incl. its rx-pool-size
  assertion (§5.6); full case census of v1 `ipc_test.c`
  (keep/move list) in the report with zero unaccounted cases;
  close-out check: with L0-6 landed, the L0 `--whole-archive`
  isolated link covers all five modules and `make check` shows
  it (this is L0's completion evidence — no separate integration
  bead, by design).

### Out-of-scope / handed-off items the coordinator should track

(Report-don't-fix, Law 3/Law 8 — listed so they are beads or
backlog entries, not folklore.)

- **`edit-jmn` existing-file `EMFILE`** (`src/ipc/STATUS.md`):
  root cause in v1 `src/file` watcher allocation → L2 `file`
  bead's known-issue list (trial-use data-path adjacent; Law 8
  says its own bead if it reproduces in v2 trial use).
- **Real-client foreground tests** (`work_foreground_test.c`):
  return with L1 `lineidx` (and L4 findui) beads.
- **Main-harness ipc cases**: return as subprocess contract
  tests against the Wave 1 minimal L5 binary (Wave 0 contract
  suite may absorb some sooner — coordinator's call).
- **`tools/tracedump`, `tools/replay`**: tools beads (each new
  tool registered in `tools/README.md`, CLAUDE.md); L0 only
  guarantees the formats they consume.
- **`os_slice_ns` Windows rationale, `os_file_set_sparse`,
  `os_map_granularity`**: kept seams (§1.4); if a future Windows
  port is ever seriously proposed, its bead starts from v1
  `os_win.c` at the pin, not from resurrecting v2-deleted
  branches.
- **PLAN §2 text errata for the coordinator's next PLAN touch**:
  (a) add `base < ipc` if L0-6's decision so goes; (b) PLAN's
  "slabs" → pools (§2 preamble); (c) `work` row could name the
  foreground lane (§4.3) so a future reader does not "simplify"
  it away. Docs-only changes need `make all`, not the suites
  (Law 1).

VERDICT: L0 is harvest-ready at pin 6fec2e8 with one structural blocker requiring a coordinator decision before the ipc bead opens (ipc→base edge vs the PLAN §2/check_dag partial order, §0.1 — recommended: amend the order to base < ipc), one naming decision before the base bead opens (edit_* → base_* rename or recorded waiver, §0.2), two genuinely new mechanisms to build red-first rather than port (pinned os_fault lease, §1.5(b); trace ring release with release ordering, §3.5), one racy-cache fix (CPU detect, §2.5), two mechanical namespace conversions (os_app_dir §1.5(a), ipc endpoints §5.5 — both forced through EDIT_APP_ID by check_isolation), and work as a near-byte-verbatim port (§4.5 manifest) — six beads (L0-1…L0-6), dependencies os → {fault-lease, base, trace} → work, ipc after its DAG decision + base, with post-c95b6eb fixes THP/os_host_name (§6.1/§6.3) and IPC activation (§6.2) kept by harvesting at the pin and never from floating main.