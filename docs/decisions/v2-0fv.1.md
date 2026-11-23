# v2-0fv.1 · Linux OS boundary

> Amended by v2-0fv.13. That bead added local streams and `os_waitset`, the one
> wait primitive. It removed `os_wake_wait` and child processes
> (`os_proc_spawn`, `os_proc_kill`, `os_proc_wait`, `os_proc_status`), because
> no module called them. The process rows and the poll-based wake wait below
> describe that earlier state.

Harvested from the read-only v1 oracle at `6fec2e8`. L0 `os` has one public
header, one Linux implementation and zero dependencies on other editor modules.

## Acceptance

- **v1 `59c0621`**. `os_vm_reserve` opts out of transparent huge pages with
  `MADV_NOHUGEPAGE` once per reservation. This preserves page-granular
  `MADV_DONTNEED` decommit under THP.
- **v1 `7c0e9ea`**. `os_host_name` supplies the local host name for file URI
  classification. It reports `OS_E_RANGE` for an undersized output buffer.
- Both XDG roots and both HOME fallbacks append `EDIT_APP_ID`. A relative
  XDG root falls back to HOME. Missing or relative HOME then gives
  `OS_E_NOENT`. Lookup creates no directories. `os_app_id` still serves
  `tests/isolation_test.c`.
- **Law 1 fuzz exemption.** This layer forwards opaque bytes to Linux and
  maps kernel outcomes. It checks flag values, sizes, alignment, the leading
  slash of a path and the absence of slashes in a single component. It
  appends fixed suffixes to environment roots. There is no input grammar,
  file format or editor state mutation here. The conformance suite covers
  the boundary conditions using caller-owned buffers and real syscalls.
- The suite belongs to ASan/UBSan and TSan because it exercises concurrency
  in mutexes, conditions, once, eventfd and positioned I/O. Law 2 guard
  coverage belongs to the base bead once the release guard exists.

## State and ownership

| State | Invariant |
|---|---|
| Build identity and error-name table | Immutable. The accessor and all directory paths use the same build macro. The diagnostic table follows `os_err` ordinal order. |
| Native error | libc holds one `errno` per thread. Syscall failures preserve it and validation errors set it to zero. Cleanup after failed acquisition preserves the original error. |
| `os_thread` | A caller-owned object stays live and unmoved through join. The start adapter reads the callback and argument published before `pthread_create`. The native handle is the join token. |
| `os_mutex`, `os_rmutex`, `os_cond`, `os_once` | These embed the corresponding native objects. The recursive lock's owner balances acquisitions and releases. Condition waiters recheck a predicate under the owning mutex. Once state belongs to the calling module. |
| `os_wake` | Its fd owns one nonblocking eventfd, or is negative when closed. The kernel counter is the pending state. The sole consumer drains before reading the announced work. |
| `os_file` and stdio transfer | A nonnegative fd owns one descriptor. Close or successful stdio transfer spends it. Duplication adds a descriptor to the same open-file description, which owns the cursor and advisory lock. |
| `os_dir` and `os_dir_iter` | A directory fd pins the inode through rename. If an `O_PATH` fd is upgraded for sync or lock, the readable replacement pins the same inode and the old fd is closed. Each iterator owns a separate open-file description and cursor. |
| `os_stat` and `os_identity` | A value captures one native stat result. Identity is device and inode, with the persistence shape's high inode word zero on Linux. Signed timestamps saturate at the representable limits. |
| `os_map` | Address and logical length own one private mapping. Linux rounds the mapped extent to a page. Closing the fd or changing its directory entry leaves the view on the same file object. Unmap resets the handle. |
| Anonymous VM | Caller owns the reservation base and extent. Linux supplies backing lazily. Commit validates alignment, decommit drops the backing for the requested pages, and release relinquishes the reservation. There is no page bitmap. |
| `os_proc` and `os_proc_status` | A positive PID identifies one child owned by a single reaper until wait succeeds. A timeout preserves ownership. Successful wait spends the PID and reports a tagged exit status or terminating signal. |
| Environment, time, resource limits and random bytes | Each call uses current process or kernel state. The module keeps no mutable globals or caches. Environment readers serialize against mutation. Only startup raises the descriptor soft limit. |

## Linux-specific decisions

- File and directory opens share their respective `openat` paths. The
  exclusive-create path already owns a new regular file, so it can publish
  the descriptor immediately. A fallible post-create stat there could
  strand the new name.
- Keep the Linux `O_PATH` path for a search-only directory. Anonymous file
  creation can work there even when opening the directory for reading fails.
  Sync and flock need a readable descriptor. A directory iterator opens its
  own description to prevent shared cursor state.
- Keep atomic `RENAME_NOREPLACE`, filesystem-specific `O_TMPFILE` and
  `FICLONE` error outcomes, and open-file-description flock semantics. The
  absent-target save, anonymous-file fallback and session lease use these
  distinctions.
- Check the signed mapping endpoint before subtracting the length. Compute
  timestamp saturation at the fractional boundaries without overflowing
  signed nanoseconds. A wake wait requires `POLLIN` for success and
  rejects a closed fd before polling. Finite waits work from monotonic
  elapsed time and retain v1's event and process polling behavior.
- Keep `os_map_granularity` for offset alignment and `os_slice_ns` for
  thread CPU deadlines. Each delegates to the native Linux value.
- The symbol gate initially rejected the new object's libc imports.
  `tools/system-symbols.txt` adds the exact imports named by the gate,
  each with its purpose. `nm -D -P -- build/rel/tests/os_test` confirmed
  all 76 additions as `GLIBC_*` versioned references (M, the set difference
  between the gate diagnostics and the existing list). This file is a
  required addition to the initial file list for the symbol gate.

## v1 material left out

### `src/os/os.h`

- All Windows preprocessor layouts and initializers for thread, mutex, recursive mutex, condition, once, wake, file, directory, iterator, map and process objects. Linux embeds only the native members it uses.
- `os_wake_handle`, `os_file_handle` and `os_dir_handle`. They expose Windows handles.
- Windows error mapping and contract notes for UTF-16 conversion, long-path prefixes, clocks, reparse points, ACLs, shares, sparse files, positioned I/O, directory locks, flush, rename, unlink, mappings, memory and process launch. The public comments describe the Linux contract.
- Windows-derived `OS_NAME_MAX` capacity. Linux name storage needs `NAME_MAX + 1` bytes including the NUL.
- `OS_PATH_SEP`. Linux paths use `/` and the v1 production source has no reference to the macro.
- `os_file_set_sparse`. It has no Linux operation or production Linux caller in the pin. Sparse holes use ordinary writes or truncation.
- `OS_FAULT_SLOTS`, `os_fault_register`, `os_fault_epoch`, `os_fault_retire` and `os_fault_test_trigger`, including the v1 service contract and mapping-recovery promises. The separate fault bead owns the generation-validated contract from amendment A5.
- The Windows guard around `os_fd_limit_raise`. The Linux-only header declares it directly.

### `src/os/os_posix.c`

- Whole-translation-unit Windows guard. The build compiles the single Linux body.
- `SLOT_FREE`, `SLOT_RESERVED`, `SLOT_LIVE`, `SLOT_RETIRING`, `SLOT_PHASE`, `SLOT_READER`, `fault_slot`, `fault_service` and its process-global instance. Their storage and slot protocol belong to the superseded fault service.
- `fault_chain`, `fault_recover`, `fault_handler`, `fault_install` and the four public `os_fault_*` bodies. The fault bead owns signal handling and registration.
- `<stdatomic.h>` and `<sys/syscall.h>`. Their production uses were in the fault service and its test trigger.
- `os_file_set_sparse` no-op body. The Linux caller needs no sparse mode transition.
- Separate `finish_open` and parallel path/dirfd open recipes. The shared open paths enforce the file classification and descriptor ownership rules once.
- All three v1 leaf literals. The build macro supplies the identity for both environment branches.
- `map_errno(0)` special case. The default already maps it to `OS_E_IO`.
- Guessed page-size fallback. Linux `getpagesize` directly supplies the native value.
- `MADV_NOHUGEPAGE` preprocessor guard. The supported Linux headers expose the flag and the advice is best effort.
- `nr_open` text read, integer parsing and guessed ceiling. Startup raises to the finite kernel-accepted hard limit. A later privileged reduction of the kernel ceiling can make that call fail, leaving the existing soft limit in effect.
- v1 mapping comments about the recovered anonymous suffix and migration-era references. The current body unmaps the native extent with `munmap` and the public mapping contract covers ordinary views.

### `tests/os_test.c`

- **Base bead**. `case_noalloc`, its sanitizer/guard detection, positive allocation control, cold lock and condition probes, contended lock and waiter helpers, and guarded clocks, identity, yield, wake and positioned I/O loop. Those checks require the live base allocation guard. The OS suite still exercises the syscalls and threading behavior.
- **Fault bead**. `case_fault`, `map_pages`, `all_bytes`, `install_ctx` and `install_body`. This covers concurrent first registration, slot exhaustion and reuse, repeated suffix zero-fill, prefix preservation, epoch increments and remap after recovery.
- **Fault bead**. The registered shrink and past-EOF recovery portions of `case_map` and the `stray-fault` process role. Only the Linux mapping and its still-backed prefix are exercised here.
- `case_reparse` and `OS_TEST_DATA_REPARSE`. They require Windows reparse fixtures.
- Windows maximum-path stress, `utf16_units`, `is_sep`, `root_len` and cross-root `relative_to_cwd`. Linux relative paths are constructed from the known checkout path and tested with symlinks.
- Clone and sparse-mode status logging, the switchable sparse-mode test, the permissions fallback and the file-delete-after-stdio-inheritance scenario. Linux sparse offsets use the real write and map directly. The clone test still accepts the filesystem's explicit `OS_E_NOTSUP` and the inheritance test examines the child fd table.
- The type-only signedness check in `case_errors` that assigned negative values to a test struct. A real `futimens` and `os_file_stat` round trip now verifies negative timestamps.
- Reserved v1 lock-file spelling. A generic dotfile still exercises visibility and preservation across a directory lock.
- Windows clock-resolution sampling, scheduler-tick assumptions and timing prints. The Linux suite checks reported resolution, CPU/slice ordering and the minimum requested sleep.
- `EDIT_ASSERT` and the accumulated failure count. Test assertions terminate at the failure so a bad setup cannot reach reads or writes through invalid state.
- Supervisor re-exec per case, `OS_TEST_CASE` selector and ambient temp-directory selection. Every case gets an independent `build/` fixture and a bounded hang guard. Process tests still self-exec their child roles.

## Red and green

The release namespace test was run before and after changing the three v1
suffixes. At that point it was the only case in `tests/os_test.c`.

```sh
make -s build/rel/tests/os_test && build/rel/tests/os_test
```

Red, exit 1.

```text
os_test: namespace FAIL (got leaf sublimite, expected sublimite-v2)
```

Green, exit 0.

```text
os_test: namespace ok
```

Source count (M, `wc -l src/os/*`).

```text
  326 src/os/os.h
  898 src/os/os_posix.c
 1224 total
```

The completed suite reproduced the regression with a build-local copy that
changes only the three app-directory suffixes. The production sources and
`os_app_id` stayed on the build identity.

```sh
mkdir -p build/os-evidence/mutation
sed -E 's@("/[^"]*") EDIT_APP_ID@\1 "sublimite"@g' src/os/os_posix.c > build/os-evidence/mutation/os_posix.c
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Wswitch-enum -D_GNU_SOURCE -pthread -O2 '-DEDIT_APP_ID="sublimite-v2"' -Isrc tests/os_test.c build/os-evidence/mutation/os_posix.c -o build/os-evidence/mutation/os_test && build/os-evidence/mutation/os_test > build/os-evidence/namespace-red.log 2>&1
make -s build/rel/tests/os_test && build/rel/tests/os_test > build/os-evidence/namespace-green.log 2>&1
```

The build, local checks and threaded lane all exited zero.

```sh
make all > build/os-evidence/all.log 2>&1 && make check > build/os-evidence/check.log 2>&1
make check-tsan > build/os-evidence/check-tsan.log 2>&1
make fuzz-smoke
```

The logs include these gate results.

```text
check-no-c-includes: ok
check_symbols: ok (6 archives plus LDLIBS)
check_isolation: ok (13 files under src/)
isolation fixtures: 23/23 passed
check-layers: 6 isolated layer links passed (L0-L4 tests, L5 via main.o)
check-san: 7 test binaries passed
check-law2: declared empty (first suites due in Wave 1)
check: ok
check-tsan: 1 threaded suites passed
fuzz-smoke: declared empty (first suites due in Wave 1)
```
