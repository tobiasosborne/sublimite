# edit-177r.1 worker report (s11) — PARTIAL

## Blocked
Downloading llvm-mingw worked (83,924,028 bytes, sha256 `bb7bb765...c21`, release 20260922, in `~/opt/dl/`). Extracting it under `~/opt` and running its clang was denied twice by the auto-mode permission classifier ("Code from External"). I did not work around it. Nothing below has run against the real toolchain: no green run, no clang version, no LeakSanitizer finding, no import list.

To finish: run the three install lines in docs/toolchain.md yourself (or allow a Bash rule for them), then `make TARGET=win check`, and re-dispatch (or have me continue) to paste the green run and fill the toolchain.md blanks.

## Written (unverified against the toolchain)
- `Makefile`: wrapped in `ifeq ($(TARGET),win) include mk/win.mk else ... endif`; Linux content untouched (`make -n all` still emits the same gcc commands).
- `mk/win.mk`: allow-list `WIN_MODULES :=` (empty), tests from `tests/win/`, rel `-O2 -g -msse2 -static`, san `-fsanitize=address,undefined` (dynamic ASan runtime DLLs copied next to the exe: names guessed, to be checked), same warning flags, no `-D_GNU_SOURCE`/`-pthread`. `check` runs rel and san exes via winrun.sh, then `--expect-asan` which must fail with an AddressSanitizer report.
- `tools/winrun.sh`: as specified.
- `tests/win/smoke_test.c` (chose `tests/win/` plus `_WIN32` guard): atomics, `_Thread_local`, CreateThread, SSE2, builtins, QPC, `--expect-asan`.
- `docs/toolchain.md`: Windows section with the pinned release, sha256, install and usage; blanks marked.

## Red run
`make TARGET=win check` before the toolchain exists, exit 2:
`make: .../opt/llvm-mingw/bin/x86_64-w64-mingw32-clang: No such file or directory` / `Error 127`.

## Green run
Not obtained.

## Linux
`make -n all` unchanged in form. Full Linux build not attempted (X11 packages status not checked; not needed for this bead).

## Found, not fixed
None.

## Update after toolchain approval
- Toolchain works: clang 23.1.2. `make TARGET=win all` builds `build/win/rel/tests/smoke_test.exe` (static; imports only KERNEL32 and UCRT api-sets) with -Werror clean after fixing a `/*` inside a comment in the test. San exe builds; ASan runtime DLL name corrected in mk/win.mk (libclang_rt.asan_dynamic-x86_64.dll from x86_64-w64-mingw32/bin).
- Linux `make -j64 all`: exit 0.
- STILL BLOCKED: `make TARGET=win check` fails in winrun.sh: `mktemp: failed to create directory via template /mnt/c/Users/tobia/AppData/Local/Temp/winrun.XXXXXX: Read-only file system`. `mount` shows `/mnt/c` as `9p ... (ro,...)` in the worker's shell, also with the sandbox override. I did not try to remount. So no green run, no ASan red case, no LeakSanitizer finding. Coordinator: run `make TARGET=win check` from an unsandboxed shell (or give the worker a writable /mnt/c), then fill the LSan line in docs/toolchain.md.

## Final update (in-place runs)
Changes: tools/winrun.sh now runs in place (cd to the exe's dir, exec, args and exit code pass through, writes nothing). mk/win.mk copies all three DLLs (`WIN_SAN_DLLS`). Design note: /mnt/c is ro by Tobias's choice; tests needing NTFS scratch create `%TEMP%\sublimite-test-*` themselves.

Green run, `rm -rf build/win; make TARGET=win check`, exit 0 [AC, desktop Spiral]:
```
== build/win/rel/tests/smoke_test.exe
smoke ok (all checks passed)
== build/win/san/tests/smoke_test.exe
smoke ok (all checks passed)
== build/win/san/tests/smoke_test.exe --expect-asan (must fail)
ASan fired: ok     (asan.err: "ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 1")
```
LeakSanitizer: unsupported (`detect_leaks is not supported on this platform`, exit 1), recorded in docs/toolchain.md.
Linux `make -j64 all`: exit 0.
