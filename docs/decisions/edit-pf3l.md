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

## Round 2: review findings → change → test

Round-1 Windows security and notification choices above are superseded by this table. The OS contract remains frozen. **Security signoff is partial: the audit-SACL part of finding 8 remains unresolved.** The worker report is `docs/worker-reports/edit-pf3l-s12-r2.md`.

| # | Finding | Change | Test |
|---|---|---|---|
| 1 | Permissive temporary before metadata copy | Native exclusive creation relative to the retained directory, with a protected owner-only descriptor. Restore and read back supported source security before the first write; cleanup any failed preflight. | `security`: inspect the creation descriptor, snapshot temporary permissions at TEMP_CREATED, attempt read/write under a restricted token; native multichunk crash artifacts retain restrictive access. Round-1 red: inherited world access and successful restricted-token open. |
| 2 | EFS becomes plaintext | Refuse an encrypted source with `ERROR_FILE_ENCRYPTED` before temporary creation. Encryption preservation needs a separate implementation; successful plaintext replacement is never the fallback. | `efs`: real EncryptFile source in a plain directory, deterministic attribute injection, original bytes/encrypted flag retained, child refuses before TEMP_CREATED. Round-1 red: successful plaintext replacement. |
| 3 | Named streams lost | Enumerate `FileStreamInfo` through the captured source handle. Any non-default stream, failed enumeration or insufficient enumeration buffer refuses the save before creation. | `streams`: independent `first` and `Zone.Identifier` streams, refusal, unchanged original and stream bytes; `failures`: enumeration error and cleanup. Round-1 red: streams disappear. |
| 4 | Identity sampled before writer close | Retain an independent Windows attribute handle to the replacement object; close the data writer before definitive stat and replacement publication. Linux retains a duplicate for the same sequencing. | `identity`: delayed-close stat fixture and callback ordering; shared COPY/MMAP repeated-save and immediate own-save checks. Round-1 red: early callback and stale identity. |
| 5 | Shared OVERLAPPED event loses wakes | Each armed directory owns an event and one-shot registered wait. Callbacks signal only the aggregate UI wake; retire waits before reset/rearm/free. | `wake`: adding a second directory after a completed first request, distinct events, completion of the first while the second rearms. Round-1 red: aliased events and missing aggregate wake. |
| 6 | Unprotected DACL re-inherits new parent | Restore captured DACL/control with `NtSetSecurityObject`, without requesting UNPROTECTED re-inheritance; read back owner/group, DACL bytes/protection and label. Refuse mismatches. | `descriptor`: native move between parents with different inheritable world permissions; exact unprotected ACL/control equality after save. Round-1 red: re-inheritance request and different ACL. |
| 7 | Unconditional WRITE_OWNER | Compare owner, group and label first; reopen the temporary with WRITE_OWNER only for an actual owner/group/label change. The initial data handle needs no ownership-changing access. | `rights`: actual Modify-only source denies a direct WRITE_OWNER reopen, save succeeds while the shim rejects that right; `failures`: unassignable captured source owner refuses before writing. Round-1 red: access denied on ordinary save. |
| 8 | Mandatory label and other security state omitted | Preserve LABEL with owner/group/DACL and verify it before writing. Query readable resource/scope/trust/filter security state and refuse unsupported non-label ACEs. **Explicit audit SACLs are not yet preserved or reliably detected/refused; this remains a blocker.** | `label`: explicit low label under a different default parent, exact retained label, injected query and label-set refusals. Round-1 red: label missing and failures ignored. Audit-policy coverage/signoff remains open. |
| 9 | Parent move/replacement leaves stale watches | Periodic/focus setup compares pathname parent identity and rebinds all file/path members. Missing parents invalidate members but keep polling until recreation; the last detached member stops polling. | `parent`: moved parent, a missing interval, recreated parent, file and path-only members rebound; `watch-errors`: last-member timer cleanup. Both original stale binding and the intermediate repair's timer-lifetime bug have red evidence. |
| 10 | Cache identity and handle refer to different objects | Open the asynchronous directory with an empty native name relative to the retained pin; verify its actual identity before publishing/cache sharing. | `cache`: rename/replacement between pin and native open; both old and replacement directory objects get distinct correct cached handles. Round-1 red: cached identity differs from native handle. |
| 11 | UTF-8 temporary basename truncation | Windows uses a short ASCII-only temporary name independent of the document basename. Linux retains its byte-name convention. | `unicode`: two-, three-, four-byte split boundaries and a name at the NTFS UTF-16 component limit. Round-1 red: invalid UTF-8 creation. |
| 12 | Hidden pread EINTR retries hide cancellation | File-private single-attempt POSIX pread; retry OS_E_INTR only in acquisition loops after checking cancellation. Windows delegates to its completed-transfer operation. | `file_acquire_test reads`: prefix and COPY suffix cancel on the first interrupted attempt, without another successful read. Red: four attempts, including a successful read after cancellation. |
| 13 | Fuzzer callers use the old stat API | Retain/include the pre-existing `fuzz/savectl_fuzz.c` migration, with a separate native stat for the timestamp oracle; build the explicit fuzz target. No commit/state-changing git command is run by the worker. | Reconstructed round-1 fuzzer fails warnings-as-errors at the incompatible pointers; `make -j64 all fuzz` builds every fuzzer. Coordinator must include the existing fuzzer diff in the commit. |
| 14 | Portable safety cases absent on Windows | Shared `.inc` cases exercise repeated saves, retained originals, late and repeated FORCE faults, compromised Keep, multichunk cancellation/cleanup, undrained close, in-flight physical cancellation, identity words/timestamps and noalloc status/dispatch. Native shims cover write/metadata/notification failures; crash writes span multiple chunks. | Shared cases run in both default file suites. Native `file_win_test` runs supervised children in release and sanitizer configurations. Real POSIX faults/FIFOs/xattrs and unported journal/findui integrations retain explicit skip reasons on Windows. Existing correct semantics are coverage ports, not invented behavioral red failures. |
| 15 | Linux directory substitution becomes IO | Translate OS_E_ISDIR after the regular-entry metadata snapshot to FILE_ERR_CHANGED; retain native errors for genuine I/O failures. | `file_acquire_test substitute`: replace the file with a directory between stat and open. Red: IO; green: CHANGED. |

The parent lifetime fallback is a policy estimate (E): one second, converted as `1 s × 1000 ms/s = 1000 ms`. It checks only parent object identities; document contents remain event driven. It is neither a performance measurement nor a new latency gate. Save-time validation remains authoritative while a watch is stale or unavailable.

Temporary security is established in two stages: owner-only at atomic creation, then verified supported source security before any data write. Owner/group/label changes request WRITE_OWNER only when required and fail normally if the token cannot preserve them. Final metadata application/readback remains before flush and replacement. The independent post-close observer never resolves the replacement by pathname.

The additional audit-policy issue is not covered by LABEL_SECURITY_INFORMATION: audit SACL query/set requires ACCESS_SYSTEM_SECURITY and SeSecurityPrivilege, which ordinary editor tokens do not necessarily have ([Microsoft security-information rights](https://learn.microsoft.com/en-us/windows/win32/secauthz/security-information)). Requiring that access for every save would block ordinary Windows saves. The coordinator must settle a preserve-or-refuse mechanism/policy before closing finding 8; the current implementation must not be described as exact full-SACL preservation. This is separate from the implemented rejection of readable non-label policy ACEs.

Finding 16, short-name notifications, is unchanged and remains coordinator backlog triage as instructed.

Round-2 verification (M) [AC, desktop Spiral]: standalone Linux `make -j64 all` exit 0; explicit `make -j64 all fuzz` exit 0, with `fuzz: 37 fuzzers built`; full `DISPLAY=:99 make -j64 check` exit 0, with `check: 106 test binaries passed` and shell contracts passed; final `make TARGET=win -j64 check` exit 0, with `check: 5 Windows release and 5 sanitizer test binaries passed` and `ASan fired: ok`. Counts are emitted harness/make enumerations, statuses are captured process exits, and the report records load and benchmark derivations. These checks do not resolve the audit-SACL blocker above.

## Coordinator decision after round 2 (session 22 wind-up, 2026-10-26)

**Finding 8, audit SACL:** a save never refuses because audit ACEs cannot be read (`SeSecurityPrivilege` is not held by a normal user). Owner, group, DACL and integrity label are preserved and read back before any document write; explicitly set per-file audit ACEs are lost by a replace-by-rename save, as in other editors, and inherited audit ACEs are reapplied by the filesystem. Readable unsupported ACE types (resource attribute, scope, trust, filter) refuse the save with a clear error, as implemented. Preserving audit ACEs when the privilege is available is backlog (edit-77o7). Landing: rebased over W0.5/W1.x (mk/win.mk: `… font file`, W0.5's `tests/win/core_file.c` stub dropped since the real module links, `-ladvapi32`).
