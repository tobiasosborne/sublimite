# edit-177r.3 — W0.3: `os.h` on Windows, the Windows malloc guard, the UTF-8 manifest

Box: desktop "Spiral", Windows 10 22H2 build 19045.7807 (M: `cmd /c ver`), WSL2 Ubuntu 24.04, llvm-mingw clang 23.1.2 (ucrt). No battery (`/sys/class/power_supply/BAT0` absent): every measurement is `[AC, desktop Spiral]` with the `uptime` load1 given. Frozen inputs unchanged: `src/os/os.h`, `tests/os_test.c`, `src/os/os_posix.c`.

## 1. Result

`make TARGET=win check` passes: `tests/os_test.c` unchanged, 17/17 cases in the release build and 17/17 in the ASan/UBSan build, both smoke tests, the `--expect-asan` red case still firing (2026-10-26 08:48:56–08:50:10, load1 3.57 → 1.77). Linux: `make -j64 all` (gcc 13) green; `build/san/tests/os_test` and `build/tests/os_test` 17/17; `base_test` green in both builds. The Windows-specific branches the bead names were exercised: recovery through `os_fault_test_trigger` (fault case, twice in one view, four racing installers), `>4 GiB` sparse offsets and a map at 2^32 (files), a 399-unit Unicode path (paths; (P) > 260 = `MAX_PATH`), rename over an open, mapped, read-only target and unlink of a read-only file (dirs), the compact-compressed fixture (reparse).

## 2. Files

| File | What |
|---|---|
| `src/os/os_win.c` (new, `#if defined(_WIN32)`) | the implementation; every open through `NtCreateFile` (§3.1) |
| `src/base/malloc_guard_win.c` (new), `src/base/malloc_guard.c` (wrapped in `#if !defined(_WIN32)`) | the Windows counting guard (§4) |
| `src/os/utf8.manifest`, `src/os/utf8.rc` | the UTF-8 activeCodePage manifest, RT_MANIFEST id 1 (§5) |
| `mk/win.mk` | `WIN_MODULES := os` plus `src/base/malloc_guard*.c`; `tests/os_test.c` in `WIN_TESTS`; libs, manifest object, `--wrap`, `.SECONDARY`, the check recipe with the fixture |
| `tools/winrun.sh` | exports `WSLENV=OS_TEST_CASE:OS_TEST_DATA_REPARSE`: interop passes only WSLENV-listed variables (M: unset otherwise) |
| `tools/win_reparse_fixture.sh` (new) | the reparse fixture, a Windows process under `%TEMP%\sublimite-test-reparse` (§3.10) |

## 3. Win32 choices, per family (numbers of §3 in `docs/decisions/edit-177r.2.md`)

### 3.1 Paths and opens
UTF-8 → UTF-16 on the stack (`MultiByteToWideChar`, invalid bytes `OS_E_INVAL`), `GetFullPathNameW` (relative paths against the current directory, `/` → `\`, `.`/`..` collapsed), then the NT object name `\??\C:\…` or `\??\UNC\server\share\…`. **Every open is `NtCreateFile`** (path form: NULL root; `*_at` form: the `os_dir` handle as `RootDirectory`), so one error path (`RtlNtStatusToDosError` → `SetLastError` → the table in the header, with `STATUS_FILE_IS_A_DIRECTORY` → ISDIR, `STATUS_NOT_A_DIRECTORY` → NOTDIR, `STATUS_DELETE_PENDING` → NOENT kept exact), one share mode (R|W|D), `FILE_NON_DIRECTORY_FILE` for files (a directory is ISDIR with no extra call) and `FILE_DIRECTORY_FILE` for directories, `FILE_SYNCHRONOUS_IO_NONALERT` everywhere. `"."` for `*_at` is the empty relative name (NT has no `.`). Disposition: CREATE → `FILE_OPEN_IF`, CREATE|EXCL → `FILE_CREATE`, TRUNC → `FILE_OVERWRITE`, CREATE|TRUNC → `FILE_OVERWRITE_IF` (keeps the file's identity, like `O_TRUNC`). Every file handle carries `FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES` beside its data access, because the suite calls `os_file_set_mode` on read-only and write-only opens. The `\\wsl.localhost` share works through the same path form (the suite's own exe is opened, stat'ed and mapped from it in `proc` and `stray-fault`). `OBJ_CASE_INSENSITIVE` matches Win32 behaviour.

### 3.2 Rename
`FileRenameInformationEx` (class 65) on a source handle opened with `DELETE` and `FILE_OPEN_REPARSE_POINT` (links are renamed, not followed; directories allowed, so `os_rename` of a held directory works, suite `dirs` and `locks`). Flags `POSIX_SEMANTICS`, plus `REPLACE_IF_EXISTS | IGNORE_READONLY_ATTRIBUTE` (0x40, from ntifs.h, absent from winbase.h) for REPLACE. **Confirmed on 19045 (M, suite `dirs`): a 0444 target that is open and mapped is replaced; the open handle and the view keep the old bytes.** `FILE_RENAME_INFORMATION.Length` must be at least `sizeof` the struct, 24 bytes on x64, or the kernel answers `STATUS_INFO_LENGTH_MISMATCH` (M, probe: a one-unit name at `offsetof(FileName) + 2 = 22` failed; the suite's one-letter names "a"/"b" hit this and multi-letter names did not). The buffer length is now `sizeof + name bytes`.

### 3.3 Unlink, rmdir
`FileDispositionInformationEx` (class 64) with `DELETE | POSIX_SEMANTICS` and, for files, `IGNORE_READONLY_ATTRIBUTE`; the delete handle is opened with `DELETE | FILE_WRITE_ATTRIBUTES`. Confirmed (M, suite): a 0444 file goes; an open file's name is reusable at once; an empty directory held open and locked is removed and `os_stat_path` says NOENT at once.

### 3.4 Locks
`LockFileEx` with `LOCKFILE_FAIL_IMMEDIATELY` on one byte at offset 2^62 (P: 4 EiB, above NTFS's implemented 2^48-byte file limit, so never data). `ERROR_LOCK_VIOLATION` → `OS_E_BUSY`. **Confirmed (M, suite `locks`): duplicates share the lock in both close orders, a duplicate unlocks it, the session-lease handoff holds, the last close releases, process death releases (`proc`).** Directories: the identity-keyed file `%LOCALAPPDATA%\sublimite\dirlocks\<volume>-<index1><index0>` (hex, `FILE_ID_INFO`; the 64-bit index on volumes without 128-bit IDs), both directory levels created with `FILE_OPEN_IF`, opened `FILE_SHARE_READ | FILE_SHARE_WRITE` (no DELETE), the same byte locked; the handle stays in `os_dir.lock` after `os_dir_unlock` (as the header says) and closes with the `os_dir`. **Confirmed (M, suite): rename-away keeps the lock, a new directory at the old path is free, a user's `.sublimite-dirlock` is listed and untouched.**

### 3.5 Durability
`FlushFileBuffers` for both syncs; `os_dir_sync` returns `OS_OK` without a call. Found: `FlushFileBuffers` needs write access, so `os_file_sync` on an `OS_OPEN_READ` handle fails `OS_E_ACCESS` on Windows where Linux `fsync` succeeds (not in the suite; a W2 migration note, §6).

### 3.6 Mapping and the fault service
`CreateFileMappingW(PAGE_READONLY, 0, 0)`; `VirtualAlloc2(MEM_RESERVE | MEM_RESERVE_PLACEHOLDER)` of the page-rounded extent; `MapViewOfFile3(…, MEM_REPLACE_PLACEHOLDER, PAGE_READONLY)`. The three placeholder APIs are api-set exports (`libonecore.a`, not `libkernel32.a`): resolved from `kernelbase.dll` with `GetProcAddress` at first use, so the release link keeps plain kernel32 imports and stays static. **`MapViewOfFile3`'s `ViewSize` is the logical length** (M, probe: 3 bytes of a 3-byte file maps; the page-rounded 4096 fails `ERROR_ACCESS_DENIED` past the section's byte size), unlike `MapViewOfFile`; the view still fills the page-rounded placeholder and the tail reads zero (suite: partial last page, 2^32 offset). `offset + len` past the file's size is `OS_E_RANGE` with native code 0, checked before mapping. Truncating a mapped file is `OS_E_BUSY` (`ERROR_USER_MAPPED_FILE`, suite `map`).
**Recovery.** A vectored handler for `EXCEPTION_IN_PAGE_ERROR` (and, inside a slot that is mid-split, `EXCEPTION_ACCESS_VIOLATION`, retried) over the posix slot algorithm, with a per-slot `busy` word serialising splits. Split: `UnmapViewOfFile2(MEM_PRESERVE_PLACEHOLDER)` of the mapped piece, `VirtualFree(MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)` at the faulting page, `MapViewOfFile3` of the prefix from the view's own offset (the one mapped piece of a recovered view is always its prefix), `VirtualAlloc2(MEM_COMMIT | MEM_REPLACE_PLACEHOLDER, PAGE_READONLY)` zeros over the rest. `os_unmap` walks `VirtualQuery` regions and releases each piece, then closes the section. **Confirmed (M, suite `fault`, both builds): `RaiseException(EXCEPTION_IN_PAGE_ERROR, 0, 3, {0, addr, STATUS_DEVICE_DATA_ERROR})` reaches the handler and continues; two successive splits of one view keep the prefix and zero the suffix with epoch +1 each; four threads racing the first registration all recover; 256 slots then `OS_E_NOSPC`; a stray fault kills the child (release: `0xC0000006` = −1073741818; sanitizer: ASan's in-page-error report, exit 1).**
**Handler order (found the hard way).** Registered LAST (`AddVectoredExceptionHandler(0, …)`). Registered first, the sanitizer build died at process exit with `0xC0000005` and no report whenever the service had been installed (M, bisected with a probe: `map`/`read`/`prefetch` exit 0, `register + retire` exit 5). ASan commits shadow pages from its own vectored handler; an instrumented handler ahead of it faults inside exception dispatch and the kernel terminates with the original code. ASan's fatal report is an unhandled-exception filter, so recoverable faults still reach ours.

### 3.7 Wake
Manual-reset event; `os_wake_wait` loops `WaitForSingleObject` with the time left, milliseconds rounded up.

### 3.8 Clocks and sleep
`os_now_ns` and `os_slice_ns`: `QueryPerformanceCounter` split into whole and fractional seconds (no 64-bit overflow). Smallest slice step observed 100 ns (M, suite; P: the counter runs at 10 MHz on this box, 1e9 / 1e7 = 100 ns). `os_thread_cpu_ns`: `GetThreadTimes` kernel + user. **`os_thread_cpu_resolution_ns` measured at first use over 4 steps (P: 4 × 15.625 ms = 62.5 ms, more than the suite's 40 ms spin sees): 15,625,000 ns, and the suite's own smallest observed step was 15,625,000 ns too (M, both builds) = 1/64 s (P), as predicted.** `os_wall_ns`: `GetSystemTimePreciseAsFileTime` − 116444736000000000 units (P: 11644473600 s × 1e7). `os_sleep_ns`: a `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` timer re-armed until QPC agrees the request elapsed (the suite checks ≥ 2 ms and ≥ 50 ms). `os_yield`: `SwitchToThread`.

### 3.9 Processes and the manifest
`CreateProcessW` with `STARTUPINFOEXW`, `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` of inheritable duplicates of the three standard handles (deduplicated), `STARTF_USESTDHANDLES`, `bInheritHandles = TRUE` only when the list is non-empty. **Confirmed (M, suite `proc`): the parent unlinks a C-stdio sentinel while a child lives (an inherited CRT handle would make that `OS_E_BUSY`); argv arrives byte for byte, the empty argument, quotes, trailing backslashes and the non-ASCII argument included (quoting by the `CommandLineToArgvW` rules).** Spawn-to-exit 17.2 ms release, 143.9 ms sanitizer (M, [AC, desktop Spiral], load1 3.57). `TerminateProcess` with `STATUS_CONTROL_C_EXIT` (0xC000013A) and the `killed` flag; `GetExitCodeProcess` ≥ 0xC0000000 is `ABNORMAL`. The manifest is embedded as RT_MANIFEST 1 through `llvm-windres` (`src/os/utf8.rc`) and linked into every exe (M: `.rsrc` section present, `activeCodePage` in the image); chosen over a linker switch because lld's mingw driver has none for manifest files and a windres object links the same way into `sublimite.exe` in W1.

### 3.10 Reparse-backed files
Open with `FILE_OPEN_REPARSE_POINT`, read `FileAttributeTagInfo`; a name surrogate is `OS_E_LOOP` (open) or `OS_KIND_LINK` (stat); another tag is re-opened without the flag and the identities compared ((E) three tries before `OS_E_BUSY`). The fixture is `fsutil file createnew` of 65,536 zero bytes (P: above the MFT-resident limit, so WOF has a stream to compress) + `compact /c /exe:lzx`, made and removed by `cmd.exe` through interop under `%TEMP%\sublimite-test-reparse`, one plain command per call (a quoted compound command does not survive interop's re-quoting, M). **Found: on 19045 the WOF filter hides its reparse point completely: `fsutil reparsepoint query` says "not a reparse point" and a `FILE_FLAG_OPEN_REPARSE_POINT` handle reads attributes 0x20, tag 0 (M, probe), while `compact` reports the file stored in 4,096 bytes.** The case therefore passes through the ordinary file path; the re-open branch is exercised by no fixture on this box (§6).

### 3.11 Anonymous files
Data handle under `.anon-<16 hex of os_random>` with `FILE_CREATE`; a second `DELETE` handle relative to the directory takes `DELETE | POSIX_SEMANTICS` and closes. Confirmed (M, suite `dirs`): the listing shows no extra entry while the file is open.

### 3.12 Listing, environment, known directories
A second handle to the directory (relative open of the empty name) with a 64 KiB record buffer (P: a `FILE_FULL_DIR_INFO` record is 72 bytes + 255 units × 2 = 582, rounded 584; 65536 / 584 = 112 records per fill), `FileFullDirectoryInfo` / `…RestartInfo` for rewind. Environment: `GetEnvironmentVariableW` / `SetEnvironmentVariableW` through a 32767-unit buffer (P: the Windows limit). `os_app_dir`: `SHGetKnownFolderPath` (`FOLDERID_RoamingAppData` / `LocalAppData`, GUIDs inline so no uuid library), falling back to `%APPDATA%` / `%LOCALAPPDATA%`; `os_temp_dir`: `GetTempPath2W` when the kernel has it, else `GetTempPathW`, trailing separator removed. `os_path_canonical`: `GetFinalPathNameByHandleW(VOLUME_NAME_DOS)` with `\\?\` and `\\?\UNC\` folded back.

### 3.13 Threads and synchronisation
`_beginthreadex`; `SRWLOCK`, `CONDITION_VARIABLE`, `INIT_ONCE` in the header's pointer-sized fields, `CRITICAL_SECTION` (40 bytes, `_Static_assert`ed) with `CRITICAL_SECTION_NO_DEBUG_INFO` and spin count 0 (no derived number for a spin). `noalloc` counts 0 allocations cold and contended under the wrap guard (M, release).

## 4. Windows malloc guard
`src/base/malloc_guard_win.c`: `__wrap_malloc/calloc/realloc` count and forward to `__real_*`; `free` is wrapped for symmetry only. `mk/win.mk` adds `-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free` to release links only; the sanitizer build compiles the guard out (same macro as Linux) and wraps nothing. A release link without `--wrap` fails on `__real_*` (M), so the guard cannot be silently inert. `--wrap` also redirects the mingw CRT startup objects' `malloc` (crt2.o, M: link map), outside any guard window. `malloc_guard.c` is now Linux-only (`#if !defined(_WIN32)`), nothing else in it changed.

## 5. Build wiring
`WIN_MODULES := os`; `src/base` joins with `malloc_guard*.c` only, because `base.c`'s arena and pool call `mmap` and move onto `os.h` in W0.4. `WIN_TESTS` lists `tests/os_test.c` by name next to the `tests/win/` glob; two pattern rules per build cover the two source locations. `tests/display_guard.h` is not force-included into Windows test objects (it pulls in `editor/runtime.h` and `wayland/plat.h`). `.SECONDARY` keeps the `.o` files (make deleted them as intermediates after each link, M). Libraries: `-lntdll -lbcrypt -lshell32 -lole32`.

## 6. Open points for the coordinator
1. `os_file_sync` on a read-only handle: `OS_E_ACCESS` on Windows, `OS_OK` on Linux (§3.5). A migration rule for W2 (sync through a write handle), or a header note.
2. The WOF fixture carries no visible tag on 19045 (§3.10): the NOFOLLOW re-open branch has no fixture. A OneDrive placeholder or a dedup file would show a tag; backlog with the link fixtures (edit-pc3s).
3. First child spawns of a run cost about 5 s each (M, suite: `errors` 5022 ms, `clocks` 6182 ms, `threads` 5041 ms, then 20–40 ms per case); (E) Defender scanning the freshly built exe on the `\\wsl.localhost` share until its verdict is cached. Not investigated; harmless to the suite (case guard 120 s).
4. Mutable globals in `os_win.o`: `os_fault_service` and one `os_win_state` of write-once caches (QPC frequency, CPU resolution, `SYSTEM_INFO`, the kernelbase and `GetTempPath2W` pointers) (M: `llvm-nm`). Whether the second needs a CLAUDE.md mention.

## 7. Coordinator decisions (session 22, 2026-10-26)

1. **`os_file_sync` on read handles:** no header change. Migration rule §9.12 added to `docs/decisions/edit-177r.2.md`: modules sync through a handle opened for writing; W2 checks every `fsync`/`fdatasync` site as it migrates (the save path writes through its own handle already; the journal's are write handles). A header comment follows with the first W2 bead that touches `os.h`.
2. **WOF fixture without a visible tag:** backlog, noted on edit-pc3s beside the link fixtures (a OneDrive placeholder or a dedup file would show a tag; neither exists on this box).
3. **`os_win_state`:** allowed as the Windows OS layer's single write-once cache (QPC frequency, CPU-clock resolution, `SYSTEM_INFO`, api-set function pointers), named in CLAUDE.md's globals line beside `os_fault_service`.
4. **Handler registered last** (ASan's shadow-commit handler must run first): accepted as recorded in §3.6.
5. **`src/base/base.c`:** migrated onto `os_vm_*` by edit-177r.4 in parallel; W0.5 widens `WIN_MODULES` and links the core suites.
6. **First-spawn cost (about 5 s per child for the first few spawns, (E) Defender on the `\\wsl.localhost` share):** tracked number, not a bead (law 1); W0.5 notes it if the core suites feel it.

## 8. Round 2: review findings → change → test

Scope is the coordinator's round-2 brief; finding numbers below are identifiers
from `docs/reviews/edit-177r.3.md`. The frozen header, portable suite and POSIX
implementation remain unchanged. Run evidence and limitations are in
`docs/worker-reports/edit-177r.3-s12-r2.md`.

| # | Finding | Change | Test |
|---|---|---|---|
| 1 | Unmap can release another allocator's object | Record each owned file/zero/placeholder piece before releasing it; cleanup never discovers ownership with `VirtualQuery` | `unmap_ownership`: another thread allocates and writes the released prefix before unmap continues; its allocation survives |
| 2 | Delayed AV dispatch after busy clears kills a reader | Serialize on the view; retry read AVs only after verifying the current committed, readable owned piece; writes/execution continue searching; failed recovery is published before unlocking | `delayed_read_av`: pause a real read AV in a test VEH, finish the split, then dispatch it; reject write/execute exceptions. `same_view_readers`: ordinary readers race faults on the same view |
| 3 | Partially published write-once caches | `INIT_ONCE` in `os_win_state` publishes system info, API pointers, temp API and QPC frequency together; separate once for CPU calibration avoids recursion | `cold_caches`: fresh process, pause `GetSystemInfo` after page size but before granularity, race clocks/temp/maps and verify complete publication and single initialization |
| 4 | Duplicate registrations have separate recovery state | Keep per-registration admission/retirement pins, but share serialization, owned pieces and epoch through the private map descriptor | `duplicate_epochs`: both epochs see recovery; retire an admitted registration while the other faults; frozen `fault` capacity case still fills every registration slot |
| 5 | A non-mapped address is falsely called recovered | Explicit file/zero/placeholder states; decommit/recommit a failed private zero range; reject unexpected/protected state and partial recovery | `zero_suffix_fault`: poison private zeros then inject an in-page error and require fresh zeros; protected memory is rejected. `split_stage_failures`: native failures during initial split, suffix preservation/coalescing and zero recreation, followed by complete unmap |
| 6 | NOFOLLOW destructive open precedes validation | Non-destructive bypass open, tag/identity validation and filtered data reopen first; one EOF update on the verified data handle before publication | `nofollow_truncation`: surrogate and visible-data-tag shims check both dispositions, retained bytes on rejection and no destructive bypass; native symlink fixture is attempted |
| 7 | Directory surrogates bypass link semantics; tag errors hidden | Inspect tag before directory classification; open final component generally for NOFOLLOW/delete; unlink permits a junction, rmdir rejects it; tag errors propagate; EXCL collision precedes inspection with or without NOFOLLOW | `junction`: native `mklink /J`, NOFOLLOW stat/open/unlink/rmdir and EXCL. `tag_query_error`; attempted dangling file-symlink coverage |
| 12 | Anonymous-file race deletes a replacement name | Creation handle retains DELETE authority; compare data/deletion identities before disposition; failure cleanup addresses only the created object | `anon_replacement`: rename the original and create a replacement between opens; require BUSY, replacement survives and the created object is cleaned up |
| 19 | Path form trims literal final periods/spaces | Normalize a harmless substitute final component, then restore the literal UTF-16 component before forming the NT path; representability/size errors remain explicit | `literal_names`: NTFS `note`, `note.`, `note ` remain distinct through listing, absolute/relative open, identity comparison with `_at`, rename and unlink |
| 24 | Listing replaces invalid UTF-16 or skips entries | Strict UTF-16 conversion, return INVAL/RANGE for an unrepresentable entry after consuming that record | `strict_listing`: native unpaired-surrogate name beside the actual U+FFFD name yields an explicit error and exactly the real representable entry |
| 8 | Data opens demand unrelated metadata rights | Plain read/write opens request their own mode's rights; NOFOLLOW additionally needs tag-read rights. `set_mode` obtains an identity-checked `ReOpenFile` metadata handle or returns ACCESS; write-only `file_stat` obtains one on demand after ACCESS | `read_acl`: real ACL denies WRITE_ATTRIBUTES but allows reading, then denies READ_DATA/READ_ATTRIBUTES but permits writing. Frozen `files` pins stat/mode on ordinary write-only handles |
| 14 | Failed stdio conversion consumes the file | Duplicate non-inheritable first, convert duplicate, close original only after success; translate the CRT's actual errno | `stdio_failure`: injected ENOMEM retains original I/O and exclusive-lock lifetime; frozen `files` checks successful conversion |
| 16 | Acquisition shrink escapes as ACCESS/IO | On section/view acquisition failure, recheck EOF on the same file handle; proven missing range returns RANGE while preserving native error; unchanged EOF preserves the genuine failure | `map_shrink`: nonzero offset, native shrink before section; smaller-section shim before the real failed view map; unchanged-size NOMEM control |
| 21 | Spawn silently loses std handles; invalid attribute cleanup | Fail every valid-standard-handle duplication error; explicit cleanup state; delete only an initialized attribute list; preserve errors across cleanup | `spawn_failures`: each standard duplicate, command allocation, attribute size/allocation/init/update and process creation; native error mapping and handle-count leak checks |
| 30 (scoped parts) | Missing Windows regression coverage | Windows-only supervised supplemental suite with native fixtures/API shims; explicit unavailable verdict, nonzero on any failure | `make TARGET=win check` builds/runs the suite in release and ASan/UBSan alongside unchanged `os_test` |

### Shared view ownership and cost

`os_map.section` is private in the frozen contract. It now points to a
`map_owner` descriptor whose `section` member holds the actual section HANDLE.
The descriptor is obtained with `VirtualAlloc`, avoiding CRT heap allocation or
another process-global cache. Its address-space reservation follows allocation
granularity and its committed storage is `round_page(sizeof(map_owner))` (P:
Windows VM rounding); it is freed by `os_unmap`. This is additional native VM
storage per live mapping. There are at most a file prefix and a zero suffix
(P: the split algorithm preserves/coalesces the old suffix before replacing the
new suffix); partial failures retain the exact placeholder ownership too.

Sharing the descriptor preserves the frozen registration-capacity semantics and
makes every registrant observe the same recovery epoch. It also keeps topology
available after the last registration retires, until unmap. Each registration
retains its own admitted-handler pins, so retirement waits for its admitted
handlers even if another registration remains live. The test-only
`OS_WIN_TEST_HOOK` macro expands away in normal objects; supplemental API shims
and the delayed-AV interceptor exist only in the suite's translation unit.

### Truncation, metadata rights and fixture limits

NOFOLLOW truncation has one atomic EOF update against the validated object; a
name replacement cannot redirect that update. The validation/open/EOF sequence
is not a single namespace transaction. The existing data-tag two-open retry race
(finding 9) is explicitly outside this brief and remains unchanged.

Reading does not require WRITE_ATTRIBUTES. `os_file_set_mode` obtains additional
rights through the same object, verifies its identity and returns ACCESS when
its ACL denies them. `os_file_stat` similarly obtains READ_ATTRIBUTES only if
needed for a write-only handle. This preserves the frozen suite without adding
read-attribute rights to every plain write open.

NTFS prevents truncating an existing section on this box, so the second map
acquisition race uses an invalidated/replaced smaller section shim followed by
real `MapViewOfFile3` failure. A visible data tag is also shimmed: the existing
WOF fixture hides its tag. The native junction fixture runs successfully; file
symlink creation is explicitly unavailable with ERROR_PRIVILEGE_NOT_HELD (M:
native code 1314), so Developer Mode/elevation is needed for that fixture.
External-volume, FAT/exFAT and network-disconnect coverage remains outside scope.


Round-2 acceptance (M, [AC, desktop Spiral]; derivation: case verdicts in
`build/r2/acceptance.log`): `make TARGET=win check` exits 0; each frozen-suite
variant prints `os_test: 17 cases, 0 failed`; each supplemental variant prints
`os_win_test: 18/19 passed, 1 unavailable`. The unavailable item is the native
file-symlink fixture, explicitly permitted by the brief's host limitation.
Linux `make -j64 all` exits 0 and release/sanitizer `os_test` each report
`17 cases, 0 failed`. The report records each run's `uptime` load and red/green
commands/output. No excluded finding is claimed fixed.
