# edit-177r.7 — worker report

Verdict: **partial**. Scoped filesystem migrations and Windows object gate are complete, Linux release/full sanitizer checks pass. The unconditional lock-field and creator-lease API migration requires excluded open.c/editor.h/private.h consumers; no scope expansion was received, so that integration remains for the coordinator. Windows full check has an unrelated release platform timeout; isolated retry passes. Inventory recorded before production edits in `docs/decisions/edit-177r.7.md` and `build/worker-logs/edit-177r.7-inventory.txt`.

Environment: [AC, desktop Spiral]; BAT0 absent (M: shell test). Initial `uptime`: 19:10:58 up 11:18, 1 user, load average: 1.62, 1.33, 0.72. Actual command output is authoritative in the run logs; later verification records current load.

## Red / green

Added object-only `make TARGET=win win-editor-fs` gate before implementation. Red (M), exit 2: `make -j64 TARGET=win win-editor-fs`; log `build/worker-logs/edit-177r.7-red.log`. Relevant failures: `poll.h file not found`, undeclared `O_DIRECTORY` / `O_CLOEXEC`, unknown `pthread_mutex_t` / `pthread_t`, undeclared `pread`, `fsync`, `realpath`.

Red excerpt (M), [AC, desktop Spiral], command `make -j64 TARGET=win win-editor-fs`, exit 2:

```text
src/editor/runtime.c:42:39: error: use of undeclared identifier 'O_DIRECTORY'
src/editor/private.h:33:5: error: unknown type name 'pthread_mutex_t'
src/editor/recovery.c:7:10: fatal error: 'poll.h' file not found
src/editor/restore.c:207:23: error: call to undeclared function 'pread'
```

Green (M), same object gate and warning flags, exit 0; complete compiler lines in the green logs named below. The initial green log contains successful `-c src/editor/{recovery,restore,runtime,retain,session}.c -o build/win/rel/src/editor/<unit>.o` invocations; the final green log recompiles recovery/session after path normalization. No warning suppression is used.

Green excerpt (M), command `make -j64 TARGET=win win-editor-fs`, exit 0:

```text
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -c src/editor/recovery.c -o build/win/rel/src/editor/recovery.o
/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -O2 -g -msse2 -Isrc -Ivendor/tree-sitter/lib/include -Ivendor/tree-sitter/lib/src -MMD -MP -c src/editor/session.c -o build/win/rel/src/editor/session.o
```

## Changes / files

- mk/win.mk: only the local win-editor-fs object gate.
- Decision document: full pre-edit call inventory and migration mapping.
- recovery/restore/runtime/retain/session.c: file operations, directory iteration and sync, exclusive lease acquisition and transfer through os.h; session mutex and yield through os.h. Existing standalone recovery wait now uses os_wake_wait on work_pool_wake.
- private.h: only session_lock and busy_source_lease changed, conditional os_file on Windows / legacy int on Linux. Excluded open.c consumers left intact.
- tests/editor_recovery_test.c: portable bulk-read interception; strict rewrite outcomes retained.
- Post-migration call scan: `build/worker-logs/edit-177r.7-inventory-after.txt`; no direct calls from the inventoried POSIX families remain (M: source scan).

## Verification

- Windows object gate green (M), exit 0: `make -j64 TARGET=win win-editor-fs`, `build/worker-logs/edit-177r.7-object-progress.log` (all named units compiled), and `build/worker-logs/edit-177r.7-green.log` (later touched units recompiled).
- Linux initial release build caught the old `read` interception in editor_recovery_test as unused after migration; updated that seam to intercept os_file_read with identical before/mixed-read rewrite assertions. Release rerun passed.
- Linux `make -j64 all`: exit 0 (M); log `build/worker-logs/edit-177r.7-linux-all.log`. Sanitizer build passed, but the sandboxed run stopped exit 2 at base_test with `LeakSanitizer has encountered a fatal error` / `LeakSanitizer does not work under ptrace`. Full check retried outside the sandbox with DISPLAY=:98 and default LeakSanitizer enabled.
- Unsandboxed Linux full check stopped exit 2 in os_test after 62 suites started (M: count of suite markers), with the scoped editor/recovery/session/hot-exit cases already passed. Failure: `tests/os_test.c:1406: [fault] FAIL os_proc_wait ... OS_E_TIMEDOUT`; stray-fault ASan child finished reporting after its parent's guard expired. Preserved log: `build/worker-logs/edit-177r.7-linux-check-os-timeout.log`. Isolated retry also timed out. Diagnostic comparison: `ASAN_OPTIONS=symbolize=0 OS_TEST_CASE=fault` passed (M: 29.6 ms in log); then `DEBUGINFOD_URLS= OS_TEST_CASE=fault` passed with full symbolization (M: 247.0 ms). This implicates external debug-info lookup, not editor code. Logs: `edit-177r.7-os-fault-retry.log`, `edit-177r.7-os-fault-nosymbol.log`, `edit-177r.7-os-fault-nodebuginfod.log`, under build/worker-logs. Final full check `DISPLAY=:98 DEBUGINFOD_URLS= make -j64 check` outside the sandbox exited 0 (M), with `check: 110 test binaries passed` plus replay CLI and display-lock shell contracts passed; ASan, UBSan, LeakSanitizer and stack symbolization remain enabled. No OS source or test changed.
- Initial Windows check compiled binaries, then stopped exit 2 before tests: `win_reparse_fixture: cannot read %TEMP%`. Authorized WSL interop retry outside the sandbox completed all release and sanitizer binaries. Release `win32_plat_test.exe` failed its foreign clipboard helper readiness/exit assertions (`tests/win/win32_plat_test.c:462,468`); remaining release RESULT lines are zero. This was missed in the early tail-only progress scan; a full RESULT scan found it. No platform code/test changed. Full log at `build/worker-logs/edit-177r.7-win-check.log`. Final full-check exit 2 (M): 48/49 release binaries passed and 49/49 sanitizer binaries passed (M: counted RESULT lines); ASan negative control passed. The only nonzero RESULT was the release platform helper timeout. The same sanitizer case passed, and the isolated release retry `tools/winrun.sh build/win/rel/tests/win32_plat_test.exe clip-foreign-contention` exited 0: `win32_plat_test: PASS (0 failures)`. Retry log: `build/worker-logs/edit-177r.7-win-clipboard-retry.log`.
- Verification load (M), [AC, desktop Spiral]: `19:15:09 up 11:23, 1 user, load average: 24.89, 7.19, 2.84`. Measured during parallel builds. Final verification load (M), [AC, desktop Spiral]: `19:44:07 up 11:52, 1 user, load average: 2.44, 2.77, 3.70`.

## Found, not fixed / coordinator questions

- recovery.c contained a standalone poll acquisition wait; replaced it with os_wake_wait (same work wake and mailbox-drain ordering); no event loop edited.
- Requested unconditional lock field changes have consumers outside the permitted files; clarification requested. No excluded consumer edited. Windows fields are os_file; Linux remains integer-compatible until open.c/editor.h migrate. Windows rejects non-NULL legacy int creator leases with EDITOR_ERR_ARG; it cannot safely convert them to HANDLEs. Coordinator must migrate editor_config.session_lease and lifecycle consumers together.
- Unported private.h pthread fields need pthread type declarations: Windows-only pthread.h includes in the scoped editor units allow object compilation; no pthread calls remain in these units. W2.4c/d still owns the unrelated fields.
- Portable deferred lease transfer uses editor_restore_defer_file, declared locally in recovery.c because private.h edits are restricted to the two fields. Existing Linux editor_restore_defer remains a compatibility wrapper. Move the portable declaration into private.h during integration.
- Windows release platform suite: foreign clipboard helper `WaitForSingleObject(c->ready,3000)==WAIT_OBJECT_0` and child exit-code assertions failed; unrelated to scoped filesystem objects (which are not linked into this Windows check). The isolated release retry passed after the full run. Original full-check failure is preserved; no source/test changes made.

## Linux final green (M)

[AC, desktop Spiral]. Release `make -j64 all`: exit 0. Full check `DISPLAY=:98 DEBUGINFOD_URLS= make -j64 check`: exit 0. Linux ASan, UBSan and LeakSanitizer are enabled; symbolization is enabled, external debug-info downloading alone is disabled. The display suites use the existing display-lock wrapper. Log: `build/worker-logs/edit-177r.7-linux-check.log`.

```text
check: 110 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

Suite count (M): 110 is the Makefile's final TEST_SAN count, printed after every binary returned success. Nested display-lock timeout/error lines are expected negative controls; the outer contract and full make check exit successfully. `git diff --check`: exit 0 (M).

## Windows full-check summary (M)

`make -j64 TARGET=win check` exited 2. The recipe exits before its success-only Windows summary echo lines when any binary fails. Actual final result lines:

```text
RESULT build/win/rel/tests/win32_plat_test.exe exit=1
RESULT build/win/san/tests/win32_plat_test.exe exit=0
ASan fired: ok
make: *** [mk/win.mk:205: check] Error 1
```

Derived summary (M: count RESULT lines): release 48 passed + 1 failed = 49; sanitizer 49 passed + 0 failed = 49. Isolated release clipboard case retry: exit 0, `win32_plat_test: PASS (0 failures)`.
