# v2-0fv.21 · OS export gate and deletions

`make check-lints` runs `check-exports`. The gate scans global definitions and
undefined references in every release archive in `mk/layers.mk` order and
also reads references from the loose release `src/main.o`. A definition
passes when an object from a different module references it or the
reservation list names its filed brief or bead. The script rejects duplicate
reservations, missing brief or decision files, and reservations for symbols
with no archive definition. References confined to the defining module and
references from tests, fuzzers, and benches do not count.

This bead enforces export usage for `os`. The same scan reports unused
exports elsewhere, including the vendored tree-sitter pseudo-module. Omitting
`--module os` enforces the rule for every archive module. The released
allocator interposers and vendor symbols are included in the scan.

## Deleted exports

The baseline scan found no cross-module release reference for any of the
entries below. The historical OS harvest describes APIs that were carried
forward. Reservations cite the filed work that consumes the export.

| Export | Why it had no eligible caller |
|---|---|
| `os_dir_fd`, `os_file_fd` | Used by fixtures to observe descriptor state or set up native calls. The shipped handle-taking APIs own descriptor access. |
| `os_dir_iter_open`, `os_dir_next`, `os_dir_rewind`, `os_dir_iter_close` | Used by OS listing tests. A23 cancels the startup retention scan. |
| `os_dir_open_at` | OS conformance use. The landed IPC directory opens use `os_dir_open`. |
| `os_dir_try_lock`, `os_dir_unlock` | OS directory-lock tests exercised the journal retirement lock seam. A23 cancels retirement. The landed IPC lease uses file locks. |
| `os_err_name`, `os_native_error` | Test diagnostics and checks. Fixtures retain enum and native error reporting using the enum value and `errno`. |
| `os_fd_limit_raise` | The OS conformance child was its only caller. The release entry point makes no such adjustment. |
| `os_file_clone` | The OS conformance test was its only caller. A23 rule 1 cancels replaced-file retention. The FICLONE implementation, its test and the OS `ioctl` grant went with it. |
| `os_file_dup` | Used by OS duplicate and lease handoff tests. The landed IPC lease owns its original file handle. |
| `os_file_open` | Tests used the path wrapper. The landed release opens use `os_file_open_at` against pinned directories. |
| `os_file_set_mode` | Used for OS mode checks and read-only rename fixtures. The filed metadata-copy seam covers ownership and extended attributes. |
| `os_file_to_stdio` | Only its conformance test used it. The manual journal replay command is deferred in the L2 dossier. |
| `os_host_name` | The host lookup was exercised by the OS test. Landed DND takes the local hostname as an input. |
| `os_map_granularity` | An alias used inside `os_map_file` and in its tests. Mapping alignment uses `os_page_size` directly. |
| `os_map_prefetch` | Only the OS mapping test called it. The filed mapped-open path does not require prefetch. |
| `os_mkdir`, `os_mkdir_at`, `os_rmdir` | Directory fixtures used these convenience calls. The landed directory users open existing directories. |
| `os_random` | Used by the OS random conformance case. |
| `os_rename`, `os_unlink` | The path wrappers appeared in fixtures. The persistence save protocol uses `os_rename_at`, and IPC uses `os_unlink_at`. |
| `os_rmutex_init`, `os_rmutex_destroy`, `os_rmutex_lock`, `os_rmutex_unlock` | Used only by OS conformance and Law-2 tests. The landed piece object does not reference them. |
| `os_slice_ns`, `os_thread_cpu_ns`, `os_thread_cpu_resolution_ns` | The clock conformance and allocation tests were the remaining users of the clock family. The proposed lineidx consumer was superseded by A19. |
| `os_temp_dir` | Only the well-known-directory test called this lookup. |
| `os_vm_commit`, `os_vm_decommit` | The OS mapping test exercised both calls. Linux base allocates from the read/write reservation. The released reserve retains page-granular lazy backing. |
| `os_wall_ns` | Used by OS clock and allocation tests. Released timing uses the monotonic clock. |
| `os_yield` | Tests, including the allocation probe, and the work bench used it. Their scheduling loops use the native yield. The file brief specifies a two-phase close. |

Surviving conformance cases retain coverage for directory-relative open and
rename, descriptor flags, anonymous file creation, file locks, mapped views
and reservation. Fixture-only filesystem setup and cleanup use native
calls where needed. The symbol grant fixture tests two independent
`abort` grants after the OS `fdopen` grant was removed.

## Reservations

The list contains 22 entries (M, one for each baseline diagnostic with a
filed future use). The [reservation file](../../tools/export-reservations.txt)
gives the individual reasons.

| Filed work | Reserved exports |
|---|---|
| [L2-1-file](../briefs/L2-1-file.md) | `os_dir_sync`, `os_fault_register`, `os_fault_retire`, `os_file_copy_metadata`, `os_file_open_anon`, `os_file_sync`, `os_map_file`, `os_phys_mem_bytes`, `os_rename_at`, `os_unmap`, `os_watch_add`, `os_watch_close`, `os_watch_fd`, `os_watch_init`, `os_watch_read`, `os_watch_remove` |
| [L2-2b-journal-runtime](../briefs/L2-2b-journal-runtime.md) | `os_app_dir`, `os_file_pwrite`, `os_file_pwrite_once`, `os_file_set_size`, `os_file_sync_data` |
| [v2-0fv.13](v2-0fv.13.md), Wave 1 minimal L5 loop | `os_wake_fd` |

The file brief's dossier identifies anonymous reload storage and the
physical-memory-based copy threshold. The journal brief's dossier names the
positioned-write and data-sync wrap points in the safety suite it ports.
The journal's app data root follows the Wave 0 namespace decision.

## Evidence

Evidence files are under `build/v2-0fv.21-evidence/` in this checkout.

- **Red.** `red.log` records `nice make -j2 check-exports` with the original
  OS object and an empty reservation list. The script returned 1 and Make
  returned 2. The 60 diagnostics (M, counted from the log) include
  `os_file_clone`, `os_map_prefetch` and all four `os_rmutex_*` exports.
- **Green.** `check.log` records `check_exports: ok (os, 6 archives)` and
  `check_symbols: ok (6 archives plus LDLIBS)`. Dependency, isolation,
  symbol and lane fixtures passed, as did all six isolated layer links.
- **Exports.** `core-before.nm` and `core-after.nm` count global defined
  symbols in `os_posix.o` within `libcore.a`. The count is 128 before and
  90 after (M, 38 removed and 22 of the 60 baseline diagnostics reserved).
  `shipped.log` records zero exact deleted-name matches from `strings -a`
  over each of the six release archives and `build/sublimite-v2`.
- **Imports.** The OS undefined-symbol diff and grant diff agree on
  `clock_getres`, `closedir`, `fchmod`, `fdopen`, `fdopendir`,
  `gethostname`, `getrandom`, `getrlimit`, `ioctl`, `mkdir`, `mkdirat`,
  `pthread_mutexattr_destroy`, `pthread_mutexattr_init`,
  `pthread_mutexattr_settype`, `readdir`, `rewinddir`, `rmdir`,
  `sched_yield`, `setrlimit` and `unlink` (M, 20 OS-scoped grants).
  The OS import of `memchr` disappeared too. IPC, journal and DND still
  use its shared grant. Vendor tree-sitter still uses its own `fdopen` grant.
- **Other modules.** The release scan reports 498 would-fail exports (M,
  sum of its per-module counts): base 13, ipc 20, trace 16, work 19,
  find 21, indent 5, piece 15, scan 2, undo 14, utf8 13, journal 27,
  config 1, dnd 1 and vendor_ts 331.
- **Size.** 342 lines added and 914 removed (M, unified diffs against the
  saved original files, including the new tool and reservation list and
  excluding this decision). Comment-free non-blank lines (M,
  `gcc -fpreprocessed -dD -E -P FILE | grep -c '[^[:space:]]'`,
  recorded in `os-line-counts.log`) went from 1,505 to 1,285 in
  `src/os/os_posix.c` and from 282 to 241 in `src/os/os.h`.
- **Compile.** `nice make -j2 all` returned 0 in `all.log`.

## Acceptance

- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:150-:159` returned 0 in
  `check.log`. ASan/UBSan passed 31 binaries, Law 2 passed seven release
  suites, and the contract harness reported 64 passing outcomes (M,
  the lane summaries).
- `nice make -j2 check-tsan` returned 0 in `check-tsan.log` with
  11 threaded suites passed (M, the lane summary).
- `nice make -j2 fuzz-smoke` returned 0 in `fuzz-smoke.log` after
  15 fuzzers replayed their seed corpora (M, the lane summary).
