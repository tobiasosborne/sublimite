# edit-177r.8 — worker report

Status: implementation complete; Linux build and full check green; final Windows full check running with an isolated native TEMP root. The standalone Windows wait suite is green in both release and sanitizer modes. Single worker, worktree only; no state-changing git or bd commands.

## Inventory before editing

Read the binding rules and brief, then CLAUDE.md, windows-port.md, edit-177r.2 migration rules, frozen os.h, edit-177r.4, win.mk and the named loop/platform/source headers.

- `open.c` owns the epoll set, registers editor/config/journal pools, IPC and signalfd, and lends epoll readiness to the platform.
- `backend.c` adds the startup pool and retires raster completion registration; `editor.c` arms raster completion and polls the headless set.
- `buffers.c` polls ready tags, reads complete signalfd records and routes the shared file-watch source; `commands.c` and `preferences.c` register that same file service.
- Quiescent pool joins in backend/buffers/open/fif/findpanel/font_size use single-fd poll, with existing millisecond timeouts retained.
- Win32 currently waits for blink, repeat, presentation, clipboard and one work wake. Its native modal service must also observe the new set.
- IPC exposes a Windows wake plus prepare/connect self-spawn launcher entries. File exposes one aggregate Windows watch HANDLE; starts return a service token there.
- Main is POSIX-only (display selection, fork launcher, signalfd, journal placeholder stat and stdio trace dump). D3D11 needs the window attached after platform creation and before worker initialization.

## Design / implementation

Implemented: UI-owned bounded source table; Linux retains one epoll readiness descriptor; Windows platform snapshots native handles through a borrowed callback each wait, so later watch/startup registrations are visible without allocation. Dedicated tags identify the shared file service and close request. See `docs/decisions/edit-177r.8.md`.

## Red / green

Red (M): `/home/tobiasosborne/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -Isrc -c tests/win/editor_wait_test.c -o build/worker-177r8/red-wait.o`, exit 1:

```text
tests/win/editor_wait_test.c:7:10: fatal error: 'editor/wait.h' file not found
1 error generated.
```

Initial green (M): `make TARGET=win -j64 build/win/rel/tests/editor_wait_test.exe`, exit 0; `tools/winrun.sh build/win/rel/tests/editor_wait_test.exe`, exit 0:

```text
editor_wait: work pool passed
editor_wait: close request passed
editor_wait: input passed
```

Expanded release green (M): `tools/winrun.sh build/win/rel/tests/editor_wait_test.exe`, exit 0:

```text
editor_wait: capacity, unregister, borrowed lifetime and allocation-free readiness passed
editor_wait: work pool passed
editor_wait: close request passed
editor_wait: console Ctrl+C bridge passed
editor_wait: console close bridge passed
editor_wait: ipc passed
editor_wait: file watch passed
editor_wait: input passed
editor_wait: WM_CLOSE passed
editor_wait: session close passed
```

The first Windows spawn under the sandbox failed with `UtilBindVsockAnyPort: socket failed 1`; the authorized interop retry with sandbox restrictions lifted succeeded. No auto-review rejection occurred.

Object compile red (M): compiling the owned editor loop TUs with TARGET=win, exit 2, exposed unknown pthread mutex/thread types, undeclared realpath/lstat/aligned_alloc/journal_session_release, and missing O_CLOEXEC/O_NONBLOCK. The same owned editor TUs now compile with production Windows flags (exit 0); `make TARGET=win -j64 win-editor-loop` now compiles main and every owned loop TU in release and sanitizer configurations, exit 0 (M).

Additional Linux regression red (M), `DISPLAY=:96 make -j64 check`, exit 2:

```text
tests/work_test.c:253: FAIL bad == 0
work_test: section2 unaligned_callers=1
work_test: section2 alignment FAIL
work_test: FAIL
make: *** [Makefile:243: check] Error 1
```

Allocation-audit green (M), `DISPLAY=:96 build/san/tests/work_test`, exit 0; Windows `tools/winrun.sh build/win/{rel,san}/tests/work_test.exe`, each exit 0:

```text
work_test: section2 unaligned_callers=0
work_test: section2 alignment ok
work_test: ok
```

## Verification

All measurements below are (M), `[AC, desktop Spiral]`; BAT0 is absent. Logs live under `build/worker-177r8/`.

| Command | Result | Evidence |
|---|---|---|
| `make -j64 all` | exit 0 | `linux-all.log`, final source including the Windows allocation helper |
| `DISPLAY=:96 make -j64 check` | exit 0; **110 test binaries passed** | `linux-check.log`; count comes from make's `check: 110 test binaries passed` summary, including its subsequent display-lock contract checks; LeakSanitizer enabled |
| `DISPLAY=:96 EDITOR_ALLOC_ONLY=null build/tests/editor_test` | exit 0 | `linux-allocator.log`: `editor_test: null 10000 keys mallocs=0 guard=active`; count comes from the existing allocation-case loop |
| `DISPLAY=:96 EDITOR_ALLOC_ONLY=raster tools/with_display_lock.sh build/tests/editor_test` | exit 0 | `linux-raster-allocator.log`: `editor_test: raster 10000 keys mallocs=0 guard=active`; same existing key-count fixture, native raster path |
| `make TARGET=win -j64 win-editor-loop` | exit 0 | `win-editor-loop.log`; production release and sanitizer compilation of main and every owned loop TU; also a Windows-check prerequisite |
| `tools/winrun.sh build/win/rel/tests/editor_wait_test.exe` | exit 0 | `wait-green.log`, all verdicts pasted above |
| `tools/winrun.sh build/win/san/tests/editor_wait_test.exe` | exit 0 | `wait-san-green.log`, same verdicts |
| `make TARGET=win -j64 check` | isolated-TEMP full retry running | `windows-check.log`; all tests unchanged, no case disabled |
| `git diff --check` | exit 0 | final source and docs checked |

Linux full-check evidence:

```text
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
editor_test: null hl-on 10000 keys mallocs=0 guard=ASan-inert
editor_test: all passed
work_test: section2 unaligned_callers=0
check: 110 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

The first sandboxed Linux run stopped at `base_test` because LeakSanitizer could not inspect processes under ptrace. The full retry lifted that restriction and retained LeakSanitizer. Windows runs use `tools/winrun.sh` with WSL interop access lifted; no test or runtime DLL was copied to a Windows drive by Linux.

The first full Linux and Windows runs caught the allocation-source audit regression above. The Linux final retry is green. The first Windows pass recorded that audit as its only failing binary; every other binary and the ASan negative control passed. Its log is `windows-check-before-alignment-fix.log`; the Linux predecessor is `linux-check-before-alignment-fix.log`.

The next full Windows run had exactly one failing binary: sanitizer `journal_kill_test`, `journal_hot_exit_test:94 FAIL (os_mkdir(root))==OS_OK native=183`. That macro reports its include's diagnostic prefix; the failing source is `tests/win/journal_kill_test.c`'s PID-named random-campaign root. Native error 183 is an existing directory, before the seeded campaign can start. The same unchanged sanitizer binary passed a standalone retry (`win-journal-retry.log`, exit 0). The full run is preserved as `windows-check-temp-collision.log`; all other binaries and the ASan negative control passed.

The final full Windows check directs Windows `TMP` and `TEMP` to a fresh `sublimite-test-editor-check-<guid>` directory under the normal Windows TEMP. A Windows PowerShell process created it through `tools/winrun.sh`; Linux wrote nothing on the Windows drive. WSLENV forwards both native path values. This isolates the full suite from old PID-named scratch directories without modifying journal tests, skipping cases or disabling sanitizers. The same Windows process mechanism will remove only this owned root afterward. Its name is recorded in `windows-temp-root.txt`. Approval review timed out on the first directory-creation attempt without a safety finding; the explicitly permitted retry was approved and succeeded.

Benchmarks: `DISPLAY=:96 build/bench/config_bench` and the existing `bench/editor_bench.args` fixture (`--track --no-idle --no-p4 --serial-only --keys=64`) both exit 0. The sample count derives from that repository fixture. All null/raster/GL-fallback serial rows report structural OK and allocations=0; these TRACK rows do not supply a new statistical G1 verdict. The first sandboxed editor-benchmark attempt could not initialize its display backend; the unsandboxed retry passed. Logs: `config-bench.log`, `editor-bench.log`. The existing harness prints `power=unknown` when BAT0 is absent; this report supplies the measured AC stamp.

Run-start `uptime` evidence, all `[AC, desktop Spiral]`:

- Inventory: `19:09:57 up 11:17, 1 user, load average: 1.23, 1.29, 0.68`.
- Initial Windows full check: `19:26:41 up 11:34, 1 user, load average: 7.56, 6.18, 4.68`.
- Initial unsandboxed Linux check: `19:27:32 up 11:35, 1 user, load average: 3.40, 5.26, 4.45`.
- Final release build/allocator: `19:35:46 up 11:43, 1 user, load average: 6.08, 5.47, 4.72`.
- Benchmark retry: `19:37:28 up 11:45, 1 user, load average: 4.51, 5.24, 4.73`.
- Final Linux check: `19:42:19 up 11:50, 1 user, load average: 1.40, 2.84, 3.84`.
- Second Windows full check: `19:51:59 up 11:59, 1 user, load average: 1.21, 2.07, 3.07`.
- Native raster allocator: `20:02:58 up 12:10, 1 user, load average: 0.23, 0.44, 1.65`.
- Journal standalone retry and final isolated check loads are in `win-journal-retry-load.txt` and `windows-check-load.txt`.

The owned loop files have no remaining epoll/poll or legacy work/config fd accessor calls. Native epoll is confined to `wait_posix.c`; Linux retains its original signal record and platform readiness behavior.

## Found, not fixed

- The journal random-campaign fixture uses only a PID in its global TEMP root name; a stale directory caused the unrelated full-check failure above. Test source was not changed. The full retry uses an isolated native TEMP root to avoid the collision.
- Sibling-owned dependency/session/recovery sources remain unported in this worktree by design. In particular, W2.4b must reconcile its portable `session_lock` / `busy_source_lease` representation and cleanup with open.c and this Windows `editor_config.session_lease` handoff. No application executable was linked or run; W2.4d owns that integration.
- The existing Windows journal summary still says file integration is skipped because file is not ported, although this base already includes the file module. The unrelated summary wording was left unchanged.
- The benchmark power helper labels this battery-less desktop unknown; the report stamps the measurements AC. The GL benchmark uses its existing raster fallback on this display; no renderer behavior was changed.

## Open questions / scope notes

A typed Windows close wake requires a minimal `editor_config` addition in `src/editor/editor.h`, although the allowed-file list omits that header; it is part of the brief's required main-to-editor close-request plumbing. `session_lease` is additionally typed as `const os_file *` on Windows for the main-to-session portable lease handoff; Linux retains its existing fd API. The private mutex and font-worker fields required `os_mutex` / `uint64_t` to compile the owned loop TUs; these small shared-header edits are local and need to be reconciled with the sibling's session migration. Windows-only filesystem branches in buffers/commands and os_stat use in open resolve blockers in the owned files; no sibling-owned source was edited.

## Files touched

- New `src/editor/wait.h`, `wait_posix.c`, `wait_win.c`: borrowed source table, readiness tags, native platform bridge and bounded registration.
- `src/editor/backend.c`, `buffers.c`, `editor.c`, `open.c`, `commands.c`, `preferences.c`, `fif.c`, `findpanel.c`, `font_size.c`: replace native waits/registrations; minimal Windows-only compile and D3D11 lifecycle branches.
- Shared `src/editor/private.h`: wait fields, portable mutex/font-worker identity, shutdown helper declaration. `src/editor/editor.h`: typed Windows-only close-wake and session-lease handoffs.
- `src/config/config.h`, `runtime.c`: only `config_runtime_wake` declaration/function.
- `src/x11/plat.h`, `src/win32/private.h`, `win32_plat.c`: Windows-only source snapshot callback, normal/modal wait integration, session-close events; Linux platform layout/sources unchanged.
- New `src/win32/close.h`, `win32_close_win.c`: no-global console-close bridge and cleanup.
- `src/main.c`: Windows command-line conversion, launcher, close service, platform selection and portable trace dump/placeholder stat.
- `tests/win/editor_wait_test.c`, `mk/win.mk`: native integration tests and release/sanitizer object-only loop check; loop check is a Windows-check dependency. Existing positional platform-fixture warning exception is confined to its test compiler recipes.
- This report and `docs/decisions/edit-177r.8.md`.

No main checkout, os.h, HANDOFF.md, worklog, dependency-module migration or sibling-owned session/recovery source was edited. No state-changing git or bd command was run.
