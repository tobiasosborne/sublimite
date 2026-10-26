# edit-pf3l — file module on os.h

## Inventory before implementation

(M) Read the pre-edit file.c and scanned stripped C tokens for every POSIX call family below. Locations are pre-edit source lines; each listed line is one call site (duplicates on a line are retained). Raw scan: `build/worker-logs/pf3l-inventory-before.txt`. This table was written before production edits. C11 allocation, atomics and string routines remain C11.

| POSIX call | Pre-edit lines (M, regex `name(` over comment/string-stripped source) | Replacement |
|---|---|---|
| `sigaction` | 78, 152, 157 | deleted; os_fault_register installs os_fault_service |
| `sigemptyset` | 77 | deleted with file_bus |
| `raise` | 79 | deleted with file_bus |
| `mmap` | 111, 241, 257, 811 | os_map_file; anonymous recovery/test mappings deleted with file_bus |
| `munmap` | 246, 256, 262, 271, 293, 820 | os_unmap; registry self-test replaced by OS conformance coverage |
| `sysconf` | 142, 238, 553, 553 | os_phys_mem_bytes, os_page_size |
| `pthread_once` | 163, 245, 1124, 1230 | watch platform file uses os_once_run; fault installer deleted |
| `pthread_create` | 225 | deleted registry probe; OS tests own fault-service concurrency |
| `pthread_join` | 267 | deleted registry probe |
| `sched_yield` | 193, 226, 230 | deleted registry probe / os_yield |
| `close` | 294, 779, 814, 820, 953, 961, 1384, 1385, 1388, 1593 | os_file_close / os_dir_close |
| `fstat` | 358, 378, 699, 732, 1028, 1483, 1495, 1501, 1578, 1585, 1629, 1666 | os_file_stat |
| `inotify_init1` | 511 | platform file file_posix.c; file_win.c CreateEventW + ReadDirectoryChangesW |
| `inotify_rm_watch` | 525 | platform file file_posix.c; file_win.c CancelIoEx + close directory |
| `nanosleep` | 609, 915 | os_sleep_ns |
| `lstat` | 684, 702 | os_stat_path(OS_STAT_NOFOLLOW) |
| `realpath` | 720 | os_path_canonical |
| `open` | 724, 1520 | os_file_open / os_dir_open |
| `pread` | 754, 835 | os_file_pread |
| `dup` | 809 | os_file_dup |
| `madvise` | 827 | os_map_prefetch |
| `inotify_add_watch` | 1127, 1232 | platform file file_posix.c; file_win.c ReadDirectoryChangesW(parent) |
| `read` | 1176 | platform watch read in file_posix.c; file_win.c GetOverlappedResult |
| `write` | 1359 | os_file_pwrite_once (explicit temp offset; cancellation between EINTR attempts) |
| `unlinkat` | 1387 | os_unlink_at |
| `flistxattr` | 1399, 1404, 1428 | platform file file_posix.c; file_win.c GetSecurityInfo / attributes |
| `fremovexattr` | 1438 | platform file file_posix.c; file_win.c SetSecurityInfo / attributes |
| `fgetxattr` | 1445, 1449, 1457 | platform file file_posix.c; file_win.c GetSecurityInfo |
| `fsetxattr` | 1451 | platform file file_posix.c; file_win.c SetSecurityInfo |
| `fstatat` | 1475, 1571 | os_stat_at(OS_STAT_NOFOLLOW) |
| `openat` | 1481, 1528 | os_file_open_at; save WRITE|CREATE|EXCL |
| `fchown` | 1497 | platform file file_posix.c; file_win.c security descriptor copy |
| `fchmod` | 1506 | os_file_set_mode |
| `clock_gettime` | 1524 | os_wall_ns |
| `getpid` | 1526 | os_proc_id |
| `fsync` | 1555, 1596 | os_file_sync(write temporary), os_dir_sync |
| `renameat` | 1581 | os_rename_at(OS_RENAME_REPLACE) |
| `pthread_mutex_lock` | 1566 | os_mutex_lock |
| `pthread_mutex_unlock` | 1592, 1604 | os_mutex_unlock |

## Binding choices

Apply edit-177r.2 §9 in the prescribed order. os.h stays frozen. Preserve native errors with os_native_error immediately at failure. `OS_E_ISDIR` is NOTREG; `OS_E_RANGE` after acquisition validation is CHANGED. The acquisition regression uses a per-open test hook, populated before submission, with no process global.

The fault installer and registry leave file.c entirely when mappings register with os_fault_register. Existing backing/snapshot queries read os_fault_epoch; retirement precedes os_unmap. Linux shared inotify registry remains the sanctioned UI-owned file_watches global in file_posix.c: sharing one descriptor and parent watches is existing editor/config behavior, and os.h deliberately has no notification abstraction. Windows provides its twin in file_win.c.

External identity callers found: materialize_base in src/editor/recovery.c, before/after descriptor stats. No journal caller of file_id_from_stat exists in this tree. Journal and recovery compare file_id.type with S_IFREG: preserve the existing file_id type encoding with named FILE_TYPE constants while consuming os_kind internally, so Linux callers retain their behavior.

The implementation choices and verification context follow.

## Final implementation choices

- `file_id_from_stat` consumes `os_stat` directly. Signed timestamps are cast to the original unsigned encoding. `file_id` retains the upper file-index word in `ino_hi`, and comparisons include it, preserving Windows file identity without a hash. Regular/directory/link type constants retain the Linux `S_IF*` encoding; other native kinds are rejected during acquisition. `os.h` is unchanged.
- The complete repo search also found callers outside the brief's editor/journal search: config's before/after settings identities, savectl's reload identities, their tests, and the savectl fuzzer. Only their identity boundaries change. Existing native descriptors are represented temporarily with `os_file_from_fd`; their existing owners still close them. The fuzzer retains its native stat for its timestamp-restoration oracle, and obtains a separate `os_stat` for identity conversion.
- Every original mapping retains its `os_map`, duplicated `os_file` and fault slot. `os_fault_retire` precedes `os_unmap`; snapshot/backing/source leases retain their existing refcounts. `file_test_recover` now delivers `os_fault_test_trigger` to a retained byte instead of invoking a second recovery algorithm. No file-owned signal installer remains.
- Public registry smoke checks use fresh views when reusing a slot: Windows epochs belong to the mapped view's owner, so retiring and re-registering an already recovered view need not reset that view's epoch. The old private RESERVED-slot and pinned-retirement regressions are preserved against the actual OS implementation in `tests/file_fault_test.c`. It includes the Linux implementation in its test translation unit, interposes only the recovery remap, and checks publication, blocked retirement, nonreuse while pinned, and foreign-page preservation. Production OS sources have no new hooks. Windows already has `os_win_test`'s `retire_admitted` white-box case.
- The acquisition hook is per file, initialized before worker submission, and runs after identity validation immediately before mapping. A deterministic shrink yields `FILE_ERR_CHANGED`: Linux admits the map and rejects the changed identity on postvalidation; Windows rejects the map with `OS_E_RANGE`, which file translates. Existing truncation tests independently exercise real Linux SIGBUS recovery through the OS service.
- Save temporaries remain write-only, exclusive creations relative to a pinned parent. `os_file_sync` operates on that write handle; `os_dir_sync` handles the directory barrier. There is no read-only file sync to reopen or drop. Windows directory sync retains the frozen contract's documented no-op.
- Save writes use `os_file_pwrite_once` with the temporary's explicit byte offset. The initial migration to `os_file_write` failed the existing interrupted-write cancellation assertion because its internal retries hide the cancellation boundary. The single-attempt API restores the original stop check between interrupted attempts; no sequential-position consumer exists on the temporary. The syscall oracle now covers both write and pwrite without changing its assertions.
- Native OS errors are captured immediately through `os_native_error`. Module-generated path-length, size and zero-write errors are supplied by a small platform-private helper: Linux keeps its existing errno values; Windows gets Win32 codes. The overlong-request Windows regression caught the initial errno-only branch.
- Linux inotify and metadata code moved whole into `file_posix.c`, retaining the shared descriptor, parent/name filtering, bounded dispatch, xattr removal/copy, owner/group copy and final chmod order. The common file state lives in a private header so platform watch implementations retain the existing ownership model.
- Windows shares one manual-reset completion event across overlapped parent-directory requests. Dispatch clears the event before checking individual request completion, filters names case-insensitively, coalesces file checks through the existing worker/mailbox path, marks path-only notices, and rearms each request. The last member cancels and retires its parent's request. `file_watch_start` returns a nonnegative service token; `file_watch_handle` exposes the borrowed event to the future Windows editor loop.
- Windows save metadata copies owner/group/DACL and its protection state, then writable DOS attributes; mode application remains last. A separate handle reopened on the actual temporary requests READ_CONTROL, WRITE_DAC, WRITE_OWNER and FILE_WRITE_ATTRIBUTES. The data handle remains write-only. READ_CONTROL is necessary for the SetSecurityInfo operation used here: the native red run returned ERROR_ACCESS_DENIED without it. Tests distinguish a protected source DACL and preserve hidden/read-only attributes. SACL audit policy, alternate streams, compression and encryption preservation are not claimed by this implementation.
- `mk/win.mk` adds file and the dependency closure needed by this worktree's OS-only link list, the portable file executable, advapi32, and included-case dependencies. When integrating W0.5, retain its wider module/test lists and add file to them; do not replace them with this worktree's smaller list.

## Verification and integration

The worker report contains commands, red/green evidence, final exit codes and measured bench results. All artifacts are in this worktree's build directory. Linux uses DISPLAY=:97 with the display lock and LeakSanitizer enabled. Interop and LeakSanitizer require the authorized sandbox escalation on this machine.

Windows rebuilds were verified from fresh build directories after an observed stale executable image through WSL. The shared reparse-fixture script can also fail cleanup if another Windows check has removed its fixed-name scratch directory; that infrastructure is outside this bead and is reported rather than modified.

No OS contract change or blocking design decision is required. The coordinator must retain W0.5's broader make lists when combining the workers and carry the remaining editor/journal native interfaces into their own port beads.

The symbol checker also ignores COFF's `.bss` and `.data` section symbols, which GNU nm lists in the same columns as variables. Its allowed C-global names remain unchanged by that format adjustment. Linux object checks still see no file-core mutable state; Windows watch and OS objects pass the same checker.

For the final Windows verification, a build-only native helper creates a private `sublimite-test-pf3l-fixture-*` directory. The check inherits it as TEMP through WSLENV, using native backslashes for cmd.exe. This isolates the existing reparse script without editing it or another worker's state; a native helper removes the empty private root afterwards.

Final acceptance (M) [AC, desktop Spiral]: Linux release make all exit 0; full DISPLAY=:97 make check exit 0, with 105 test binaries plus shell contracts; final isolated Windows make check exit 0, with its release/sanitizer loop and ASan positive control. Counts are emitted suite/make enumerations, and exit codes are captured shell statuses. The report contains the evidence and the final nm audit.
