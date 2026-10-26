# edit-177r.4-s12 worker report

## Verdict

Partial: the migrations supported by frozen os.h are implemented, and Windows object compilation and smoke verification pass. The mincore residency probe remains Linux-only under the brief's required fallback rule; the coordinator must decide its Windows contract. The required Linux make check exits with a pre-existing missing-BAT0 fixture failure; this worker does not change out-of-scope test expectations. Remaining-suite results are recorded below.

## Scope and changes

The inventory table was written first in `docs/decisions/edit-177r.4.md`, before production edits. Its raw pre-edit include/call scan is in `build/worker-logs/inventory-before.txt`. Pure modules were verified from source rather than assumed: utf8, scan, find and hl needed no direct OS-call changes. All listed modules participate in the new win-compile target.

- base uses OS page reservations/releases, with Windows allocation-page commits and the original Linux lazy mapping/allocation paths. The glibc malloc guard is wrapped whole for Linux; no Windows guard implementation was added.
- trace, view and scroll use os_now_ns. Trace replay retains an absolute logical schedule, recomputing remaining sleep against each original deadline. layout and lineidx waits use os_sleep_ns.
- piece pool_mu uses os_rmutex; add-store locks use os_mutex; owner checks use os_thread_self. The test-only add_ptr helper now shares its caller's PIECE_TESTING guard, resolving the Windows unused-function error.
- lineidx deadlines and comparisons use os_slice_ns; cpu_begin and cancellation accounting use os_thread_cpu_ns. Worker deadlines start separately in the slice-clock domain.
- work uses OS threads/mutexes/conditions and an os_wake, drained before mailbox state is consumed and re-signalled when bounded draining leaves messages. work_pool_wake exposes the borrowed wake. work_posix.c retains work_pool_eventfd for Linux W2 callers. Workers publish identities through work_pool_worker_id so existing ownership checks need no access to private os_thread fields.
- undo decommits through os.h and commits record/backup pages before reuse. Commit failure returns UNDO_ERR_NOMEM and preserves replay rollback.
- Windows-only C11 reverse byte searches replace unavailable GNU memrchr in lineidx/layout; Linux keeps memrchr.
- WIN_MODULES and the frozen OS contract are unchanged. win-compile supplies the vendored tree-sitter API include path, uses the existing release object rule and warning flags, and links nothing. all remains the default Windows target.

Linux fixture adaptations preserve every existing assertion: work's fault-injection tests compare native/wrapper object addresses and use the existing fd accessor; raster fixtures use active/os_mutex; editor/file ownership tests compare the exact published bulk-worker identity. The Linux journal CPU benchmark obtains the same pthread CPU clock from os_thread_self's documented Linux identity representation. raster_bench uses os_mutex for the migrated public pool lock. No editor/file/journal/raster production behavior was changed.

## Files touched

Production/build: `mk/win.mk`; `src/base/{base.c,base.h,malloc_guard.c}`; `src/trace/{trace.c,replay.c}`; `src/work/{work.c,work.h,work_posix.c}`; `src/piece/piece.c`; `src/undo/undo.c`; `src/scroll/{index.c,resident.c,visual.c}`; `src/lineidx/lineidx.c`; `src/layout/{checkpoint.c,wrap.c}`; `src/view/view.c`.

Tests/bench consumers: `tests/{lineidx_os_test.c,piece_test.c,work_test.c,raster_test.c,editor_test.c,file_test.c}`; `bench/{journal_bench.c,raster_bench.c}`.

Documentation: this report and `docs/decisions/edit-177r.4.md`. Build products, diagnostic mutants and logs stay in this worktree's build/worker-logs. No bd or state-changing git command was run, and no main-checkout file was changed.

## Red / green evidence

All exit codes and quoted output below are (M), obtained by executing the shown commands in this worktree. Measurement context: **[AC, desktop Spiral]**, BAT0 absent. Load snapshots are from uptime; they are evidence of concurrent load, not a performance comparison:

```text
08:14:56 up 31 min, 1 user, load average: 4.15, 1.38, 0.54
08:26:12 up 42 min, 1 user, load average: 10.89, 5.62, 2.67
```

Windows mechanical-migration red, after adding only the inventory and compile target:

```text
$ make TARGET=win -j64 win-compile
exit=2
src/base/base.c:5:10: fatal error: 'sys/mman.h' file not found
src/scroll/resident.c:5:10: fatal error: 'sys/mman.h' file not found
src/undo/undo.c:3:10: fatal error: 'sys/mman.h' file not found
src/lineidx/lineidx.c:375:28: error: call to undeclared function 'memrchr'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
src/work/work.c:6:10: fatal error: 'sys/eventfd.h' file not found
src/layout/wrap.c:236:29: error: call to undeclared function 'memrchr'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
src/piece/piece.c:593:24: error: unused function 'add_ptr' [-Werror,-Wunused-function]
```

Full red output: `build/worker-logs/win-compile-red.log`.

```text
$ make TARGET=win -B -j64 win-compile
exit=0
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -MMD -MP -c src/work/work.c -o build/win/rel/src/work/work.o
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -MMD -MP -c src/piece/piece.c -o build/win/rel/src/piece/piece.o
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -MMD -MP -c src/lineidx/lineidx.c -o build/win/rel/src/lineidx/lineidx.o
```

Green compiler output: `build/worker-logs/win-compile-green.log` is a forced rebuild of every listed source with -Werror, linking nothing. The normal unforced target also exited successfully; `build/worker-logs/win-compile-final.log` records its up-to-date result.

Deterministic lineidx clock-role regression, written and run before migration:

```text
$ make -j64 build/tests/lineidx_os_test
exit=0
$ (ulimit -c 0; build/tests/lineidx_os_test)
exit=134
... assertion failed: slice_reads >= 2 && span_calls == 1 && x->refresh_pos == 1
```

The initial attempt to build this new test reported unused fake-clock functions; marking their addresses used made the pre-migration test compile. The red above is the subsequent runtime assertion, not that preliminary compiler error. After migration:

```text
$ make -j64 build/tests/lineidx_os_test
exit=0
$ DISPLAY=:99 build/tests/lineidx_os_test
exit=0
lineidx_os_test: slice yields with frozen accounting; cancellation uses CPU delta
```

The final test also verifies the worker deadline's independent clock domain. Synthetic values derive from lineidx's existing UI/worker slice constants; cancellation checks the explicit delta, as documented in the test. Logs: clock-red-build.log, clock-red.log, clock-green-build.log and clock-green.log under build/worker-logs.

Piece recursive-lock regression was added before migration. To prove it rejects a default mutex, a build-only mutant changes pool_mu initialization from os_rmutex_init to os_mutex_init (cast to the same Linux storage), leaving the production source untouched:

```text
$ gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -c build/worker-logs/piece-nonrecursive.c -o build/worker-logs/piece-nonrecursive.o
$ gcc -pthread build/rel/tests/piece_test.o build/worker-logs/piece-nonrecursive.o build/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb-shm -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/worker-logs/piece-nonrecursive-test
$ DISPLAY=:99 timeout 5s build/worker-logs/piece-nonrecursive-test
exit=124
$ DISPLAY=:99 timeout 5s build/tests/piece_test
exit=0
piece_test: recursive pool release passed
piece_test: checkpoint model passed
piece_test: all passed
```

The (G) diagnostic watchdog is copied from tests/piece_mt_test.c's existing deadlock watchdog; it is not a new performance budget. The first diagnostic link omitted the project's display libraries and failed; the complete link above produced the expected deadlock. Logs: piece-red.log and piece-green.log.

## Verification

```text
$ make -j64 all
exit=0
```

GCC release build, full log: build/worker-logs/linux-all.log; the final fixture-only rebuild is in final-all.log (exit=0). The first full build exposed the legacy test/bench pthread field assumptions described above; the completed run passes after those consumer adaptations.

```text
$ make TARGET=win -j64 all
exit=0
$ make TARGET=win -j64 check
exit=0
== build/win/rel/tests/smoke_test.exe
smoke ok (all checks passed)
== build/win/san/tests/smoke_test.exe
smoke ok (all checks passed)
== build/win/san/tests/smoke_test.exe --expect-asan (must fail)
ASan fired: ok
```

Logs: win-all.log and win-check.log. WSL interop initially failed inside the sandbox with UtilBindVsockAnyPort (win-check-sandbox.log). Automatic approval review timed out on the first escalation; its permitted retry succeeded. The smoke executables ran outside the sandbox, in place, without Windows-drive writes. These are the existing smoke tests; linking/running core Windows suites awaits the parallel OS implementation and W0.5, as the brief specifies.

```text
$ DISPLAY=:99 make -j64 check
exit=2
journal_kill_test: legacy WINDOW parser passed
journal_kill_test: BEGIN/DELETE/pre-COMMIT/post-COMMIT kills with PAD passed
== build/san/tests/journal_retire_test
journal_retire_test:298 FAIL f
journal_retire_test:617 FAIL measurement()==0
make: *** [Makefile:233: check] Error 1
```

Full log: build/worker-logs/linux-check.log (also linux-check-full.log). The failing fopen is the unchanged tests/journal_retire_test.c:297 attempt to open /sys/class/power_supply/BAT0/status. BAT0 is absent on this desktop, as the worker rules explicitly state; git diff for that test is empty. This prevents an honest green unchanged full check. No test was altered or skipped in this make invocation. The coordinator must handle this pre-existing machine-assumption failure separately.

The sandbox attempt had first stopped at base_test because LeakSanitizer cannot run under ptrace (linux-check-sandbox.log, make exit=2). The full retry above ran outside the sandbox with leaks enabled and DISPLAY=:99; display suites retained tools/with_display_lock.sh. To finish coverage after make's stop, build/worker-logs/check-remaining.sh runs every remaining sanitizer executable in lexical order after journal_retire_test, with the same display-lock selection, then both check shell scripts. The continuation exited=1 solely because x11_live_test and x11_order_test could not create private Xvfb sockets. Core suites and both shell checks passed:

```text
$ bash build/worker-logs/check-remaining.sh
exit=1
lineidx_os_test exit=0
lineidx_test exit=0
piece_test exit=0
undo_test exit=0
work_foreground_test exit=0
work_test exit=0
x11_live_test: private Xvfb unavailable
x11_live_test exit=1
x11_order_test: FAIL no private Xvfb
x11_order_test exit=1
shell-replay exit=0
shell-display exit=0
remaining suites exit=1
```

The private-server helper already supports reusing the caller's locked safe display when both DISPLAY and EDIT_DISPLAY name :99. Retrying the failed suites with that existing override passed, with leaks enabled:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 tools/with_display_lock.sh build/san/tests/x11_live_test
exit=0
x11_live_test: ok
$ DISPLAY=:99 EDIT_DISPLAY=:99 tools/with_display_lock.sh build/san/tests/x11_order_test
exit=0
x11_order_test: all ok
```

Logs: linux-check-remaining.log and x11-safe-display.log. Some optional private-server cases in the continuation returned success after failing to start a private server, as their logs show; this is not represented as a green full make check. Use EDIT_DISPLAY=:99 for the coordinator's full retry. The final implicit-active raster fixture adaptation was rebuilt with `make -j64 all build/san/tests/raster_test` (exit=0, final-all.log), and its unchanged capacity/rollback assertions passed with `DISPLAY=:99 tools/with_display_lock.sh build/san/tests/raster_test --review 4` (exit=0, raster-fixture-final.log).

Focused release suites also passed: lineidx_os_test, piece_test, work_test and undo_test (each exit=0; the *_green.log files). undo_test's deliberate child assertion is expected by its existing out-of-band-mutation regression.

Existing touched-module benchmarks were run in tracked mode/default supplied quick modes, with no new thresholds or expectation changes. Commands and results (M) are in bench-results.txt / bench-<module>.log. The common stamp for these logs is [AC, desktop Spiral]; their old harnesses print unknown on this battery-less desktop. Before and during the serial run, uptime is captured in bench-load.txt. Example beginning:

```text
08:28:38 up 45 min, 1 user, load average: 2.56, 4.05, 2.50
```

base, trace, work, piece --quick, piece_reclaim --quick, undo, lineidx --track, layout, view --track: exit=0 for each. scroll --track --probe ran on DISPLAY=:99 through with_display_lock.sh, exit=0 (bench-scroll.log and bench-scroll-load.txt). No before/after performance claim is made from these loaded runs.

## Found, not fixed

- The unchanged journal_retire_test measurement fixture hard-requires BAT0/status; this known battery-less desktop has none. The full make check therefore exits=2 before the later suites. Fixing that journal test is outside this bead.
- Frozen os.h has no residency query for scroll's mincore probe. Linux retains it behind an explicit !defined(_WIN32) guard naming edit-177r.4. Windows uses WORK_BULK, so the resident-copy foreground optimization is pending the coordinator's contract decision.
- Existing layout benchmark rows report tracked misses; scroll's probe reports a tracked unindexed-jump miss and physical cadence remains unsupported. The logs preserve those measurements. No performance tuning belongs to this mechanical migration, and no baseline comparison was measured.
- The default quick piece benchmark skips its opt-in huge-file row, as before.

## Open questions for coordinator

Resolve the unrelated journal_retire_test BAT0 assumption in its own scope, then re-run the unchanged full check with DISPLAY=:99 EDIT_DISPLAY=:99. Decide whether to add an OS residency-query entry in a later contract bead, or accept Windows' conservative bulk lane. Rebase the small shared base and win.mk changes with W0.3. Full linked Windows core-suite verification belongs to W0.5 after os_win.c and the Windows guard land.
