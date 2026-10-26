# edit-177r.5 — Windows core suite harness

## Scope and counting

All work is in the assigned worktree. No production algorithms or frozen headers change. The brief's explicit suite list contains **26 names (M: whitespace count of `WIN_CORE_TESTS`), versus 27 requested (G: acceptance text)**. Both sessions retained the explicit list; the continuation asked the coordinator to resolve the discrepancy. No extra suite is inferred from the requested total.

The report gives run evidence and the final verdict. A successful executable with a printed skip is distinguished from full case coverage. Optional `PIECE_TESTING` cases remain enabled automatically in sanitizer builds, exactly as on Linux.

## Build and runner

`WIN_MODULES` covers the full core set from the brief. `win-compile` uses that same object list. Release and sanitizer archives let white-box suites supply their own implementation without duplicate symbols, while extracting normal dependencies as needed. The tree-sitter runtime and every grammar/parser/scanner named by the Linux Makefile compile for Windows. Each grammar retains its ABI include path and the Linux generated-code warning exceptions; the runtime also needs its own `lib/src` include path for vendored Unicode headers. `hl_test` links the opt-in wrapper objects explicitly. The attempted `--undefined` flags did not override lld COFF weak defaults on this toolchain (M: `llvm-nm` still reported absolute-zero definitions). Measured red: without these explicit objects, COFF weak defaults satisfied the ordinary references before archive extraction; `hl_language_julia` resolved to absolute zero and the call jumped outside the image. The sanitizer report plus `llvm-addr2line`/`llvm-objdump` located that call in `hl_test`'s main. This is build wiring, not a changed grammar assertion.

The only extra production sources linked into core tests are the pure render-grid and config-theme implementations. Windows-only fixture support supplies the existing baked font atlas lookup and the null-file-backing contract. It does **not** implement the file layer: real file-backed integration remains skipped until W2. The file bridge asserts that core snapshots do not carry backing hooks and that no non-null `file_backing` reaches it. These fixture providers are test-only, not candidates for the Windows editor link.

`tools/winrun.sh` still runs the original executable at its build path; DLL search uses the executable's directory. Its cwd is now the repository root so relative Unicode, query, and source-audit fixtures work. It forwards an explicitly configured native Python path and a translated corpus root through WSLENV. Scratch created by the Windows fixture code lives under `os_temp_dir`, with random names and `OS_OPEN_EXCL`; no Linux-side writes to Windows drives occur.

Test links retain `-MMD` header dependencies, including white-box implementation includes, and depend on the Windows makefile so link changes rebuild them. Windows `check` records every exit code and runs both configurations even after a failure. Its final exit remains nonzero if any executable fails. The existing expected-ASan failure check still runs and must produce an ASan report.

The continuation reproduced `layout_test`'s sanitizer stack overflow before changing its link. `llvm-objdump` shows an inlined `main` frame of `0x15fd58 = 1,441,112` bytes (M), greater than the PE default reserve of `0x100000 = 1,048,576` bytes (M: PE header). Its sanitizer executable alone now uses `--stack,8388608`, matching this host's Linux stack (`ulimit -s = 8192` KiB; P: `8192 * 1024 = 8,388,608` bytes). The targeted run then passed (M). No fixture moves, assertion edits or production-stack policy changes are needed.

## Per-suite port decisions

| Suite | Harness port / unchanged code | Windows omissions |
|---|---|---|
| base_test | Native `os_thread_start/join`, same malloc-guard checks | Existing sanitizer-owned malloc guard notices only |
| trace_test | `os_thread_start/join`; standard binary temporary stream replaces GNU `fmemopen` | None |
| trace_pairs_test | Native interpreter subprocess through `os_proc_spawn/wait` when `SUBLIMITE_TEST_PYTHON` is configured | Python cases print SKIP when that path is unset; final verification configures the installed native interpreter and runs all cases |
| work_test | Windows white-box inclusion of production work code with test-local `os.h` injection wrappers; Linux retains libc interposition. Wake waits replace fd polling; kernel events provide the Windows blocking completion token. Portable aligned allocation/free | None; initialization stages, cleanup, dead-mutex, deadlines, real threads and allocation expectations remain |
| work_foreground_test | Independent fragmented-index/seek/slice fixture runs with an OS-backed completion token | Real clients and editor jump depend on findui, savectl, font discovery and editor/platform port; Linux retains the original cases and headers |
| utf8_test | Unchanged; repository cwd supplies GraphemeBreakTest data | None |
| utf8_tables_test | Unchanged | None |
| scan_test | VM reserve/commit/release through os.h; test-only protection helper retains guard pages | None |
| piece_test | Native OS threads; byte initializer spells the identical non-NUL fixture bytes explicitly for clang's stricter string warning | None |
| piece_bounds_test | Unchanged; sanitizer observability preserved | None |
| piece_mem_test | Native OS thread for final snapshot release | None |
| piece_mt_test | OS threads/mutexes/conditions, one-shot condition barrier, existing reclaim watchdog duration preserved with monotonic polling because os.h has no timed condition wait. Both normal and sanitizer-only paths ported | None |
| undo_test | Self-re-exec selectors for mutation-abort and protected-history children. SIGABRT handler returns a unique child exit code for the expected assertion. `QueryWorkingSetEx` measures actual resident pages instead of mincore | None |
| lineidx_test | OS sleep/yield/accounting clock, file stat/map via os.h, translated corpus root; original timing assertions retained and Windows diagnostic values printed | Combined file/piece/find/save ownership census requires the W2 file layer |
| lineidx_os_test | Unchanged deterministic clock-role white-box suite | None |
| layout_test | OS sleep, correctly paired portable aligned allocation/free; existing baked atlas fixture | None |
| view_test | OS VM lifetime; protected-tail child self-reexec. Windows access violations naturally fail the child/suite; Linux signal diagnostics remain | None |
| find_test | Guard pages and OS sleep; self-reexec supervisor with the original watchdog | None |
| find_bench_test | Included benchmark harness uses os.h for clocks, file I/O, mapping and temporary files. Explicit self-reexec selectors cover missing start/completion and stuck shutdown | Expected optional-input SKIP lines test benchmark policy; they are passing assertions, not omitted test cases |
| find_bestof_test | Unchanged | None |
| find_p1r_test | Unchanged | None |
| find_simd_filter_verify_test | Unchanged | None |
| find_timeout_test | Self-reexec of normal-exit and missing-start fixtures; inherited private marker path checks normal atexit behavior | None; original watchdogs retained |
| hl_test | OS yield; all vendored grammars and pure config-theme dependency linked | None |
| hl_parse_test | Unchanged, uses the null-backing fixture provider | None |
| hl_worker_test | OS wake wait for the nonblocking progress pump; parser/worker cases retained | Faulted file-backed snapshot requires W2 file/fault integration |

No core suite adds a Windows X11 or editor include. `work_foreground_test` already includes `editor/private.h` on Linux because it exercises a real editor jump; that integration is explicitly guarded out on Windows. `tests/display_guard.h` stays out of Windows builds.

## Guard-page boundary

Frozen os.h has reserve/commit/decommit/release but no permission-change operation. `tests/core_vm.h` confines native `VirtualProtect` / `mprotect` to the test harness. A Windows pool can contain committed records and decommitted tail pages; protection walks committed regions and leaves the reserved tail inaccessible, because VirtualProtect rejects a range containing uncommitted pages. This preserves the protected-history test without committing or touching its retained records.

## Coordinator decisions

Final measured failures, skips, and the suite-count discrepancy are recorded in the worker report. Timing expectations have not been relaxed. The Windows CPU accounting clock reports 15,625,000 ns resolution (M); the existing lineidx cancellation assertions require a positive delta no greater than 5,000,000 ns (G, inherited assertion). The ordinary polling fixture itself requests 2,000,000 ns of accounting-clock work (G, inherited fixture), which advances by an entire accounting tick. The enlarged/fragmented cases can instead observe a zero delta. The worker keeps these failures visible, as the brief requires. A different accounting contract or an adjusted expectation requires coordinator action; wall time is not substituted for CPU time. The test-only file/atlas providers must not be mistaken for completion of the W1/W2 module ports.

## Final verification

(M) The complete Windows check exits 2: release and sanitizer each pass 25 of the 26 explicitly named core suites; only `lineidx_test` exits 1. The brief asks for 27 (G), leaving one unnamed slot. Both configurations run native Python trace-pair coverage, and the sanitizer layout stack fix is green. Windows integration skips and case-count derivations are in [the worker report](../worker-reports/edit-177r.5-s12.md), which embeds the complete Windows output and credits the prior session's red runs.

(M) Linux `make -j64 all` exits 0. The final full `DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 stdbuf -oL make -j64 check` exits 0 and prints `check: 104 test binaries passed` (count from Makefile's completed test inventory), followed by the shell display-lock contract PASS. A premature earlier interruption of raster was a worker diagnostic error; the uninterrupted rerun clears it. No Linux code or assertions were changed to obtain green.
