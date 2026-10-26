# edit-177r.4 — core modules on os.h

## Inventory before editing

(M) Inspected every `src/<module>/*.c` and `*.h` for the POSIX include families in the brief, then their call families (including `mincore`, `madvise` and `clock_nanosleep`), and read each matching implementation. The raw pre-edit file/line scan is `build/worker-logs/inventory-before.txt`. Source locations below refer to the pre-edit tree. No production code had been edited when this table was written.

| Module | POSIX includes and calls found | os.h mapping / disposition |
|---|---|---|
| base | `base.c:5–6`: sys/mman.h, unistd.h; `mmap` (arena and pool), `munmap`, `sysconf(_SC_PAGESIZE)` | `os_vm_reserve`, `os_vm_commit`, `os_vm_release`, `os_page_size`; page-round arena reservations while retaining logical size. `malloc_guard.c` is glibc-only and will be wrapped whole for Linux. |
| trace | `trace.c:7`, `replay.c:8`: time.h; `clock_gettime(CLOCK_MONOTONIC)`, `clock_nanosleep(TIMER_ABSTIME)` | `os_now_ns`; recompute the remaining interval to the original absolute replay deadline and use `os_sleep_ns`. errno.h in replay becomes unnecessary. |
| work | `work.h:19`: pthread.h; `work.c:6–8`: sys/eventfd.h, time.h, unistd.h; `pthread_mutex_init`, `pthread_mutex_destroy`, `pthread_mutex_lock`, `pthread_mutex_unlock`, `pthread_cond_init`, `pthread_cond_destroy`, `pthread_cond_wait`, `pthread_cond_signal`, `pthread_cond_broadcast`, `pthread_create`, `pthread_join`; eventfd, read, write, close; monotonic clock | `os_mutex`, `os_cond`, `os_thread_start/join`, `os_wake_init/signal/drain/close`, `os_now_ns`. Drain before reading announced mailbox state. Retain Linux `work_pool_eventfd` compatibility in `work_posix.c`; portable access through the wake object. |
| piece | `piece.c:9`: pthread.h; `pthread_mutex_init`, `pthread_mutex_destroy`, `pthread_mutex_lock`, `pthread_mutex_unlock`; `pthread_mutexattr_init`, `pthread_mutexattr_settype`, `pthread_mutexattr_destroy`; `pthread_self` | `pool_mu` → `os_rmutex`; add-store `mu` → `os_mutex`; owner identity → `os_thread_self` in uint64_t. |
| undo | `undo.c:3–5`: sys/mman.h, time.h, unistd.h; madvise(DONTNEED), sysconf(PAGESIZE), monotonic clock | `os_vm_decommit`, recommit before reusing packed records/backup pages, `os_page_size`, `os_now_ns`. Not pure C11. |
| scroll | `index.c:3`, `visual.c:2`, `resident.c:4–6`: time.h, sys/mman.h, unistd.h; monotonic clocks, sysconf(PAGESIZE), mincore | `os_now_ns`, `os_page_size`; **mincore has no frozen entry**. Keep the existing probe behind a Linux guard naming this bead; Windows conservatively uses WORK_BULK pending coordinator decision. |
| lineidx | `lineidx.c:7`: time.h; clock_gettime(THREAD_CPUTIME_ID / MONOTONIC), nanosleep | Slice deadlines/comparisons → `os_slice_ns`; cpu_begin and cancel_cpu_ns → `os_thread_cpu_ns`; monotonic mailbox deadline → `os_now_ns`; waits → `os_sleep_ns`. Worker deadline must begin with the slice clock separately from cpu_begin. |
| layout | `checkpoint.c:4`: time.h; nanosleep on publication backpressure | `os_sleep_ns`; remaining implementation is C11, with no direct OS calls. |
| view | `view.c:5`: time.h; monotonic clock | `os_now_ns`; remaining implementation is C11. |
| utf8 | No matching POSIX includes or calls; utf8.c, utf8.h, tables.h inspected | none needed: pure C11 |
| scan | No matching POSIX includes or calls; scan.c and scan.h inspected (SSE2 intrinsics) | none needed: C11 plus portable-to-this-target SIMD intrinsics |
| find | No matching POSIX includes or calls in its C and header files; literal SIMD and work/file contracts only | none needed: C11 plus SIMD; OS work is already delegated |
| hl | No matching POSIX includes or calls in its C and header files; clock default in parse.c delegates to trace_now_ns; tree-sitter and work/file contracts only | none needed: C11 and tree-sitter API; win-compile needs the existing vendored API include path |

## Binding migration decisions

Apply edit-177r.2 §9 without changing the frozen header or Linux test expectations. Keep WIN_MODULES untouched; win-compile is an object-only target using the existing release pattern rule. Its target-specific include path supplies the vendored tree-sitter API.

## Implementation choices

- VM reservations use the frozen page-multiple API. Arena size remains the caller's logical capacity; only the reservation/release extent is rounded. Windows commits the pages of each returned arena allocation/fresh pool block. Linux keeps the writable MAP_NORESERVE mapping and its existing allocation fast paths. Undo bypasses pool_get for packed storage, so it explicitly commits new/reused record and backup pages before touching them, and decommits unused tails through os.h. Commit failure follows UNDO_ERR_NOMEM and rolls back replay work.
- Work stores an os_wake plus a portable active flag. work_pool_wake exposes the borrowed wake object; work_posix.c retains the old fd accessor for Linux callers awaiting W2. Both drain APIs clear the notification before acquiring mailbox state, and re-signal if bounded consumption leaves pending messages.
- os_thread has no public native-handle accessor. Existing Linux ownership tests and the journal worker-CPU benchmark used work's public pthread array. Workers now publish their os_thread_self identity atomically once at startup; work_pool_worker_id exposes it for those diagnostics. Tests still check the exact bulk worker, without accessing os.h private fields. The Linux-only journal benchmark uses the documented pthread_self identity representation to obtain the same native CPU clock. Linux work fault-injection tests compare native object addresses with wrapper addresses, preserving their injection stages/assertions without reading wrapper fields. Raster fixtures and raster_bench use the new active flag and os_mutex types, including the formerly implicit active zero-fd fixture.
- Windows' C runtime lacks GNU memrchr. lineidx and layout use a local reverse byte search on Windows, retaining memrchr on Linux. This is a C-library portability adjustment, not a new OS operation. piece's add_ptr helper is now compiled only with PIECE_TESTING, matching its sole caller; clang's Windows build otherwise rejects it as unused.
- The new lineidx_os_test compiles lineidx with deterministic, separate clock functions. It checks yielding with a frozen accounting clock, cancellation's CPU delta, and worker slice deadlines starting in the slice clock's domain. The piece conformance suite explicitly destroys an edited tree and then releases its surviving snapshot, exercising node_unref → pool_free under recursive pool_mu.
- WIN_MODULES, os.h, and Linux test expectations remain unchanged. win-compile inherits only the vendored API include path it needs, compiles release objects with the existing warning flags, and never links. all remains the default Windows target.

## Open questions

`mincore` has no os.h equivalent. Should a later contract bead add a residency-query entry, or should Windows retain the conservative bulk lane? This worker leaves the Linux probe guarded with an edit-177r.4 comment, as required by the brief. No speculative Win32 query or os.h change is introduced.

## Coordinator decision (session 22, 2026-10-26)

`mincore` residency probe (scroll's resident-copy foreground optimisation): **Windows takes the conservative bulk lane** (`WORK_BULK`) until a residency query enters `os.h` through its own contract bead. The Linux probe stays as today behind `#if !defined(_WIN32)`. The contract bead is filed as backlog and is pulled only when W2's measurement on NTFS or `\\wsl.localhost`, or a trace profile, shows the foreground lane matters on Windows (Win32 candidate: `QueryWorkingSetEx`). The frozen header is unchanged by this bead.

The worker's `make check` exit 2 was `tests/journal_retire_test.c`'s BAT0 assertion, fixed on main by edit-hltk (889932c) in parallel; the private-Xvfb failures in its continuation run are the same bead. The coordinator re-ran the full check after rebasing onto that main (see the worker report's coordinator section).
