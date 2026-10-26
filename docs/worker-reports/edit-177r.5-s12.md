# edit-177r.5-s12 — W0.5 Windows core suites

Verdict: **partial**. Windows release and sanitizer each pass **25/26 named suites (M: RESULT records; 27 requested by the brief, G)**. Lineidx retains its failing CPU-time assertions. Linux `make all` and the full `make check` both exit **0 (M)**; the check keeps LeakSanitizer enabled on display `:99`. This is the continuation of the session terminated by provider capacity; its correct uncommitted work was retained. The coordinator owns the commit. All commands, edits, and build output used this worktree; only the three user-authorized brief/rules/transcript files were read in the main checkout. No `bd` or state-changing git command was run.

## Changes and boundaries

`mk/win.mk` now builds the full core module set, pure render/theme dependencies, test-only null-file/baked-atlas providers, the tree-sitter runtime, and the vendored grammar parsers/scanners into release/sanitizer archives. `win-compile` uses the same source inventory. Explicit optional-language wrapper objects overcome COFF weak-default archive resolution in `hl_test`. Dependencies include headers and the Windows makefile; `check` records each executable's exit and runs both configurations after failures. The expected-ASan negative control remains enabled.

The harness ports use `os.h` threads, mutexes, conditions, wake tokens, clocks, sleeps, temporary files, mapping, and process supervision. Fork fixtures self-reexec with selectors. Test-only permission changes remain native because frozen `os.h` lacks a protection operation. The runner uses the repository cwd for relative fixtures, executes the original build-path executable, and forwards the translated corpus root and optional native Python path. Windows processes create their own scratch under `%TEMP%`; nothing is copied to a Windows drive from Linux.

The continuation fixed sanitizer `layout_test` link wiring: ASan inlined its fixtures into `main`, whose measured frame exceeds PE's default stack reserve. Its sanitizer executable alone now matches this host's Linux stack reserve. Production code and timing assertions were not changed. The design and per-suite ports are in [the decision](../decisions/edit-177r.5.md).

Files touched (including retained prior-session work): `mk/win.mk`; `tools/winrun.sh`; `bench/find_bench.c`, `bench/find_supervise.h`, `bench/harness.h`; `tests/{base,trace,trace_pairs,work,work_foreground,scan,piece,piece_mem,piece_mt,undo,lineidx,layout,view,find,find_bench,find_timeout,hl,hl_worker}_test.c`; new `tests/core_{proc,sync,vm}.h`, `tests/win/core_{file,font}.c`, `tests/win/work_hooks.h`; this report and `docs/decisions/edit-177r.5.md`. No production module source or frozen header changed.

No Windows core suite includes an X11/editor header. `work_foreground_test` retains its existing Linux `editor/private.h` include for the actual editor-jump integration; that integration prints a Windows skip. `tests/display_guard.h` remains force-included only by the Linux build.

## Red and green evidence

Measurements below are (M) [AC, desktop Spiral]; this host has no BAT0. Gate values are (G) inherited assertions, not new budgets. Counts are derived from the explicit dispatch lists or RESULT records. The preceding session's evidence is credited explicitly rather than represented as fresh execution.

### Prior-session first-suite red, reused

Credit: previous session, supplied `edit-177r.5-s12.codex.log`, lines around the first `red.log` command. Its pre-run uptime was `10:03:31 up 2:20, 1 user, load average: 0.28, 0.59, 0.72` (M). Raw output remains `build/worker-177r.5/red.log`.

```text
$ make TARGET=win build/win/rel/tests/base_test.exe
ld.lld: error: undefined symbol: edit_version
ld.lld: error: undefined symbol: edit_arena_init
ld.lld: error: undefined symbol: pthread_create
ld.lld: error: undefined symbol: pthread_join
clang: error: linker command failed with exit code 1
make: *** [mk/win.mk:72: build/win/rel/tests/base_test.exe] Error 1
EXIT=2
```

The prior session's green (`tools/winrun.sh build/win/rel/tests/base_test.exe`, restriction lifted) printed `base_test: PASS`, `EXIT=0`; raw output is `build/worker-177r.5/base-green.log`. Fresh full-run evidence is pasted below.

### Prior-session wiring/harness reds, reused

Credit: same transcript and `build/worker-177r.5/win-check.log`; the full prior command exited `2` (M, `win-check.exit`). Before/after uptime is in `win-before.txt` and `win-after.txt`. Relevant failures were:

```text
undo_test:567: FAIL core_vm_protect(u.pool.base, u.pool.map_bytes, false) == 0
RESULT build/win/rel/tests/undo_test.exe exit=1
RESULT build/win/rel/tests/hl_test.exe exit=5
RESULT build/win/san/tests/undo_test.exe exit=1
RESULT build/win/san/tests/layout_test.exe exit=1
```

The previous session fixed the protected-history child selector/state and protected only committed pages, then verified release undo (`undo-green.log`, `UNDO_EXIT=0`). It traced optional-language calls to absolute-zero COFF weak defaults; `--undefined` did not override those defaults, so explicit wrapper objects replaced those flags. Its targeted sanitizer `hl_test` then passed (`hl-green.log`, `HL_EXIT=0`). Those changes are retained. Fresh complete runs below verify both configurations.

### Continuation layout red → green

```text
$ tools/winrun.sh build/win/san/tests/layout_test.exe
<3>WSL (...) ERROR: UtilBindVsockAnyPort:317: socket failed 1
EXIT=1
$ tools/winrun.sh build/win/san/tests/layout_test.exe  # sandbox restriction lifted
ERROR: AddressSanitizer: stack-overflow
SUMMARY: AddressSanitizer: stack-overflow (...layout_test.exe+0x1400b08a9)
EXIT=1
$ llvm-addr2line -f -e build/win/san/tests/layout_test.exe 0x1400b08a9
___chkstk_ms
$ llvm-objdump -p build/win/san/tests/layout_test.exe
SizeOfStackReserve      0000000000100000
$ llvm-objdump -d --disassemble-symbols=main build/win/san/tests/layout_test.exe
movl $0x15fd58, %eax
callq ... <___chkstk_ms>
$ ulimit -s
8192
```

Derivation (M): main frame `0x15fd58 = 1,441,112` bytes exceeds `0x100000 = 1,048,576` reserve. The link's `--stack,8388608` matches Linux's measured `8192 KiB * 1024 = 8,388,608` bytes. It is an executable fixture resource setting, not a relaxed assertion.

```text
$ make -j8 TARGET=win build/win/san/tests/layout_test.exe
... -Wl,--stack,8388608 ...
EXIT=0
$ tools/winrun.sh build/win/san/tests/layout_test.exe  # restriction lifted
layout no-malloc: 0 allocations over 10000 relayouts (guard ASan-inert)
layout_test: all passed
LAYOUT_EXIT=0
```

Raw continuation files: `continuation-layout-red.log`, `continuation-layout-red-native.log`, `continuation-layout-build.log`, `continuation-layout-green.log` under `build/worker-177r.5/`. The adjacent native Python run printed `Ran 5 tests ... OK`, `PYTHON_EXIT=0`; final check configures `SUBLIMITE_TEST_PYTHON=C:\Python314\python.exe`, so trace-pair Python coverage is not skipped.

## Case coverage and omissions

The acceptance calls for **27 suites (G, brief)**, but its explicit list and `WIN_CORE_TESTS` contain **26 (M, whitespace count)**. The worker asked for clarification and retained the named list. No unlisted suite is counted toward acceptance.

Case counts below are (M) source-dispatch counts for the ordinary no-argument run, using named top-level fixture groups, not individual CHECKs, randomized iterations, or child replays. An inline main test body counts once. A composite helper such as undo's `original_tests` counts once at its dispatch level. Parameterized top-level dispatches count separately. These definitions make the comparison reproducible without adding instrumentation or changing assertions. Linux release counts are inspected; Linux sanitizer and Windows counts correspond to the default commands run. A failed group still counts as executed. `r/s` means release/sanitizer. Sanitizer guard-only omissions are called out separately below.

| Suite | Linux r/s groups | Windows r/s groups | Derivation / Windows omission |
|---|---:|---:|---|
| base_test | 5/5 | 5/5 | Five `test_*` calls in main; guard observations conditional as before |
| trace_test | 8/8 | 8/8 | Threaded main fixture plus seven `test_*` calls |
| trace_pairs_test | 5/5 | 5/5 | Five Python unittest methods; native interpreter configured |
| work_test | 31/30 | 31/30 | Dispatch array has 31 entries; sanitizer returns early from allocation-guard case |
| work_foreground_test | 10/10 | 1/1 | `2 counts * 4 active clients + slice + editor`; Windows runs slice only |
| utf8_test | 17/17 | 17/17 | Seventeen `t_*` dispatches; conformance file is present |
| utf8_tables_test | 1/1 | 1/1 | Inline table-order/membership fixture |
| scan_test | 3/3 | 3/3 | Inline edge/random-byte fixture plus `large_ranges`, `guarded_ranges` |
| piece_test | 21/21 | 21/21 | Twenty-one `t_*` calls |
| piece_bounds_test | 1/10 | 1/10 | `high_offsets` plus nine PIECE_TESTING calls under sanitizer |
| piece_mem_test | 9/10 | 9/10 | Default dispatches include three final-release combinations; sanitizer adds depth |
| piece_mt_test | 9/14 | 9/14 | Nine ordinary calls plus five PIECE_TESTING calls; no optional thread-wrap/gap-trial build |
| undo_test | 12/12 | 12/12 | Twelve default calls including composite original_tests; subprocesses preserve parent fixtures |
| lineidx_test | 36/37 | 36/36 | 37 default calls, including four corpus calls; combined ownership is sanitizer-only on Linux and skipped on Windows; timing groups execute and fail |
| lineidx_os_test | 1/1 | 1/1 | Inline deterministic clock-role fixture |
| layout_test | 21/21 | 21/21 | Twenty-one `test_*` calls |
| view_test | 20/20 | 20/20 | Eleven reviews plus nine default additional calls |
| find_test | 6/6 | 6/6 | Six calls in supervised run_find_tests |
| find_bench_test | 3/3 | 3/3 | Cancellation-policy, deadline-policy, optional-input-policy groups |
| find_bestof_test | 11/11 | 11/11 | Eleven CHECK-wrapped helper calls |
| find_p1r_test | 3/3 | 3/3 | Three default helper calls |
| find_simd_filter_verify_test | 10/10 | 10/10 | Ten helper calls |
| find_timeout_test | 2/2 | 2/2 | Normal exit and missing-start child scenarios |
| hl_test | 12/12 | 12/12 | Query fixture, ten language probes, inline mailbox/budget fixture |
| hl_parse_test | 1/1 | 1/1 | Inline parse/edit/snapshot fixture |
| hl_worker_test | 2/2 | 1/1 | Inline slice/cancel/restart/failure/pool/empty fixture plus file-fault fixture |

Printed Windows-specific omissions (M: source guards and final output):

- `SKIP work_foreground real clients (8 cases require W2 savectl/findui/font discovery)`: the eight active-client/count combinations depend on modules outside the Windows core port. The real implementation is retained on Linux; no synthetic replacement is counted as coverage.
- `SKIP work_foreground editor jump (requires Windows editor/platform port)`: one actual editor integration case awaits W1/W2.
- `SKIP lineidx combined_ownership (file-backed integration requires W2 file port)`: one file/piece/find/save ownership census awaits the file port. Linux release already omits its sanitizer allocator body.
- `SKIP hl_worker faulted_snapshot (requires W2 mapped file/fault integration)`: one real file-truncation/fault integration case awaits W2.

Total newly omitted integration groups: **11 (M: 8 + 1 + 1 + 1)** per Windows configuration, of which the lineidx group is already absent in Linux release. These are omitted integration coverage, even when the containing executable exits successfully.

Existing sanitizer behavior is preserved on both platforms: base malloc/guard-exemption observations, trace allocation counts, the work allocation-guard case, and other allocator-count diagnostics are inactive while ASan owns malloc. They print the existing SKIP/inactive notices. Release runs exercise the real guard. `find_bench_test` deliberately prints five optional-input SKIP lines (M: four absent quick-row probes plus cancellation row); these are asserted policy results, not omitted cases. The previous session's unconfigured Python skip is eliminated in the final run. Additional OS conformance executables are outside the named core count; their inherited path-root skip is visible in the full output.

## Remaining Windows timing gate / coordinator decisions

Lineidx's original polling/cancellation assertions remain unchanged. Windows `os_thread_cpu_resolution_ns()` reports **15,625,000 ns (M)**; the assertion requires a positive cancellation delta of at most **5,000,000 ns (G, inherited test)**. The ordinary fixture requests **2,000,000 ns (G, inherited fixture)** of accounting-clock work and can only observe a whole CPU accounting tick; the enlarged or fragmented fixture can observe zero before that tick. Full measured values and failures are pasted below. Frozen `src/os/os.h` explicitly defines CPU accounting as `GetThreadTimes` kernel + user time and reserves `os_slice_ns` for short slice enforcement. They cannot be repaired by harness API substitution without changing what the test measures. The deterministic `lineidx_os_test` still verifies separate accounting and slice clocks.

Coordinator must resolve the missing suite name/count and decide how to represent the CPU accounting expectation on Windows (or assign an OS accounting change). This worker neither substitutes wall time for CPU time nor relaxes/skips the failing timing assertions. Coordinator should also acknowledge the explicitly documented integration omissions when assessing W0 exit; W1/W2 implementations are outside this bead.

## Linux verification

Fresh `make -j64 all` exited **0 (M)**; `make TARGET=win win-compile` exited **0 (M)** (`Nothing to be done`, compile-only inventory current). `git diff --check` exited **0 (M)**. Linux build output: `build/worker-177r.5/continuation-linux-all.log`. The first full Linux check in the sandbox exited **2 (M)** at `base_test`, with:

```text
$ DISPLAY=:99 make -j64 check
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:236: check] Error 1
EXIT=2
```

The authorized unrestricted rerun uses `DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 make -j64 check`; display tests retain the Makefile's `tools/with_display_lock.sh`. LeakSanitizer was never disabled. The first unrestricted attempt was terminated by the continuation worker at raster and returned **2 (M)** (`Error 143` from the signaled test). That was premature: the parent-debugger diagnostic showed the test executing `backend_test`'s **10,000-frame loop (G: inherited conformance fixture)**; stdout buffering had hidden progress. The debug pause invalidated its per-frame deadline, and the debugger also prevents LSan from completing; this diagnostic is not a verification failure. Attaching to the original process was denied by host ptrace policy, so the diagnostic used GDB as parent. No production or raster source was changed.

The fresh uninterrupted full run passed, exit **0 (M)**:

```text
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1 stdbuf -oL make -j64 check
lineidx_test: ok
render_test: PASS (strips, bounds, cells, lifecycle, ownership, hooks, allocator)
raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)
check: 104 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
EXIT=0
```

The **104 binaries (M)** are the Makefile's printed completed `TEST_SAN` count. `stdbuf -oL` changes only stdout buffering for progress visibility. The shell display-lock contract intentionally runs nested commands that time out or fail; their diagnostic `Error 1` lines follow the successful main `check` summary and end in the contract's PASS. They are expected negative controls, not a failed full check. LeakSanitizer stayed enabled. The earlier prematurely terminated attempt is superseded by this complete green run.

[AC, desktop Spiral], before (M):

```text
2026-10-26T10:43:46+01:00
 10:43:46 up  3:00,  1 user,  load average: 0.08, 0.49, 0.80
```

After (M):

```text
2026-10-26T10:56:26+01:00
 10:56:26 up  3:13,  1 user,  load average: 9.14, 4.58, 2.48
```

Raw files: `build/worker-177r.5/final-linux-{before.txt,check.log,check.exit,after.txt}`. The earlier unrestricted logs remain under `continuation-linux-*` for audit.

## Found, not fixed

- The Windows CPU accounting granularity conflicts with inherited sub-tick lineidx CPU assertions; see above.
- W1/W2 file/editor/font-client integration is unported and explicitly skipped; the null-file/baked-atlas fixture providers are test-only and must not enter a Windows editor link.
- Some existing Linux test output reports power as unknown on this batteryless host. The verification run itself is stamped [AC, desktop Spiral]; no unrelated test diagnostics were changed.

## Full final Windows check output

Command: `SUBLIMITE_TEST_PYTHON='C:\Python314\python.exe' make -j64 TARGET=win check`, sandbox restriction lifted for WSL interop. Exit **2 (M)**. Per configuration, **25 named core executables exit 0 and one exits 1 (M: count of RESULT records)**. The acceptance denominator **27 (G)** contains one unspecified suite. Additional `os_win_test`, `os_test`, and `smoke_test` all exit 0 in both configurations; they do not fill the missing core slot. The negative-control smoke run prints `ASan fired: ok` as required. `layout_test`, undo, and HL pass in both builds. The only nonzero RESULT records are:

```text
RESULT build/win/rel/tests/lineidx_test.exe exit=1
RESULT build/win/san/tests/lineidx_test.exe exit=1
```

Each lineidx run has four assertion failures across three case groups (M: FAIL lines): ordinary polling exceeds the span bound and cancellation bound, fragmented polling fails the positive cancellation bound, enlarged polling fails that positive bound. The diagnostic ordinary span/cancellation values are `15,625,000 ns`; enlarged values are `0 ns`, against the inherited `5,000,000 ns` bound. These failures remain red.

[AC, desktop Spiral], before (M):

```text
2026-10-26T10:31:32+01:00
 10:31:32 up  2:48,  1 user,  load average: 0.72, 0.70, 1.03
```

After (M):

```text
2026-10-26T10:41:24+01:00
 10:41:24 up  2:58,  1 user,  load average: 0.29, 0.74, 0.92
```

Complete stdout/stderr, with only CRLF normalized to LF (raw file: `build/worker-177r.5/continuation-win-check.log`):

```text
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/os_win_test.d -MT build/win/rel/tests/os_win_test.exe tests/win/os_win_test.c build/win/rel/src/os/os_posix.o build/win/rel/src/base/base.o build/win/rel/src/base/malloc_guard.o build/win/rel/src/base/malloc_guard_win.o build/win/rel/src/trace/replay.o build/win/rel/src/trace/trace.o build/win/rel/src/trace/trace_load.o build/win/rel/src/work/work.o build/win/rel/src/work/work_posix.o build/win/rel/src/piece/piece.o build/win/rel/src/undo/undo.o build/win/rel/src/scroll/index.o build/win/rel/src/scroll/resident.o build/win/rel/src/scroll/scroll.o build/win/rel/src/scroll/visual.o build/win/rel/src/lineidx/lineidx.o build/win/rel/src/layout/checkpoint.o build/win/rel/src/layout/layout.o build/win/rel/src/layout/wrap.o build/win/rel/src/view/edits.o build/win/rel/src/view/view.o build/win/rel/src/utf8/utf8.o build/win/rel/src/scan/scan.o build/win/rel/src/find/find.o build/win/rel/src/find/literal.o build/win/rel/src/find/options.o build/win/rel/src/hl/dispatch.o build/win/rel/src/hl/hl.o build/win/rel/src/hl/lang_bash.o build/win/rel/src/hl/lang_c.o build/win/rel/src/hl/lang_javascript.o build/win/rel/src/hl/lang_json.o build/win/rel/src/hl/lang_julia.o build/win/rel/src/hl/lang_latex.o build/win/rel/src/hl/lang_markdown.o build/win/rel/src/hl/lang_markdown_inline.o build/win/rel/src/hl/lang_python.o build/win/rel/src/hl/lang_rust.o build/win/rel/src/hl/lang_toml.o build/win/rel/src/hl/lang_typescript.o build/win/rel/src/hl/lang_yaml.o build/win/rel/src/hl/parse.o build/win/rel/src/hl/query.o build/win/rel/vendor/tree-sitter/lib/src/lib.o build/win/rel/vendor/tree-sitter-bash/src/parser.o build/win/rel/vendor/tree-sitter-c/src/parser.o build/win/rel/vendor/tree-sitter-javascript/src/parser.o build/win/rel/vendor/tree-sitter-json/src/parser.o build/win/rel/vendor/tree-sitter-julia/src/parser.o build/win/rel/vendor/tree-sitter-latex/src/parser.o build/win/rel/vendor/tree-sitter-python/src/parser.o build/win/rel/vendor/tree-sitter-rust/src/parser.o build/win/rel/vendor/tree-sitter-toml/src/parser.o build/win/rel/vendor/tree-sitter-yaml/src/parser.o build/win/rel/vendor/tree-sitter-bash/src/scanner.o build/win/rel/vendor/tree-sitter-javascript/src/scanner.o build/win/rel/vendor/tree-sitter-julia/src/scanner.o build/win/rel/vendor/tree-sitter-latex/src/scanner.o build/win/rel/vendor/tree-sitter-python/src/scanner.o build/win/rel/vendor/tree-sitter-rust/src/scanner.o build/win/rel/vendor/tree-sitter-toml/src/scanner.o build/win/rel/vendor/tree-sitter-yaml/src/scanner.o build/win/rel/vendor/tree-sitter-typescript/typescript/src/parser.o build/win/rel/vendor/tree-sitter-typescript/typescript/src/scanner.o build/win/rel/vendor/tree-sitter-markdown/tree-sitter-markdown/src/parser.o build/win/rel/vendor/tree-sitter-markdown/tree-sitter-markdown/src/scanner.o build/win/rel/vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/parser.o build/win/rel/vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/scanner.o build/win/rel/src/render/render.o build/win/rel/src/config/theme.o build/win/rel/tests/win/core_file.o build/win/rel/tests/win/core_font.o build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/os_win_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/smoke_test.d -MT build/win/rel/tests/smoke_test.exe tests/win/smoke_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/smoke_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/os_test.d -MT build/win/rel/tests/os_test.exe tests/os_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/os_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/base_test.d -MT build/win/rel/tests/base_test.exe tests/base_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/base_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/trace_test.d -MT build/win/rel/tests/trace_test.exe tests/trace_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/trace_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/trace_pairs_test.d -MT build/win/rel/tests/trace_pairs_test.exe tests/trace_pairs_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/trace_pairs_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/work_test.d -MT build/win/rel/tests/work_test.exe tests/work_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/work_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/work_foreground_test.d -MT build/win/rel/tests/work_foreground_test.exe tests/work_foreground_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/work_foreground_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/utf8_test.d -MT build/win/rel/tests/utf8_test.exe tests/utf8_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/utf8_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/utf8_tables_test.d -MT build/win/rel/tests/utf8_tables_test.exe tests/utf8_tables_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/utf8_tables_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/scan_test.d -MT build/win/rel/tests/scan_test.exe tests/scan_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/scan_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/piece_test.d -MT build/win/rel/tests/piece_test.exe tests/piece_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/piece_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/piece_bounds_test.d -MT build/win/rel/tests/piece_bounds_test.exe tests/piece_bounds_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/piece_bounds_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/piece_mem_test.d -MT build/win/rel/tests/piece_mem_test.exe tests/piece_mem_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/piece_mem_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/piece_mt_test.d -MT build/win/rel/tests/piece_mt_test.exe tests/piece_mt_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/piece_mt_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/undo_test.d -MT build/win/rel/tests/undo_test.exe tests/undo_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/undo_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/lineidx_test.d -MT build/win/rel/tests/lineidx_test.exe tests/lineidx_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/lineidx_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/lineidx_os_test.d -MT build/win/rel/tests/lineidx_os_test.exe tests/lineidx_os_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/lineidx_os_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/layout_test.d -MT build/win/rel/tests/layout_test.exe tests/layout_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/layout_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/view_test.d -MT build/win/rel/tests/view_test.exe tests/view_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/view_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/find_test.d -MT build/win/rel/tests/find_test.exe tests/find_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/find_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/find_bench_test.d -MT build/win/rel/tests/find_bench_test.exe tests/find_bench_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/find_bench_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/find_bestof_test.d -MT build/win/rel/tests/find_bestof_test.exe tests/find_bestof_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/find_bestof_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/find_p1r_test.d -MT build/win/rel/tests/find_p1r_test.exe tests/find_p1r_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/find_p1r_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/find_simd_filter_verify_test.d -MT build/win/rel/tests/find_simd_filter_verify_test.exe tests/find_simd_filter_verify_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/find_simd_filter_verify_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/find_timeout_test.d -MT build/win/rel/tests/find_timeout_test.exe tests/find_timeout_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/find_timeout_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/hl_test.d -MT build/win/rel/tests/hl_test.exe tests/hl_test.c build/win/rel/src/hl/lang_julia.o build/win/rel/src/hl/lang_typescript.o build/win/rel/src/hl/lang_markdown.o build/win/rel/src/hl/lang_markdown_inline.o build/win/rel/src/hl/lang_latex.o build/win/rel/src/hl/lang_bash.o build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/hl_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/hl_parse_test.d -MT build/win/rel/tests/hl_parse_test.exe tests/hl_parse_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/hl_parse_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/rel/tests/hl_worker_test.d -MT build/win/rel/tests/hl_worker_test.exe tests/hl_worker_test.c  build/win/rel/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -static -o build/win/rel/tests/hl_worker_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/os_win_test.d -MT build/win/san/tests/os_win_test.exe tests/win/os_win_test.c build/win/san/src/os/os_posix.o build/win/san/src/base/base.o build/win/san/src/base/malloc_guard.o build/win/san/src/base/malloc_guard_win.o build/win/san/src/trace/replay.o build/win/san/src/trace/trace.o build/win/san/src/trace/trace_load.o build/win/san/src/work/work.o build/win/san/src/work/work_posix.o build/win/san/src/piece/piece.o build/win/san/src/undo/undo.o build/win/san/src/scroll/index.o build/win/san/src/scroll/resident.o build/win/san/src/scroll/scroll.o build/win/san/src/scroll/visual.o build/win/san/src/lineidx/lineidx.o build/win/san/src/layout/checkpoint.o build/win/san/src/layout/layout.o build/win/san/src/layout/wrap.o build/win/san/src/view/edits.o build/win/san/src/view/view.o build/win/san/src/utf8/utf8.o build/win/san/src/scan/scan.o build/win/san/src/find/find.o build/win/san/src/find/literal.o build/win/san/src/find/options.o build/win/san/src/hl/dispatch.o build/win/san/src/hl/hl.o build/win/san/src/hl/lang_bash.o build/win/san/src/hl/lang_c.o build/win/san/src/hl/lang_javascript.o build/win/san/src/hl/lang_json.o build/win/san/src/hl/lang_julia.o build/win/san/src/hl/lang_latex.o build/win/san/src/hl/lang_markdown.o build/win/san/src/hl/lang_markdown_inline.o build/win/san/src/hl/lang_python.o build/win/san/src/hl/lang_rust.o build/win/san/src/hl/lang_toml.o build/win/san/src/hl/lang_typescript.o build/win/san/src/hl/lang_yaml.o build/win/san/src/hl/parse.o build/win/san/src/hl/query.o build/win/san/vendor/tree-sitter/lib/src/lib.o build/win/san/vendor/tree-sitter-bash/src/parser.o build/win/san/vendor/tree-sitter-c/src/parser.o build/win/san/vendor/tree-sitter-javascript/src/parser.o build/win/san/vendor/tree-sitter-json/src/parser.o build/win/san/vendor/tree-sitter-julia/src/parser.o build/win/san/vendor/tree-sitter-latex/src/parser.o build/win/san/vendor/tree-sitter-python/src/parser.o build/win/san/vendor/tree-sitter-rust/src/parser.o build/win/san/vendor/tree-sitter-toml/src/parser.o build/win/san/vendor/tree-sitter-yaml/src/parser.o build/win/san/vendor/tree-sitter-bash/src/scanner.o build/win/san/vendor/tree-sitter-javascript/src/scanner.o build/win/san/vendor/tree-sitter-julia/src/scanner.o build/win/san/vendor/tree-sitter-latex/src/scanner.o build/win/san/vendor/tree-sitter-python/src/scanner.o build/win/san/vendor/tree-sitter-rust/src/scanner.o build/win/san/vendor/tree-sitter-toml/src/scanner.o build/win/san/vendor/tree-sitter-yaml/src/scanner.o build/win/san/vendor/tree-sitter-typescript/typescript/src/parser.o build/win/san/vendor/tree-sitter-typescript/typescript/src/scanner.o build/win/san/vendor/tree-sitter-markdown/tree-sitter-markdown/src/parser.o build/win/san/vendor/tree-sitter-markdown/tree-sitter-markdown/src/scanner.o build/win/san/vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/parser.o build/win/san/vendor/tree-sitter-markdown/tree-sitter-markdown-inline/src/scanner.o build/win/san/src/render/render.o build/win/san/src/config/theme.o build/win/san/tests/win/core_file.o build/win/san/tests/win/core_font.o build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/os_win_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/smoke_test.d -MT build/win/san/tests/smoke_test.exe tests/win/smoke_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/smoke_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/os_test.d -MT build/win/san/tests/os_test.exe tests/os_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/os_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/base_test.d -MT build/win/san/tests/base_test.exe tests/base_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/base_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/trace_test.d -MT build/win/san/tests/trace_test.exe tests/trace_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/trace_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/trace_pairs_test.d -MT build/win/san/tests/trace_pairs_test.exe tests/trace_pairs_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/trace_pairs_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/work_test.d -MT build/win/san/tests/work_test.exe tests/work_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/work_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/work_foreground_test.d -MT build/win/san/tests/work_foreground_test.exe tests/work_foreground_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/work_foreground_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/utf8_test.d -MT build/win/san/tests/utf8_test.exe tests/utf8_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/utf8_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/utf8_tables_test.d -MT build/win/san/tests/utf8_tables_test.exe tests/utf8_tables_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/utf8_tables_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/scan_test.d -MT build/win/san/tests/scan_test.exe tests/scan_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/scan_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/piece_test.d -MT build/win/san/tests/piece_test.exe tests/piece_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/piece_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/piece_bounds_test.d -MT build/win/san/tests/piece_bounds_test.exe tests/piece_bounds_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/piece_bounds_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/piece_mem_test.d -MT build/win/san/tests/piece_mem_test.exe tests/piece_mem_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/piece_mem_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/piece_mt_test.d -MT build/win/san/tests/piece_mt_test.exe tests/piece_mt_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/piece_mt_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/undo_test.d -MT build/win/san/tests/undo_test.exe tests/undo_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/undo_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/lineidx_test.d -MT build/win/san/tests/lineidx_test.exe tests/lineidx_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/lineidx_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/lineidx_os_test.d -MT build/win/san/tests/lineidx_os_test.exe tests/lineidx_os_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/lineidx_os_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/view_test.d -MT build/win/san/tests/view_test.exe tests/view_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/view_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/find_test.d -MT build/win/san/tests/find_test.exe tests/find_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/find_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/find_bench_test.d -MT build/win/san/tests/find_bench_test.exe tests/find_bench_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/find_bench_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/find_bestof_test.d -MT build/win/san/tests/find_bestof_test.exe tests/find_bestof_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/find_bestof_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/find_p1r_test.d -MT build/win/san/tests/find_p1r_test.exe tests/find_p1r_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/find_p1r_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/find_simd_filter_verify_test.d -MT build/win/san/tests/find_simd_filter_verify_test.exe tests/find_simd_filter_verify_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/find_simd_filter_verify_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/find_timeout_test.d -MT build/win/san/tests/find_timeout_test.exe tests/find_timeout_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/find_timeout_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/hl_test.d -MT build/win/san/tests/hl_test.exe tests/hl_test.c build/win/san/src/hl/lang_julia.o build/win/san/src/hl/lang_typescript.o build/win/san/src/hl/lang_markdown.o build/win/san/src/hl/lang_markdown_inline.o build/win/san/src/hl/lang_latex.o build/win/san/src/hl/lang_bash.o build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/hl_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/hl_parse_test.d -MT build/win/san/tests/hl_parse_test.exe tests/hl_parse_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/hl_parse_test.exe
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -MF build/win/san/tests/hl_worker_test.d -MT build/win/san/tests/hl_worker_test.exe tests/hl_worker_test.c  build/win/san/libcore.a build/win/utf8_manifest.o -lntdll -lbcrypt -lshell32 -lole32 -o build/win/san/tests/hl_worker_test.exe
reparse fixture: C:\Users\tobia\AppData\Local\Temp\sublimite-test-reparse\wof.txt
== build/win/rel/tests/os_win_test.exe
os_win_test unmap_ownership: PASS
os_win_test delayed_read_av: PASS
os_win_test cold_caches: PASS
os_win_test duplicate_epochs: PASS
os_win_test zero_suffix_fault: PASS
os_win_test split_stage_failures: PASS
os_win_test same_view_readers: PASS
os_win_test retire_admitted: PASS
os_win_test nofollow_truncation: PASS
Junction created for C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-212540\junction <<===>> C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-212540\target
os_win_test junction: PASS
fixture unavailable: file symlink native 1314 (requires Developer Mode/elevation)
os_win_test file_symlink: UNAVAILABLE (file symlink privilege)
os_win_test tag_query_error: PASS
os_win_test anon_replacement: PASS
os_win_test literal_names: PASS
os_win_test strict_listing: PASS
os_win_test read_acl: PASS
os_win_test stdio_failure: PASS
os_win_test map_shrink: PASS
os_win_test spawn_failures: PASS
os_win_test: 18/19 passed, 1 unavailable
RESULT build/win/rel/tests/os_win_test.exe exit=0
== build/win/rel/tests/smoke_test.exe
smoke ok (all checks passed)
RESULT build/win/rel/tests/smoke_test.exe exit=0
== build/win/rel/tests/os_test.exe
errors: ok
errors: 5030.6 ms (M)
clocks: cpu resolution 15625000 ns (reported), smallest cpu step 15625000 ns, smallest slice step 100 ns (M)
clocks: ok
clocks: 6156.1 ms (M)
threads: ok
threads: 5049.5 ms (M)
wake: ok
wake: 5038.6 ms (M)
noalloc: ok
noalloc: 5057.6 ms (M)
files: clone OS_E_NOTSUP
files: sparse OS_OK
files: ok
files: 2883.7 ms (M)
dirs: anon OS_OK
dirs: ok
dirs: 27.1 ms (M)
listing: ok
listing: 22.2 ms (M)
locks: ok
locks: 32.8 ms (M)
map: truncate while mapped -> OS_E_BUSY
map: range past end of file -> OS_E_RANGE
map: ok
map: 32.4 ms (M)
fault: a child now takes an unregistered fault on purpose (a crash report may follow)
fault: stray fault child -> abnormal -1073741818
fault: ok
fault: 891.7 ms (M)
random: ok
random: 18.2 ms (M)
utf8: ok
utf8: 18.7 ms (M)
paths: relative skipped (work dir and cwd share no root)
paths: long path 399 UTF-16 units
paths: ok
paths: 20.2 ms (M)
known_dirs: temp=C:\Users\tobia\AppData\Local\Temp config=C:\Users\tobia\AppData\Roaming\sublimite data=C:\Users\tobia\AppData\Local\sublimite
known_dirs: ok
known_dirs: 20.5 ms (M)
reparse: C:\Users\tobia\AppData\Local\Temp\sublimite-test-reparse\wof.txt is a file of 65536 bytes
reparse: ok
reparse: 17.5 ms (M)
proc: spawn-to-exit 17.375 ms (M)
proc: ok
proc: 112.0 ms (M)
os_test: 17 cases, 0 failed
RESULT build/win/rel/tests/os_test.exe exit=0
== build/win/rel/tests/base_test.exe
0.0.1-dev
arena: ok
malloc guard: 0 libc malloc calls in 10000 pool ops
pool: ok
malloc guard exempt: worker exempt=0 worker counted=1 after_close=1
cpu: avx2=1 popcnt=1
base_test: PASS
RESULT build/win/rel/tests/base_test.exe exit=0
== build/win/rel/tests/trace_test.exe
G1 T4-T0       n=100 p50=50000 ns p99=99000 ns
G3 T5-T0       n=100 p50=100000 ns p99=198000 ns
T6-T0          n=100 p50=150000 ns p99=297000 ns
input append: 0 mallocs in 10000 events
trace_test: PASS
RESULT build/win/rel/tests/trace_test.exe exit=0
== build/win/rel/tests/trace_pairs_test.exe
test_all_unpaired_events_are_skipped (__main__.TraceJoinTest.test_all_unpaired_events_are_skipped) ... ok
test_cli_startup_records (__main__.TraceJoinTest.test_cli_startup_records) ... ok
test_future_enum_events_and_gaps (__main__.TraceJoinTest.test_future_enum_events_and_gaps) ... ok
test_legacy_pipeline (__main__.TraceJoinTest.test_legacy_pipeline) ... ok
test_malformed_records_still_fail (__main__.TraceJoinTest.test_malformed_records_still_fail) ... ok

----------------------------------------------------------------------
Ran 5 tests in 0.329s

OK
RESULT build/win/rel/tests/trace_pairs_test.exe exit=0
== build/win/rel/tests/work_test.exe
work_test: section1 callbacks=2 nested=0 delivered=2 pending=0
work_test: section1 recursive ok
work_test: section2 unaligned_callers=0
work_test: section2 alignment ok
work_test: section3 old=2 replacement=3 alias=0
work_test: section3 epoch ok
work_test: section4 queued_busy=0 queue_count=0
work_test: section4 shutdown ok
work_test: section5 destroyed_mutex_locks=0
work_test: section5 closed_submit ok
work_test: section6 stage=1 rc=-1 thread_attempts=0 bad_destroy=0
work_test: section6 stage=2 rc=-1 thread_attempts=0 bad_destroy=0
work_test: section6 stage=3 rc=-1 thread_attempts=0 bad_destroy=0
work_test: section6 stage=4 rc=-1 thread_attempts=1 bad_destroy=0
work_test: section6 stage=5 rc=-1 thread_attempts=2 bad_destroy=0
work_test: section6 stage=6 rc=-1 thread_attempts=3 bad_destroy=0
work_test: section6 init_errors ok
work_test: section7 cancelled_queue=0 raster_epoch=2
work_test: section7 saturated_bulk=54 raster_epoch=3
work_test: section7 churn ok
work_test: section8 slice_delivered=1 continuation_ready=1
work_test: section8 slice ok
work_test: section15 live_integrity=24576 producers=3
work_test: section15 live_ordering ok
work_test: section15 full_wrap ok
work_test: section15 state_machine ok
work_test: section17 guard_active=1 calibration=1 ui_allocations=0 closed_allocations=0
work_test: section17 allocation_guard ok
work_test: section18 timestamp_nonzero=1
work_test: section18 shutdown_stamp ok
work_test: reservation_and_cancel_contracts ok
work_test: section8 explicit_budget_fairness_shutdown_drain ok
work_test: section7 cancelled_queue_fifo_wrap ok
work_test: receive selected=2 foreign=3
work_test: selective_receive ok
work_test: batch fail_at=3 queue_delta=0 handles_unchanged=1
work_test: atomic_batch ok
work_test: receive_slices_generation_wrap_continuation ok
work_test: receive_binding_cancel_shutdown_reentrancy ok
work_test: physical_lease_completion_reuse ok
work_test: batch_pending_invalid_full_publication_shutdown ok
work_test: batch_retired_identities ok
work_test: batch_mixed_foreground_reserve_cancellation ok
work_test: dormant_receive_cursor_wrap_order ok
work_test: new_api ui_allocations=0 guard_active=1
work_test: new_api_allocation_guard ok
work_test: P1-1 section4 active=index bulk_jobs=1 foreground_delivered=1 latency_ns=1504200 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1 section4 active=find bulk_jobs=1 foreground_delivered=1 latency_ns=1496800 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1 section4 active=save bulk_jobs=1 foreground_delivered=1 latency_ns=1501100 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1 section4 active=index bulk_jobs=3 foreground_delivered=1 latency_ns=1501500 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1 section4 active=find bulk_jobs=3 foreground_delivered=1 latency_ns=1500100 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1 section4 active=save bulk_jobs=3 foreground_delivered=1 latency_ns=1505400 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1_section4_foreground_latency ok
work_test: foreground independent_reserves ui_allocations=0 guard_active=1
work_test: foreground_capacity_batch_lease_allocation ok
work_test: foreground_fifo_cancel_shutdown_stale_drain ok
work_test: foreground_mailbox_wrap_bind_live_integrity ok
work_test: foreground_init_failure_cleanup ok
work_test: continuation_same_lease_all_classes ok
work_test: foreground_priority_same_lease_queued_running ok
work_test: ok
RESULT build/win/rel/tests/work_test.exe exit=0
== build/win/rel/tests/work_foreground_test.exe
SKIP work_foreground real clients (8 cases require W2 savectl/findui/font discovery)
SKIP work_foreground editor jump (requires Windows editor/platform port)
work_foreground_test: fragmented worker seeks zero/partial/EOF + cancellation suppression checked
work_foreground_test: running_index_yields fragment_spans_before_reply=64 bound=64 (G) lease_released=1
work_foreground_test: ok
RESULT build/win/rel/tests/work_foreground_test.exe exit=0
== build/win/rel/tests/utf8_test.exe
GraphemeBreakTest: 1187 cases, 0 mismatches, 0 chunked mismatches
utf8_test: all passed
RESULT build/win/rel/tests/utf8_test.exe exit=0
== build/win/rel/tests/utf8_tables_test.exe
utf8 tables test: ok (Unicode 15.1.0, table bytes 16720)
RESULT build/win/rel/tests/utf8_tables_test.exe exit=0
== build/win/rel/tests/scan_test.exe
scan_test: all checks passed (§13 large ranges; §16 exact bounds, 16 alignments, guarded pages)
RESULT build/win/rel/tests/scan_test.exe exit=0
== build/win/rel/tests/piece_test.exe
piece_test: recursive pool release passed
  random: pieces=88658 peak=6004776 typed=1583772 deleted=1341790
piece_test: pre-P1.3c rows all passed
piece_test: checkpoint model passed
piece_test: all passed
RESULT build/win/rel/tests/piece_test.exe exit=0
== build/win/rel/tests/piece_bounds_test.exe
uint64_t positions: ok (4295098368 logical bytes; 2^32/chunk/EOF/range; snapshot after destroy)
piece_bounds_test: ok
RESULT build/win/rel/tests/piece_bounds_test.exe exit=0
== build/win/rel/tests/piece_mem_test.exe
thin leaves: live=172832 bound=203596.00 pieces=1249
thin leaves: ok
version headers: live=10495296 bound=10648382.00
version headers: ok
overflow delete (no ref): live=594304 bound=806912.00
overflow delete: ok
final release (owner): live=67776 bound=69769.25
final release: ok
final release (worker): live=67776 bound=69769.25
final release: ok
final release (worker, current cache): live=67952 bound=69769.25
final release: ok
initialization failure sweep: ok (create/copy/mapped; immediate rollback)
abort with earlier owner: live=69304 bound=69729.25
abort with earlier owner: live=69304 bound=69729.25
abort with earlier owner: live=69304 bound=69729.25
abort with earlier owner: live=69304 bound=69729.25
abort with earlier owner: live=69304 bound=69729.25
abort with earlier owner: live=69304 bound=69729.25
abort with earlier owner: live=69304 bound=69729.25
abort with earlier owner: live=69304 bound=69729.25
abort with earlier owner: ok (transaction reservations reclaimed immediately)
destroy with earlier owner: live=68328 bound=69729.25
destroy with earlier owner: ok (only snapshot-referenced ADD prefix survives)
piece_mem_test: ok
RESULT build/win/rel/tests/piece_mem_test.exe exit=0
== build/win/rel/tests/piece_mt_test.exe
lifetime: ok
failure atomicity: ok (469 injected allocation failures)
small memory: live=67776 bound=69729
trim memory: live=71952 bound=104107 pieces=100
empty memory: live=67776 bound=94632
small/trim memory: ok
repeated snapshots: live=68512 bound=71649
repeated snapshots: ok
typing allocator: malloc_calls=0 guard=active
piece_mt_test: ok (43 snapshots checked, mapping owners=0)
checkpoint snapshot threads: ok (abort/reuse/commit/destroy; mapping owners=0)
checkpoint unchanged snapshot: ok (abort restores run without reusing a shared cursor)
checkpoint initialization: ok (abort restores eligibility; inside original/mapping stays valid)
RESULT build/win/rel/tests/piece_mt_test.exe exit=0
== build/win/rel/tests/undo_test.exe
src/undo/undo.c:172: assertion failed: current && current==u->replay_guard
review editor 9: bounded group query, bursts, replay, eviction and clear passed
review 1: allocation=1 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=2 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=3 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=4 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=5 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=6 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=7 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=8 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=9 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=10 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=11 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=12 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=13 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=14 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=15 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=16 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=17 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=18 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=19 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=20 successful_prefix=0 tree_len=1048577 original=1048577
review 1: exhaustive allocator sweep failures=224 after_slice=88; bytes, queries, records and cursors restored
review 1: completed undo groups survive a later group abort
undo_test: replay allocator failures=45 (aborted, retried)
undo_test: 10000 keys mallocs=0 guard=active bytes/record=64
undo_test: all passed
review 2: expansion and one-slot admission passed
review 3: cleared resident pool=0 allowance=4096
review 4: redo records reclaimed by key=0 bound=16
review 5: first slice rc=4 records=1 groups=0
review 5: expecting an assertion in the out-of-band mutation child
review 5: slice clear/destroy abort and out-of-band mutation assertion passed
review 8: redo failure positions=31 passed
review 8: completed-group state and dirty range across batch failures passed
undo_test: P1.5e all passed
RESULT build/win/rel/tests/undo_test.exe exit=0
== build/win/rel/tests/lineidx_test.exe
prepaid restart: bounded preparation, cancellation/reuse, mallocs=0 guard=active passed (M)[AC]
-- review_capacity
-- review_seek_budget
review 6: strict byte budget + partial continuation ok
-- seek_changed_target
-- seek_skips_refreshed_chunk
review 6: continuation skips independently refreshed chunks
-- review_ui_seek
review 7: synchronous continuation yields at span limit
-- review_worker_seek
review 7: worker seek publishes target before full index
-- seek_indexed_fragmented
review 7: indexed fragmented seek resumes between slices
-- review_metadata
review 8: 10 GB edit/query and backlog adoption bounded
-- large_metadata_edits
review 8: indexed 10 GB edits at start/middle/EOF + subtree deletion ok
-- review_cancel
review 9: cancel invalidates before receive/release
-- review_polling
lineidx polling ordinary: span_cpu_ns=15625000 cancel_cpu_ns=15625000 bound_ns=5000000 (G) resolution_ns=15625000 (M)[AC, desktop Spiral]
tests/lineidx_test.c:1203: FAIL atomic_load(&s.cpu_ns) <= 5000000
tests/lineidx_test.c:1204: FAIL lineidx_cancel_cpu_ns(x) > 0 && lineidx_cancel_cpu_ns(x) <= 5000000
review 10: cancelled first span requests no further data
-- review_fragmented
tests/lineidx_test.c:1224: FAIL lineidx_cancel_cpu_ns(x) > 0 && lineidx_cancel_cpu_ns(x) <= 5000000
review 10: one-byte source cancels inside partially counted chunk
-- review_enlarged_polling
lineidx polling enlarged: span_cpu_ns=0 cancel_cpu_ns=0 bound_ns=5000000 (G) resolution_ns=15625000 (M)[AC, desktop Spiral]
tests/lineidx_test.c:1204: FAIL lineidx_cancel_cpu_ns(x) > 0 && lineidx_cancel_cpu_ns(x) <= 5000000
review work-scan 9: cancelled first span requests no further data
-- review_queued_destroy
review 11: queued destroy returns before unrelated bulk job
-- review_overflow
-- review_epoch
-- review_boundaries
-- review_memory
-- combined_ownership
G10f combined ownership census runs in the sanitizer suite
-- review_owned_source
-- review_mailbox
-- mailbox_pressure
lineidx mailbox: full-ring retry + foreign FIFO + shared dispatcher ok
-- completed_slot_reuse
lineidx mailbox: completed lease survives slot reuse ok
-- review_wide_count
-- shapes
-- estimate_and_cancel_resume
  cancel left prefix 0 of 384 chunks
  resumed: lines 628724 want 628724
-- seek_partial
-- edits
-- edit_during_build
-- no_malloc
lineidx typing: keys=10000 allocations=0 guard=active
-- memory
  1 GiB index: 16384 chunks, 269672 B live (16.459 B/chunk)
-- piece_source
-- corpus
-- corpus
-- corpus
-- corpus
SKIP lineidx combined_ownership (file-backed integration requires W2 file port)
lineidx_test: 4 failure(s)
RESULT build/win/rel/tests/lineidx_test.exe exit=1
== build/win/rel/tests/lineidx_os_test.exe
lineidx_os_test: slice yields with frozen accounting; cancellation uses CPU delta
RESULT build/win/rel/tests/lineidx_os_test.exe exit=0
== build/win/rel/tests/layout_test.exe
layout no-malloc: 0 allocations over 10000 relayouts (guard active)
layout_test: all passed
RESULT build/win/rel/tests/layout_test.exe exit=0
== build/win/rel/tests/view_test.exe
review_1: trailing empty End/Shift+End passed
review_2: exact long-line follow and rendered cursor passed
review_3: cancelled edit repair viewport passed
review_4: long-cluster boundary no-ops and legacy external repair passed
review_7: protected-tail queries and exact resumable line motion passed
review_8: bounded replacement/progress/input ownership/cancel/undo passed
review_9: conservative resumable scan budget passed
review_10: undo delete/type/replace/group/redo integration passed
review_11: reused error change output passed
review_14: external rebase/affinity/cluster/restore passed
review_15: insert/delete/prefix allocation failures and retry passed
edit-dz0: relative restore matches restore without counting the prefix
edit-wv7: wrapped relative document End/Home match view_command without a line query
view_test: 10000 keys mallocs=0 guard=active
wrap_selection: anchored Shift+Up/Down preserves soft-row affinity and preferred column
wrap_port: long visual-column continuation, anchored soft-end motion and cancellation passed
view_test: all passed
RESULT build/win/rel/tests/view_test.exe exit=0
== build/win/rel/tests/find_test.exe
find_test: ok (frozen P1.10a)
RESULT build/win/rel/tests/find_test.exe exit=0
== build/win/rel/tests/find_bench_test.exe
find fixture FAIL: missing start timeout
find fixture FAIL: missing completion timeout
find fixture FAIL: stuck shutdown deadline; elapsed_ns=4377619300 budget_ns=10000000 (M)[AC, desktop Spiral]
fixture missing/wrong size: C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-fc834517aee22c94
fixture missing/wrong size: C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-fc834517aee22c94
fixture missing/wrong size: C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-fc834517aee22c94
P1R10 self-check: PASS delayed_ack_rejected=1 cancellation_CPU_interval_rejected=1 boundary=1 median=1 tail=1
P1R11 self-check: PASS missing_start=2 missing_completion=2 shutdown=2
SKIP name=G6_dense_a file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-fc834517aee22c94 (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_a31b file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-fc834517aee22c94 (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_middle file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-fc834517aee22c94 (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_early file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-fc834517aee22c94 (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6c file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-fc834517aee22c94 (quick: optional all-a fixture absent; full mode requires it)
find_bench_test: ok (G6c misses rejected; missing events/shutdown bounded; absent optional quick fixtures skipped)
RESULT build/win/rel/tests/find_bench_test.exe exit=0
== build/win/rel/tests/find_bestof_test.exe
snapshot traversal: seeks=2 span_next=768 bound_seeks=3 bound_spans=3072
find_bestof_test: ok
RESULT build/win/rel/tests/find_bestof_test.exe exit=0
== build/win/rel/tests/find_p1r_test.exe
P1R7 dense masks: popcounts=1024 enumerated=4096 discarded_masks=960
P1R7: PASS bounded enumeration + fixed-work popcount
P1R9 pattern=a*b N=1024 polls=514 code=6
P1R9 pattern=a*b N=2048 polls=1026 code=6
P1R9 pattern=a*b N=4096 polls=2050 code=6
P1R9 pattern=aa*b N=1024 polls=645 code=6
P1R9 pattern=aa*b N=2048 polls=1285 code=6
P1R9 pattern=aa*b N=4096 polls=2565 code=6
P1R9 pattern=(a*)b N=1024 polls=770 code=6
P1R9 pattern=(a*)b N=2048 polls=1538 code=6
P1R9 pattern=(a*)b N=4096 polls=3074 code=6
P1R9: PASS linear retry budget, limit clearing, cancellation
S22 regex prefix trace: count=4096 plans=1
find_p1r_test: ok
RESULT build/win/rel/tests/find_p1r_test.exe exit=0
== build/win/rel/tests/find_simd_filter_verify_test.exe
find_simd_filter_verify_test: ok
RESULT build/win/rel/tests/find_simd_filter_verify_test.exe exit=0
== build/win/rel/tests/find_timeout_test.exe
find fixture FAIL: frozen cancellation missing start deadline; elapsed_nsfind_timeout_test: ok (normal exit hooks; frozen missing start terminated with live storage)
=5119113300 budget_ns=100000000 (M)[AC, desktop Spiral]
RESULT build/win/rel/tests/find_timeout_test.exe exit=0
== build/win/rel/tests/hl_test.exe
hl_test: PASS C/Rust parse, queries, predicates, scopes, row range, pointer-free spans, worker publication/completion, themes
hl_test: python ABI=15; fixture without ERROR; highlights query compiled
  @variable
  @constructor
  @constant
  @function
  @function.method
  @function.builtin
  @property
  @type
  @constant.builtin
  @number
  @comment
  @string
  @escape
  @punctuation.special
  @embedded
  @operator
  @keyword
hl_test: python embedded production query ran, captures=29 (P fixture-derived)
hl_test: julia ABI=15; fixture without ERROR; highlights query compiled
  @variable
  @variable.member
  @string.special.symbol
  @function.call
  @_pipe
  @function.macro
  @function.builtin
  @type.definition
  @type
  @operator
  @type.builtin
  @keyword
  @keyword.conditional
  @keyword.conditional.ternary
  @keyword.exception
  @keyword.repeat
  @keyword.modifier
  @keyword.function
  @keyword.return
  @keyword.import
  @punctuation.delimiter
  @keyword.type
  @punctuation.special
  @punctuation.bracket
  @keyword.operator
  @constant.builtin
  @variable.builtin
  @boolean
  @number
  @number.float
  @character
  @string.escape
  @string
  @string.special
  @string.documentation
  @comment
hl_test: julia embedded production query ran, captures=28 (P fixture-derived)
hl_test: javascript ABI=15; fixture without ERROR; highlights query compiled
  @variable
  @property
  @function
  @function.method
  @constructor
  @constant
  @variable.builtin
  @function.builtin
  @constant.builtin
  @comment
  @string
  @string.special
  @number
  @punctuation.delimiter
  @operator
  @punctuation.bracket
  @punctuation.special
  @embedded
  @keyword
hl_test: javascript embedded production query ran, captures=37 (P fixture-derived)
hl_test: typescript ABI=14; fixture without ERROR; highlights query compiled
  @type
  @type.builtin
  @punctuation.bracket
  @variable.parameter
  @keyword
hl_test: typescript embedded production query ran, captures=10 (P fixture-derived)
hl_test: json ABI=14; fixture without ERROR; highlights query compiled
  @string.special.key
  @string
  @number
  @constant.builtin
  @escape
  @comment
hl_test: json embedded production query ran, captures=13 (P fixture-derived)
hl_test: toml ABI=14; fixture without ERROR; highlights query compiled
  @type
  @string
  @property
  @boolean
  @comment
  @number
  @string.special
  @punctuation.delimiter
  @operator
  @punctuation.bracket
hl_test: toml embedded production query ran, captures=24 (P fixture-derived)
hl_test: yaml ABI=14; fixture without ERROR; highlights query compiled
  @boolean
  @constant.builtin
  @string
  @number
  @comment
  @label
  @type
  @attribute
  @property
  @punctuation.delimiter
  @punctuation.bracket
  @punctuation.special
hl_test: yaml embedded production query ran, captures=32 (P fixture-derived)
hl_test: markdown ABI=15; fixture without ERROR; highlights query compiled
  @text.title
  @punctuation.special
  @text.literal
  @punctuation.delimiter
  @none
  @text.uri
  @text.reference
  @string.escape
hl_test: markdown embedded production query ran, captures=6 (P fixture-derived)
hl_test: markdown_inline ABI=15; fixture without ERROR; highlights query compiled
  @text.literal
  @punctuation.delimiter
  @text.emphasis
  @text.strong
  @text.uri
  @text.reference
  @string.escape
hl_test: markdown_inline embedded production query ran, captures=11 (P fixture-derived)
hl_test: latex ABI=15; fixture without ERROR; highlights query compiled
  @comment
  @function
  @keyword
  @punctuation.delimiter
  @embedded
  @text.title
  @text.literal
  @text.reference
  @text.uri
hl_test: latex embedded production query ran, captures=9 (P fixture-derived)
hl_test: bash ABI=15; fixture without ERROR; highlights query compiled
  @string
  @function
  @property
  @keyword
  @comment
  @number
  @embedded
  @operator
  @constant
hl_test: bash embedded production query ran, captures=20 (P fixture-derived)
hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard
RESULT build/win/rel/tests/hl_test.exe exit=0
== build/win/rel/tests/hl_parse_test.exe
hl_parse_test: PASS viewport-first, snapshot lifetime, incremental edit, typing guard
RESULT build/win/rel/tests/hl_parse_test.exe exit=0
== build/win/rel/tests/hl_worker_test.exe
SKIP hl_worker faulted_snapshot (requires W2 mapped file/fault integration)
hl_worker_test: PASS slice resume, cancellation, restart, failure, pool ordering, empty/faulted parse
RESULT build/win/rel/tests/hl_worker_test.exe exit=0
== build/win/san/tests/os_win_test.exe
os_win_test unmap_ownership: PASS
os_win_test delayed_read_av: PASS
os_win_test cold_caches: PASS
os_win_test duplicate_epochs: PASS
os_win_test zero_suffix_fault: PASS
os_win_test split_stage_failures: PASS
os_win_test same_view_readers: PASS
os_win_test retire_admitted: PASS
os_win_test nofollow_truncation: PASS
Junction created for C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-218652\junction <<===>> C:\Users\tobia\AppData\Local\Temp\sublimite-test-r2-218652\target
os_win_test junction: PASS
fixture unavailable: file symlink native 1314 (requires Developer Mode/elevation)
os_win_test file_symlink: UNAVAILABLE (file symlink privilege)
os_win_test tag_query_error: PASS
os_win_test anon_replacement: PASS
os_win_test literal_names: PASS
os_win_test strict_listing: PASS
os_win_test read_acl: PASS
os_win_test stdio_failure: PASS
os_win_test map_shrink: PASS
os_win_test spawn_failures: PASS
os_win_test: 18/19 passed, 1 unavailable
RESULT build/win/san/tests/os_win_test.exe exit=0
== build/win/san/tests/smoke_test.exe
smoke ok (all checks passed)
RESULT build/win/san/tests/smoke_test.exe exit=0
== build/win/san/tests/os_test.exe
errors: ok
errors: 5162.8 ms (M)
clocks: cpu resolution 15625000 ns (reported), smallest cpu step 15625000 ns, smallest slice step 100 ns (M)
clocks: ok
clocks: 6294.8 ms (M)
threads: ok
threads: 5179.3 ms (M)
wake: ok
wake: 5190.4 ms (M)
noalloc: sanitizer build, allocator guard compiled out; counts not checked
noalloc: ok
noalloc: 4989.4 ms (M)
files: clone OS_E_NOTSUP
files: sparse OS_OK
files: ok
files: 167.1 ms (M)
dirs: anon OS_OK
dirs: ok
dirs: 162.7 ms (M)
listing: ok
listing: 168.6 ms (M)
locks: ok
locks: 158.1 ms (M)
map: truncate while mapped -> OS_E_BUSY
map: range past end of file -> OS_E_RANGE
map: ok
map: 164.4 ms (M)
fault: a child now takes an unregistered fault on purpose (a crash report may follow)
=================================================================
==216572==ERROR: AddressSanitizer: in-page-error on unknown address 0x7ffb1ef55369 (pc 0x7ffb1ef55369 bp 0x002970bebdb0 sp 0x002970bebc20 T0)
    #0 0x7ffb1ef55368 in RaiseException+0x68 (C:\WINDOWS\System32\KERNELBASE.dll+0x180025368)
    #1 0x7ff754353334  (\\wsl.localhost\Ubuntu\home\tobiasosborne\Projects\sublimite\.wt\edit-177r.5-s12\build\win\san\tests\os_test.exe+0x140033334)
    #2 0x7ff754321c8d  (\\wsl.localhost\Ubuntu\home\tobiasosborne\Projects\sublimite\.wt\edit-177r.5-s12\build\win\san\tests\os_test.exe+0x140001c8d)
    #3 0x7ff7543210fa  (\\wsl.localhost\Ubuntu\home\tobiasosborne\Projects\sublimite\.wt\edit-177r.5-s12\build\win\san\tests\os_test.exe+0x1400010fa)
    #4 0x7ffb20317123 in BaseThreadInitThunk+0x13 (C:\WINDOWS\System32\KERNEL32.DLL+0x180017123)
    #5 0x7ffb2173cc90 in RtlUserThreadStart+0x20 (C:\WINDOWS\SYSTEM32\ntdll.dll+0x18004cc90)

==216572==Register values:
rax = 0  rbx = 2970bebd80  rcx = 0  rdx = 0  
rdi = 52e17da9c  rsi = 0  rbp = 2970bebdb0  rsp = 2970bebc20  
r8  = 2970beb820  r9  = 15d93b14ba0  r10 = 2970beb9b8  r11 = 2  
r12 = 2970bebd20  r13 = 52e17d7a4  r14 = 2970bed4e0  r15 = 15d94080000  
AddressSanitizer can not provide additional info.
SUMMARY: AddressSanitizer: in-page-error (C:\WINDOWS\System32\KERNELBASE.dll+0x180025368) in RaiseException+0x68
==216572==ABORTING
fault: stray fault child -> exited 1
fault: ok
fault: 374.2 ms (M)
random: ok
random: 160.7 ms (M)
utf8: ok
utf8: 157.6 ms (M)
paths: relative skipped (work dir and cwd share no root)
paths: long path 399 UTF-16 units
paths: ok
paths: 157.6 ms (M)
known_dirs: temp=C:\Users\tobia\AppData\Local\Temp config=C:\Users\tobia\AppData\Roaming\sublimite data=C:\Users\tobia\AppData\Local\sublimite
known_dirs: ok
known_dirs: 152.7 ms (M)
reparse: C:\Users\tobia\AppData\Local\Temp\sublimite-test-reparse\wof.txt is a file of 65536 bytes
reparse: ok
reparse: 144.8 ms (M)
proc: spawn-to-exit 146.208 ms (M)
proc: ok
proc: 874.2 ms (M)
os_test: 17 cases, 0 failed
RESULT build/win/san/tests/os_test.exe exit=0
== build/win/san/tests/base_test.exe
0.0.1-dev
arena: ok
malloc guard: SKIP (AddressSanitizer owns malloc)
pool: ok
malloc guard exempt: SKIP (sanitizer owns malloc)
cpu: avx2=1 popcnt=1
base_test: PASS
RESULT build/win/san/tests/base_test.exe exit=0
== build/win/san/tests/trace_test.exe
G1 T4-T0       n=100 p50=50000 ns p99=99000 ns
G3 T5-T0       n=100 p50=100000 ns p99=198000 ns
T6-T0          n=100 p50=150000 ns p99=297000 ns
input append: malloc guard inactive (ASan build), count check skipped
trace_test: PASS
RESULT build/win/san/tests/trace_test.exe exit=0
== build/win/san/tests/trace_pairs_test.exe
test_all_unpaired_events_are_skipped (__main__.TraceJoinTest.test_all_unpaired_events_are_skipped) ... ok
test_cli_startup_records (__main__.TraceJoinTest.test_cli_startup_records) ... ok
test_future_enum_events_and_gaps (__main__.TraceJoinTest.test_future_enum_events_and_gaps) ... ok
test_legacy_pipeline (__main__.TraceJoinTest.test_legacy_pipeline) ... ok
test_malformed_records_still_fail (__main__.TraceJoinTest.test_malformed_records_still_fail) ... ok

----------------------------------------------------------------------
Ran 5 tests in 0.371s

OK
RESULT build/win/san/tests/trace_pairs_test.exe exit=0
== build/win/san/tests/work_test.exe
work_test: section1 callbacks=2 nested=0 delivered=2 pending=0
work_test: section1 recursive ok
work_test: section2 unaligned_callers=0
work_test: section2 alignment ok
work_test: section3 old=2 replacement=3 alias=0
work_test: section3 epoch ok
work_test: section4 queued_busy=0 queue_count=0
work_test: section4 shutdown ok
work_test: section5 destroyed_mutex_locks=0
work_test: section5 closed_submit ok
work_test: section6 stage=1 rc=-1 thread_attempts=0 bad_destroy=0
work_test: section6 stage=2 rc=-1 thread_attempts=0 bad_destroy=0
work_test: section6 stage=3 rc=-1 thread_attempts=0 bad_destroy=0
work_test: section6 stage=4 rc=-1 thread_attempts=1 bad_destroy=0
work_test: section6 stage=5 rc=-1 thread_attempts=2 bad_destroy=0
work_test: section6 stage=6 rc=-1 thread_attempts=3 bad_destroy=0
work_test: section6 init_errors ok
work_test: section7 cancelled_queue=0 raster_epoch=2
work_test: section7 saturated_bulk=54 raster_epoch=3
work_test: section7 churn ok
work_test: section8 slice_delivered=1 continuation_ready=1
work_test: section8 slice ok
work_test: section15 live_integrity=24576 producers=3
work_test: section15 live_ordering ok
work_test: section15 full_wrap ok
work_test: section15 state_machine ok
work_test: section17 allocation_guard skipped (sanitizer owns malloc)
work_test: section18 timestamp_nonzero=1
work_test: section18 shutdown_stamp ok
work_test: reservation_and_cancel_contracts ok
work_test: section8 explicit_budget_fairness_shutdown_drain ok
work_test: section7 cancelled_queue_fifo_wrap ok
work_test: receive selected=2 foreign=3
work_test: selective_receive ok
work_test: batch fail_at=3 queue_delta=0 handles_unchanged=1
work_test: atomic_batch ok
work_test: receive_slices_generation_wrap_continuation ok
work_test: receive_binding_cancel_shutdown_reentrancy ok
work_test: physical_lease_completion_reuse ok
work_test: batch_pending_invalid_full_publication_shutdown ok
work_test: batch_retired_identities ok
work_test: batch_mixed_foreground_reserve_cancellation ok
work_test: dormant_receive_cursor_wrap_order ok
work_test: P1-1 section4 active=index bulk_jobs=1 foreground_delivered=1 latency_ns=1484900 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1 section4 active=find bulk_jobs=1 foreground_delivered=1 latency_ns=1531500 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1 section4 active=save bulk_jobs=1 foreground_delivered=1 latency_ns=1496400 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1 section4 active=index bulk_jobs=3 foreground_delivered=1 latency_ns=1502400 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1 section4 active=find bulk_jobs=3 foreground_delivered=1 latency_ns=1481000 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1 section4 active=save bulk_jobs=3 foreground_delivered=1 latency_ns=1587400 (M)[AC] watchdog_ns=2000000000 (G)
work_test: P1-1_section4_foreground_latency ok
work_test: foreground independent_reserves ui_allocations=0 guard_active=0
work_test: foreground_capacity_batch_lease_allocation ok
work_test: foreground_fifo_cancel_shutdown_stale_drain ok
work_test: foreground_mailbox_wrap_bind_live_integrity ok
work_test: foreground_init_failure_cleanup ok
work_test: continuation_same_lease_all_classes ok
work_test: foreground_priority_same_lease_queued_running ok
work_test: ok
RESULT build/win/san/tests/work_test.exe exit=0
== build/win/san/tests/work_foreground_test.exe
SKIP work_foreground real clients (8 cases require W2 savectl/findui/font discovery)
SKIP work_foreground editor jump (requires Windows editor/platform port)
work_foreground_test: fragmented worker seeks zero/partial/EOF + cancellation suppression checked
work_foreground_test: running_index_yields fragment_spans_before_reply=64 bound=64 (G) lease_released=1
work_foreground_test: ok
RESULT build/win/san/tests/work_foreground_test.exe exit=0
== build/win/san/tests/utf8_test.exe
GraphemeBreakTest: 1187 cases, 0 mismatches, 0 chunked mismatches
utf8_test: all passed
RESULT build/win/san/tests/utf8_test.exe exit=0
== build/win/san/tests/utf8_tables_test.exe
utf8 tables test: ok (Unicode 15.1.0, table bytes 16720)
RESULT build/win/san/tests/utf8_tables_test.exe exit=0
== build/win/san/tests/scan_test.exe
scan_test: all checks passed (§13 large ranges; §16 exact bounds, 16 alignments, guarded pages)
RESULT build/win/san/tests/scan_test.exe exit=0
== build/win/san/tests/piece_test.exe
piece_test: recursive pool release passed
  random: pieces=88658 peak=6004904 typed=1583772 deleted=1341790
piece_test: pre-P1.3c rows all passed
piece_test: checkpoint model passed
piece_test: all passed
RESULT build/win/san/tests/piece_test.exe exit=0
== build/win/san/tests/piece_bounds_test.exe
snapshot owner boundary: ok (retain/take refuse saturation; mapping balanced)
insert growth boundary: ok (cursor/COW/subtree sums; exact UINT64_MAX; refusal unchanged)
reference growth boundary: ok (sum spans before mutation; exact UINT64_MAX)
reference span-sum overflow: ok (rejects before allocation; tree unchanged)
uint64_t cursor offset: ok (subtract logical start before pointer addition)
trim work: pieces=16384 delete=0 slabs=80 returns=144 bound=160
trim work: pieces=16384 delete=1 slabs=81 returns=147 bound=163
trim work: pieces=16384 delete=2 slabs=81 returns=147 bound=163
trim work: pieces=16384 delete=3 slabs=82 returns=147 bound=163
trim work: pieces=16384 delete=4 slabs=82 returns=148 bound=164
trim work: pieces=16384 delete=5 slabs=81 returns=147 bound=163
trim work: pieces=16384 delete=6 slabs=81 returns=147 bound=163
trim work: pieces=16384 delete=7 slabs=81 returns=147 bound=163
trim work: pieces=16384 delete=8 slabs=82 returns=149 bound=165
trim work: pieces=16384 delete=9 slabs=79 returns=147 bound=163
trim work: pieces=65536 delete=0 slabs=80 returns=144 bound=160
trim work: pieces=65536 delete=1 slabs=81 returns=147 bound=163
trim work: pieces=65536 delete=2 slabs=81 returns=147 bound=163
trim work: pieces=65536 delete=3 slabs=82 returns=147 bound=163
trim work: pieces=65536 delete=4 slabs=82 returns=148 bound=164
trim work: pieces=65536 delete=5 slabs=81 returns=147 bound=163
trim work: pieces=65536 delete=6 slabs=81 returns=147 bound=163
trim work: pieces=65536 delete=7 slabs=80 returns=147 bound=163
trim work: pieces=65536 delete=8 slabs=81 returns=148 bound=164
trim work: pieces=65536 delete=9 slabs=81 returns=147 bound=163
trim work: pending returns length=0 slabs=64 bound=64
trim work: pending returns take slabs=64 bound=64
trim work: pending returns length=8 slabs=64 bound=64
trim work: pending returns take slabs=64 bound=64
foreground slab work: ok (reclaimed storage only, independent of live pool)
range work: tree nodes=9357 bound=9357 height=4
range work: snapshot nodes=9356 bound=9357
range work: collect/fallback nodes=18712 bound=28071
fragmented range work: ok (tree/snapshot/content/collection/fallback)
reference work: replay=0 bytes=0 bound=510 descents=2
reference work: replay=1 bytes=0 bound=510 descents=2
reference work: replay=2 bytes=0 bound=510 descents=2
reference work: replay=3 bytes=0 bound=510 descents=2
reference work: partial bytes=1386 bound=1530
large reference work: ok (bounded prefix edges; bulk replay; exact bytes/newlines)
reference prefix rollback: ok (saved partial block; pinned/unpinned transaction snapshot)
uint64_t positions: ok (4295098368 logical bytes; 2^32/chunk/EOF/range; snapshot after destroy)
piece_bounds_test: ok
RESULT build/win/san/tests/piece_bounds_test.exe exit=0
== build/win/san/tests/piece_mem_test.exe
thin leaves: live=172960 bound=203596.00 pieces=1249
thin layout: leaf slabs=158 live=156 underfull=0 entries=1249; branch slabs=21 live=22
thin leaves: ok
version headers: live=10495424 bound=10648382.00
version headers: ok
deep snapshot: height=1 path=672 header=80 slab_B_per_slot=8
deep snapshot: height=2 path=1072 header=80 slab_B_per_slot=8
deep snapshot: height=3 path=1472 header=80 slab_B_per_slot=8
deep snapshot: height=4 path=1872 header=80 slab_B_per_slot=8
deep snapshot: height=5 path=2272 header=80 slab_B_per_slot=8
deep snapshot: height=6 path=2672 header=80 slab_B_per_slot=8
deep snapshot: height=7 path=3072 header=80 slab_B_per_slot=8
deep snapshot: height=8 path=3472 header=80 slab_B_per_slot=8
deep snapshot: height=9 path=3872 header=80 slab_B_per_slot=8
deep snapshot: height=10 path=4272 header=80 slab_B_per_slot=8
deep snapshot: height=11 path=4672 header=80 slab_B_per_slot=8
deep snapshot: height=12 path=5072 header=80 slab_B_per_slot=8
deep snapshot: height=13 path=5472 header=80 slab_B_per_slot=8
deep snapshot: height=14 path=5872 header=80 slab_B_per_slot=8
deep snapshot: height=15 path=6272 header=80 slab_B_per_slot=8
deeper snapshots: ok (actual paths/headers/slab overhead; checked uint64_t depth)
overflow delete (no ref): live=594432 bound=806912.00
delete accounting: original=0 fallback_ADD=0
overflow delete: ok
final release (owner): live=67904 bound=69769.25
final release: ok
final release (worker): live=67904 bound=69769.25
final release: ok
final release (worker, current cache): live=68080 bound=69769.25
final release: ok
initialization failure sweep: ok (create/copy/mapped; immediate rollback)
abort with earlier owner: live=69432 bound=69729.25
abort with earlier owner: live=69432 bound=69729.25
abort with earlier owner: live=69432 bound=69729.25
abort with earlier owner: live=69432 bound=69729.25
abort with earlier owner: live=69432 bound=69729.25
abort with earlier owner: live=69432 bound=69729.25
abort with earlier owner: live=69432 bound=69729.25
abort with earlier owner: live=69432 bound=69729.25
abort with earlier owner: ok (transaction reservations reclaimed immediately)
destroy with earlier owner: live=68456 bound=69729.25
destroy with earlier owner: ok (only snapshot-referenced ADD prefix survives)
piece_mem_test: ok
RESULT build/win/san/tests/piece_mem_test.exe exit=0
== build/win/san/tests/piece_mt_test.exe
reclamation typing: pieces=20000 workers=1 typing_before_worker_resume=ok
reclamation typing latency (M)[AC]: 34600 ns (TRACK, loaded box)
reclamation typing allocator: malloc_calls=0 guard=sanitizer-inert
reclamation maintenance: batches=45 bounded_steps=64 (G)
reclamation typing: pieces=20000 workers=3 typing_before_worker_resume=ok
reclamation typing latency (M)[AC]: 28400 ns (TRACK, loaded box)
reclamation typing allocator: malloc_calls=0 guard=sanitizer-inert
reclamation maintenance: batches=45 bounded_steps=64 (G)
reclamation typing: pieces=200000 workers=1 typing_before_worker_resume=ok
reclamation typing latency (M)[AC]: 23100 ns (TRACK, loaded box)
reclamation typing allocator: malloc_calls=0 guard=sanitizer-inert
reclamation maintenance: batches=447 bounded_steps=64 (G)
reclamation typing: pieces=200000 workers=3 typing_before_worker_resume=ok
reclamation typing latency (M)[AC]: 23100 ns (TRACK, loaded box)
reclamation typing allocator: malloc_calls=0 guard=sanitizer-inert
reclamation maintenance: batches=447 bounded_steps=64 (G)
reclamation typing: ok (parked retirement; insert/delete/snapshot/read; three queued owners)
reclamation lifetime: workers=1 destroy_before_worker_resume=ok live_bytes=0
reclamation lifetime: workers=3 destroy_before_worker_resume=ok live_bytes=0
reclamation lifetime: owner thread exited; final worker release; live_bytes=0
snapshot owner race: ok (one winner for the final slot; no wrap)
cursor: root_descents=0 hits=1000
compact: leaf=272 branch=400 pathcopy=672
lifetime: ok
failure atomicity: ok (469 injected allocation failures)
small memory: live=67904 bound=69729
trim memory: live=72080 bound=104107 pieces=100
empty memory: live=67904 bound=94632
small/trim memory: ok
repeated snapshots: live=68640 bound=71649
repeated snapshots: ok
typing allocator: malloc_calls=0 guard=sanitizer-inert
piece_mt_test: ok (43 snapshots checked, mapping owners=0)
checkpoint snapshot threads: ok (abort/reuse/commit/destroy; mapping owners=0)
checkpoint unchanged snapshot: ok (abort restores run without reusing a shared cursor)
checkpoint initialization: ok (abort restores eligibility; inside original/mapping stays valid)
RESULT build/win/san/tests/piece_mt_test.exe exit=0
== build/win/san/tests/undo_test.exe
src/undo/undo.c:172: assertion failed: current && current==u->replay_guard
review editor 9: bounded group query, bursts, replay, eviction and clear passed
review 1: allocation=1 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=2 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=3 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=4 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=5 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=6 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=7 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=8 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=9 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=10 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=11 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=12 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=13 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=14 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=15 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=16 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=17 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=18 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=19 successful_prefix=0 tree_len=1048577 original=1048577
review 1: allocation=20 successful_prefix=0 tree_len=1048577 original=1048577
review 1: exhaustive allocator sweep failures=224 after_slice=88; bytes, queries, records and cursors restored
review 1: completed undo groups survive a later group abort
undo_test: replay allocator failures=45 (aborted, retried)
undo_test: 10000 keys mallocs=0 guard=ASan-inert bytes/record=64
undo_test: all passed
review 2: expansion and one-slot admission passed
review 3: cleared resident pool=0 allowance=4096
review 4: redo records reclaimed by key=0 bound=16
review 5: first slice rc=4 records=1 groups=0
review 5: expecting an assertion in the out-of-band mutation child
review 5: slice clear/destroy abort and out-of-band mutation assertion passed
review 8: redo failure positions=31 passed
review 8: completed-group state and dirty range across batch failures passed
undo_test: P1.5e all passed
RESULT build/win/san/tests/undo_test.exe exit=0
== build/win/san/tests/lineidx_test.exe
prepaid restart: bounded preparation, cancellation/reuse, mallocs=0 guard=ASan-inert passed (M)[AC]
-- review_capacity
-- review_seek_budget
review 6: strict byte budget + partial continuation ok
-- seek_changed_target
-- seek_skips_refreshed_chunk
review 6: continuation skips independently refreshed chunks
-- review_ui_seek
review 7: synchronous continuation yields at span limit
-- review_worker_seek
review 7: worker seek publishes target before full index
-- seek_indexed_fragmented
review 7: indexed fragmented seek resumes between slices
-- review_metadata
review 8: 10 GB edit/query and backlog adoption bounded
-- large_metadata_edits
review 8: indexed 10 GB edits at start/middle/EOF + subtree deletion ok
-- review_cancel
review 9: cancel invalidates before receive/release
-- review_polling
lineidx polling ordinary: span_cpu_ns=15625000 cancel_cpu_ns=15625000 bound_ns=5000000 (G) resolution_ns=15625000 (M)[AC, desktop Spiral]
tests/lineidx_test.c:1203: FAIL atomic_load(&s.cpu_ns) <= 5000000
tests/lineidx_test.c:1204: FAIL lineidx_cancel_cpu_ns(x) > 0 && lineidx_cancel_cpu_ns(x) <= 5000000
review 10: cancelled first span requests no further data
-- review_fragmented
tests/lineidx_test.c:1224: FAIL lineidx_cancel_cpu_ns(x) > 0 && lineidx_cancel_cpu_ns(x) <= 5000000
review 10: one-byte source cancels inside partially counted chunk
-- review_enlarged_polling
lineidx polling enlarged: span_cpu_ns=0 cancel_cpu_ns=0 bound_ns=5000000 (G) resolution_ns=15625000 (M)[AC, desktop Spiral]
tests/lineidx_test.c:1204: FAIL lineidx_cancel_cpu_ns(x) > 0 && lineidx_cancel_cpu_ns(x) <= 5000000
review work-scan 9: cancelled first span requests no further data
-- review_queued_destroy
review 11: queued destroy returns before unrelated bulk job
-- review_overflow
-- review_epoch
-- review_boundaries
-- review_memory
  10 GB geometry + paused job: 3411536 B accounted, 3411536 B allocator-observed
-- combined_ownership
G10f combined ownership census runs in the sanitizer suite
-- review_owned_source
-- review_mailbox
-- mailbox_pressure
lineidx mailbox: full-ring retry + foreign FIFO + shared dispatcher ok
-- completed_slot_reuse
lineidx mailbox: completed lease survives slot reuse ok
-- review_wide_count
-- shapes
-- estimate_and_cancel_resume
  cancel left prefix 0 of 384 chunks
  resumed: lines 628724 want 628724
-- seek_partial
-- edits
-- edit_during_build
-- no_malloc
lineidx typing: keys=10000 allocations=0 guard=sanitizer skipped
-- memory
  1 GiB index: 16384 chunks, 269672 B live (16.459 B/chunk)
-- piece_source
-- corpus
-- corpus
-- corpus
-- corpus
lineidx_test: 4 failure(s)
SKIP lineidx combined_ownership (file-backed integration requires W2 file port)
RESULT build/win/san/tests/lineidx_test.exe exit=1
== build/win/san/tests/lineidx_os_test.exe
lineidx_os_test: slice yields with frozen accounting; cancellation uses CPU delta
RESULT build/win/san/tests/lineidx_os_test.exe exit=0
== build/win/san/tests/layout_test.exe
layout no-malloc: 0 allocations over 10000 relayouts (guard ASan-inert)
layout_test: all passed
RESULT build/win/san/tests/layout_test.exe exit=0
== build/win/san/tests/view_test.exe
review_1: trailing empty End/Shift+End passed
review_2: exact long-line follow and rendered cursor passed
review_3: cancelled edit repair viewport passed
review_4: long-cluster boundary no-ops and legacy external repair passed
review_7: protected-tail queries and exact resumable line motion passed
review_8: bounded replacement/progress/input ownership/cancel/undo passed
review_9: conservative resumable scan budget passed
review_10: undo delete/type/replace/group/redo integration passed
review_11: reused error change output passed
review_14: external rebase/affinity/cluster/restore passed
review_15: insert/delete/prefix allocation failures and retry passed
edit-dz0: relative restore matches restore without counting the prefix
edit-wv7: wrapped relative document End/Home match view_command without a line query
view_test: 10000 keys mallocs=0 guard=ASan-inert
wrap_selection: anchored Shift+Up/Down preserves soft-row affinity and preferred column
wrap_port: long visual-column continuation, anchored soft-end motion and cancellation passed
view_test: all passed
RESULT build/win/san/tests/view_test.exe exit=0
== build/win/san/tests/find_test.exe
find_test: ok (frozen P1.10a)
RESULT build/win/san/tests/find_test.exe exit=0
== build/win/san/tests/find_bench_test.exe
find fixture FAIL: missing start timeout
find fixture FAIL: missing completion timeout
find fixture FAIL: stuck shutdown deadline; elapsed_ns=5032298900 budget_ns=10000000 (M)[AC, desktop Spiral]
fixture missing/wrong size: C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-4390bbe680792bfb
fixture missing/wrong size: C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-4390bbe680792bfb
fixture missing/wrong size: C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-4390bbe680792bfb
P1R10 self-check: PASS delayed_ack_rejected=1 cancellation_CPU_interval_rejected=1 boundary=1 median=1 tail=1
P1R11 self-check: PASS missing_start=2 missing_completion=2 shutdown=2
SKIP name=G6_dense_a file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-4390bbe680792bfb (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_a31b file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-4390bbe680792bfb (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_middle file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-4390bbe680792bfb (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6v_first_last_early file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-4390bbe680792bfb (quick: optional all-a fixture absent; full mode requires it)
SKIP name=G6c file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-4390bbe680792bfb (quick: optional all-a fixture absent; full mode requires it)
find_bench_test: ok (G6c misses rejected; missing events/shutdown bounded; absent optional quick fixtures skipped)
RESULT build/win/san/tests/find_bench_test.exe exit=0
== build/win/san/tests/find_bestof_test.exe
snapshot traversal: seeks=2 span_next=768 bound_seeks=3 bound_spans=3072
find_bestof_test: ok
RESULT build/win/san/tests/find_bestof_test.exe exit=0
== build/win/san/tests/find_p1r_test.exe
P1R7 dense masks: popcounts=1024 enumerated=4096 discarded_masks=960
P1R7: PASS bounded enumeration + fixed-work popcount
P1R9 pattern=a*b N=1024 polls=514 code=6
P1R9 pattern=a*b N=2048 polls=1026 code=6
P1R9 pattern=a*b N=4096 polls=2050 code=6
P1R9 pattern=aa*b N=1024 polls=645 code=6
P1R9 pattern=aa*b N=2048 polls=1285 code=6
P1R9 pattern=aa*b N=4096 polls=2565 code=6
P1R9 pattern=(a*)b N=1024 polls=770 code=6
P1R9 pattern=(a*)b N=2048 polls=1538 code=6
P1R9 pattern=(a*)b N=4096 polls=3074 code=6
P1R9: PASS linear retry budget, limit clearing, cancellation
S22 regex prefix trace: count=4096 plans=1
find_p1r_test: ok
RESULT build/win/san/tests/find_p1r_test.exe exit=0
== build/win/san/tests/find_simd_filter_verify_test.exe
find_simd_filter_verify_test: ok
RESULT build/win/san/tests/find_simd_filter_verify_test.exe exit=0
== build/win/san/tests/find_timeout_test.exe
find fixture FAIL: frozen cancellation missing start deadline; elapsed_ns=5129141600 budget_ns=100000000 (M)[AC, desktop Spiral]
find_timeout_test: ok (normal exit hooks; frozen missing start terminated with live storage)
RESULT build/win/san/tests/find_timeout_test.exe exit=0
== build/win/san/tests/hl_test.exe
hl_test: PASS C/Rust parse, queries, predicates, scopes, row range, pointer-free spans, worker publication/completion, themes
hl_test: python ABI=15; fixture without ERROR; highlights query compiled
  @variable
  @constructor
  @constant
  @function
  @function.method
  @function.builtin
  @property
  @type
  @constant.builtin
  @number
  @comment
  @string
  @escape
  @punctuation.special
  @embedded
  @operator
  @keyword
hl_test: python embedded production query ran, captures=29 (P fixture-derived)
hl_test: julia ABI=15; fixture without ERROR; highlights query compiled
  @variable
  @variable.member
  @string.special.symbol
  @function.call
  @_pipe
  @function.macro
  @function.builtin
  @type.definition
  @type
  @operator
  @type.builtin
  @keyword
  @keyword.conditional
  @keyword.conditional.ternary
  @keyword.exception
  @keyword.repeat
  @keyword.modifier
  @keyword.function
  @keyword.return
  @keyword.import
  @punctuation.delimiter
  @keyword.type
  @punctuation.special
  @punctuation.bracket
  @keyword.operator
  @constant.builtin
  @variable.builtin
  @boolean
  @number
  @number.float
  @character
  @string.escape
  @string
  @string.special
  @string.documentation
  @comment
hl_test: julia embedded production query ran, captures=28 (P fixture-derived)
hl_test: javascript ABI=15; fixture without ERROR; highlights query compiled
  @variable
  @property
  @function
  @function.method
  @constructor
  @constant
  @variable.builtin
  @function.builtin
  @constant.builtin
  @comment
  @string
  @string.special
  @number
  @punctuation.delimiter
  @operator
  @punctuation.bracket
  @punctuation.special
  @embedded
  @keyword
hl_test: javascript embedded production query ran, captures=37 (P fixture-derived)
hl_test: typescript ABI=14; fixture without ERROR; highlights query compiled
  @type
  @type.builtin
  @punctuation.bracket
  @variable.parameter
  @keyword
hl_test: typescript embedded production query ran, captures=10 (P fixture-derived)
hl_test: json ABI=14; fixture without ERROR; highlights query compiled
  @string.special.key
  @string
  @number
  @constant.builtin
  @escape
  @comment
hl_test: json embedded production query ran, captures=13 (P fixture-derived)
hl_test: toml ABI=14; fixture without ERROR; highlights query compiled
  @type
  @string
  @property
  @boolean
  @comment
  @number
  @string.special
  @punctuation.delimiter
  @operator
  @punctuation.bracket
hl_test: toml embedded production query ran, captures=24 (P fixture-derived)
hl_test: yaml ABI=14; fixture without ERROR; highlights query compiled
  @boolean
  @constant.builtin
  @string
  @number
  @comment
  @label
  @type
  @attribute
  @property
  @punctuation.delimiter
  @punctuation.bracket
  @punctuation.special
hl_test: yaml embedded production query ran, captures=32 (P fixture-derived)
hl_test: markdown ABI=15; fixture without ERROR; highlights query compiled
  @text.title
  @punctuation.special
  @text.literal
  @punctuation.delimiter
  @none
  @text.uri
  @text.reference
  @string.escape
hl_test: markdown embedded production query ran, captures=6 (P fixture-derived)
hl_test: markdown_inline ABI=15; fixture without ERROR; highlights query compiled
  @text.literal
  @punctuation.delimiter
  @text.emphasis
  @text.strong
  @text.uri
  @text.reference
  @string.escape
hl_test: markdown_inline embedded production query ran, captures=11 (P fixture-derived)
hl_test: latex ABI=15; fixture without ERROR; highlights query compiled
  @comment
  @function
  @keyword
  @punctuation.delimiter
  @embedded
  @text.title
  @text.literal
  @text.reference
  @text.uri
hl_test: latex embedded production query ran, captures=9 (P fixture-derived)
hl_test: bash ABI=15; fixture without ERROR; highlights query compiled
  @string
  @function
  @property
  @keyword
  @comment
  @number
  @embedded
  @operator
  @constant
hl_test: bash embedded production query ran, captures=20 (P fixture-derived)
hl_test: PASS mailbox readiness, stale/plain fallback, refresh budget, allocation guard
RESULT build/win/san/tests/hl_test.exe exit=0
== build/win/san/tests/hl_parse_test.exe
hl_parse_test: PASS viewport-first, snapshot lifetime, incremental edit, typing guard
RESULT build/win/san/tests/hl_parse_test.exe exit=0
== build/win/san/tests/hl_worker_test.exe
SKIP hl_worker faulted_snapshot (requires W2 mapped file/fault integration)
hl_worker_test: PASS slice resume, cancellation, restart, failure, pool ordering, empty/faulted parse
RESULT build/win/san/tests/hl_worker_test.exe exit=0
== build/win/san/tests/smoke_test.exe --expect-asan (must fail)
ASan fired: ok
make: *** [mk/win.mk:162: check] Error 1
```


## Coordinator verification (session 22)

Two coordinator commits after the continuation (decision doc §4-5: span CPU bound takes one accounting step; per-run reparse fixture directory). Verification, all (M)[AC, desktop Spiral]:

```
round 1 (d356cfd, before the fixture fix): DISPLAY=:99 make -j64 check → exit 0, 105 suites, 11:00 → 11:43; Windows: 55 green, lineidx_test (span bound) and os_test san (fixture removed by a concurrent worktree check) red
round 2 (f7afda2): make -j64 all → 0; Linux lineidx_test rel and san → 0; make TARGET=win -j64 check → exit 0, 58 results all exit=0 (26 core suites + os_test + os_win_test + smoke, rel and san; --expect-asan fires), 11:44:47 → 11:55:33, load1 1.20 at start
lineidx on Windows: span 15,625,000 ns and cancel 15,625,000 ns against resolution 15,625,000 ns (ordinary); 0 and 0 (enlarged): both within bound + one step
```
The Linux-visible delta between the rounds is the one lineidx bound (rerun) and the fixture script (Windows-only), so the round-1 full Linux check stands for the landed tree.
