# Worker report: edit-177r.3 (W0.3), session 22, suffix -s12

Verdict: **done.** `tests/os_test.c` passes unchanged as a Windows executable, 17/17 cases in release and 17/17 under ASan/UBSan, through `make TARGET=win check`; Linux unchanged and green. Design choices and confirmations: `docs/decisions/edit-177r.3.md`.

## What changed
- `src/os/os_win.c` (new, 1607 lines, `#if defined(_WIN32)`): the whole of `os.h` on Win32/NT.
- `src/base/malloc_guard_win.c` (new): the `--wrap` counting guard; `src/base/malloc_guard.c` wrapped in `#if !defined(_WIN32)` (one line each end; nothing else touched).
- `src/os/utf8.manifest`, `src/os/utf8.rc`: UTF-8 activeCodePage manifest, linked into every Windows exe.
- `mk/win.mk`: `WIN_MODULES := os` + base's guard files; `tests/os_test.c` in `WIN_TESTS` with pattern rules for both test locations; `-lntdll -lbcrypt -lshell32 -lole32`; manifest object via `llvm-windres`; `--wrap` on release links; `.SECONDARY` for objects; check recipe creates and removes the reparse fixture and passes it as `OS_TEST_DATA_REPARSE`.
- `tools/winrun.sh`: exports `WSLENV=OS_TEST_CASE:OS_TEST_DATA_REPARSE` (interop passes only WSLENV-listed variables).
- `tools/win_reparse_fixture.sh` (new): `create` / `remove` of the WOF fixture as a Windows process under `%TEMP%\sublimite-test-reparse`.

## Red runs
1. `make TARGET=win all` with `tests/os_test.c` in the build, no `os_win.c`, no Windows guard (08:19):
   ```
   ld.lld: error: undefined symbol: __wrap_malloc   (crt2.o, smoke_test.c:31)
   ld.lld: error: undefined symbol: __wrap_free
   make: *** [mk/win.mk:53: build/win/rel/tests/smoke_test.exe] Error 1   exit=2
   ```
2. `make TARGET=win build/win/rel/tests/os_test.exe` with the guard present, no `os_win.c`:
   ```
   ld.lld: error: undefined symbol: os_dir_open / os_env_get / os_err_name / os_fault_register / os_fault_test_trigger / os_file_open / os_map_file / os_mkdir / os_now_ns / os_page_size ... (20 before --error-limit)   exit=2
   ```
3. Guard part: a release link without `--wrap` refuses (`undefined symbol: __real_malloc/__real_calloc/__real_realloc/__real_free`, exit 2), so an inert guard cannot reach the `noalloc` case.
4. First suite runs, release, per case: `files` `os_map_file -> OS_E_ACCESS` (MapViewOfFile3 view size), `dirs` renames of one-letter names failing (`STATUS_INFO_LENGTH_MISMATCH`) plus `n == 4` and `os_rmdir -> OS_E_EXIST`, `listing` rewind repeating nothing (`at` not reset); sanitizer `map` `FAIL (died abnormally, code -1073741819)` at process exit (handler order). Each is in the decision doc §3.

## Green run (`make TARGET=win check`, 2026-10-26 08:48:56–08:50:10, [AC, desktop Spiral], load1 3.57 at start, 1.77 at end, exit 0)
```
reparse fixture: C:\Users\tobia\AppData\Local\Temp\sublimite-test-reparse\wof.txt
== build/win/rel/tests/smoke_test.exe      smoke ok (all checks passed)
== build/win/rel/tests/os_test.exe         os_test: 17 cases, 0 failed
== build/win/san/tests/smoke_test.exe      smoke ok (all checks passed)
== build/win/san/tests/os_test.exe         os_test: 17 cases, 0 failed
== build/win/san/tests/smoke_test.exe --expect-asan (must fail)   ASan fired: ok
```
Release cases (ms, M): errors 5022, clocks 6182, threads 5041, wake 3536, noalloc 28, files 37, dirs 30, listing 22, locks 34, map 31, fault 903, random 18, utf8 (ok), paths 19, known_dirs 21, reparse 17, proc 126. Sanitizer: errors 5148, clocks 6287, threads 5165, wake 5182, noalloc 5176, files 917, dirs 159, listing 149, locks 151, map 149, fault 339, random 137, paths 136, known_dirs 139, reparse 144, proc 827. Platform lines: `clocks: cpu resolution 15625000 ns (reported), smallest cpu step 15625000 ns, smallest slice step 100 ns`; `files: clone OS_E_NOTSUP`, `files: sparse OS_OK`; `dirs: anon OS_OK`; `map: truncate while mapped -> OS_E_BUSY`, `map: range past end of file -> OS_E_RANGE`; `fault: stray fault child -> abnormal -1073741818` (release) / `exited 1` (sanitizer, ASan report); `paths: long path 399 UTF-16 units`, `paths: relative skipped` (cwd on `\\wsl.localhost`, work dir on `C:`); `reparse: ... is a file of 65536 bytes`; `proc: spawn-to-exit 17.249 ms` (release) / `143.871 ms` (sanitizer); `noalloc: ok` with the active guard in release, "guard compiled out" in sanitizer.

## Linux (unchanged behaviour)
`make -j64 all` exit 0 (gcc). `build/san/tests/os_test`: `os_test: 17 cases, 0 failed`; `build/tests/os_test`: same. `build/san/tests/base_test` and `build/tests/base_test`: `base_test: PASS`. `git status`: only the files listed above.

## Verification beyond the suite
- Probe (release, `MapViewOfFile3`): placeholder 4096 / view 4096 on a 3-byte file → err 5; view 3 → ok; view 0 → err 87.
- Probe (rename): flags 0x43, 0x03, 0x01 with a one-unit name all `0xC0000004`; fixed by `Length = sizeof + name bytes`.
- Probe (sanitizer, bisect): `vm`, `map`, `read`, `prefetch` exit 0; `reg`, `trig` exit 5 with the handler first; all exit 0 with the handler last.
- Probe (WOF fixture): attributes 0x20, tag 0 through both a plain and a `FILE_FLAG_OPEN_REPARSE_POINT` handle; `compact` reports 65,536 bytes in 4,096.
- `llvm-nm` of `os_win.o`: mutable globals `os_fault_service`, `os_win_state` only. `llvm-objdump`: `.rsrc` present; `activeCodePage` in the image.

## Found, not fixed
1. `os_file_sync` / `os_file_sync_data` on a read-only handle: `FlushFileBuffers` needs write access → `OS_E_ACCESS` on Windows, `OS_OK` on Linux. Not in the suite; W2 migration note.
2. On 19045 the WOF filter hides the reparse tag from every handle, so the NOFOLLOW re-open branch (non-surrogate tag) runs on no fixture. Link fixtures are backlog edit-pc3s already.
3. The first three to five child spawns of a Windows run take about 5 s each (M), later ones 20–40 ms; (E) Defender scanning the exe from the `\\wsl.localhost` share. The suite's 120 s case guard covers it.
4. `src/base/base.c` (arena, pool) is not in the Windows build: `mmap`/`munmap`/`sysconf`; W0.4.
5. The Windows `fdscan` child returns 0 without looking (no `/proc`); the parent's unlink-while-child-lives check carries the inheritance test on Windows, as the suite comments.
6. `tests/display_guard.h` is not force-included into Windows test objects (decision doc edit-177r.2 §6.4): it needs X11/Wayland headers.

## Open questions
- Whether `os_win_state` (write-once caches) needs a CLAUDE.md globals mention beside `os_fault_service`.
- Whether `os_file_sync` on read handles should be documented as write-handle-only in `os.h` (a header change, so a new bead) or handled per call site in W2.
