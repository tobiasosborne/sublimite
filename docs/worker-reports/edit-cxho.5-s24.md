# edit-cxho.5 — worker report

Scope: X11 platform drop target only. Editor open-path integration remains
`edit-cxho.6`. No `bd` or `git` commands were run; work stayed in this worktree.

## Files touched

- `src/x11/dnd.c`, `src/x11/dnd.h`: asynchronous XDND negotiation and replies.
- `src/x11/x11.c`, `src/x11/private.h`: initialize, dispatch, poll and destroy the target.
- `src/x11/clip.c`, `src/x11/clip.h`: dedicated raw receive slot sharing the
  clipboard worker, INCR implementation, timeout machinery and memory budget;
  independent retained drop data.
- `src/x11/plat.h`: shared `PLAT_EV_DROP` and borrowed `plat_drop_data` API.
- `src/wayland/dispatch.c`, `src/wayland/plat.h`: no-data Wayland accessor stub and note.
- `src/win32/win32_plat.c`: `plat_drop_data` stub, ignoring the platform,
  clearing non-NULL length and returning NULL. Event kind comes from the shared
  platform header. `mk/win.mk` unchanged; no stub source file was needed.
- `tests/x11_dnd_test.c`: hand-written raw-XCB source on the private-Xvfb harness.
- `Makefile`: explicit private-Xvfb header dependency for the new suite; existing
  globs supply its source, sanitizer build and display locking.
- `docs/decisions/edit-cxho.5.md`, this report.

## Red run

The suite was written before implementation. The first build failed because
its new event/accessor did not exist. Exit codes below are process results
(P: success is zero; make reports failed recipes as nonzero). Build parallelism
and compiler flags follow the brief (P). Compiler line locations and error
counts below are observed diagnostics (M)[AC].

```text
$ make -j4 build/san/tests/x11_dnd_test
tests/x11_dnd_test.c:127:20: error: use of undeclared identifier 'PLAT_EV_DROP'
tests/x11_dnd_test.c:128:43: error: call to undeclared function 'plat_drop_data'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
tests/x11_dnd_test.c:128:36: error: incompatible integer to pointer conversion initializing 'const uint8_t *' (aka 'const unsigned char *') with an expression of type 'int' [-Wint-conversion]
tests/x11_dnd_test.c:185:56: error: call to undeclared function 'plat_drop_data'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
tests/x11_dnd_test.c:185:45: error: incompatible integer to pointer conversion initializing 'const uint8_t *' (aka 'const unsigned char *') with an expression of type 'int' [-Wint-conversion]
tests/x11_dnd_test.c:191:45: error: comparison between pointer and integer ('int' and 'const uint8_t *' (aka 'const unsigned char *')) [-Werror,-Wpointer-integer-compare]
6 errors generated.
make: *** [Makefile:187: build/san/tests/x11_dnd_test.o] Error 1
make: *** Waiting for unfinished jobs....
exit=2
```

Full compiler output: `build/edit-cxho.5-dnd-red.log`.

## Green runs and verification

```text
$ make -j4 build/san/tests/x11_dnd_test build/san/tests/x11_clip_test
exit=0

$ make -j4 all
exit=0
```

GCC release build uses the required C11 warning set including `-Werror`.
Sanitizers use clang with ASan/UBSan. The final release log is
`build/edit-cxho.5-all-final.log`.

```text
$ ASAN_OPTIONS=detect_leaks=1 EDIT_X11_STRICT=1 env -u EDIT_DISPLAY build/san/tests/x11_dnd_test
Not charging
 11:36:12 up 1 day, 13:39,  1 user,  load average: 1.82, 1.25, 1.41
x11_dnd_test: ok
exit=0

$ ASAN_OPTIONS=detect_leaks=1 EDIT_X11_STRICT=1 env -u EDIT_DISPLAY build/san/tests/x11_clip_test
Charging
 11:34:01 up 1 day, 13:37,  1 user,  load average: 0.88, 0.91, 1.33
x11_clip_test: ok
exit=0
```

Stamps above are direct BAT0/status and uptime observations (M)[AC]; no elapsed
performance claim is made. Private-Xvfb XKB keysym warnings were nonfatal.
The initial sandboxed attempts exited nonzero because Xvfb could not bind its
sockets and LeakSanitizer could not inspect processes. The successful runs used
the approved unsandboxed execution path, retained leak detection, and started
private servers. Logs: `build/edit-cxho.5-dnd-green-final.log` and
`build/edit-cxho.5-clip-green.log`.

The XDND suite checks advertised awareness, inline and property-based offers,
Status target/accept/action/empty rectangle, exact URI bytes in the event callback,
Finished success and rejection, Leave-before-Drop, unsupported offers, idle peer
timeout, a conflicting source during a transfer, stale timestamp forwarding,
late notification rejection, raw NUL/non-UTF8 preservation, and independent
clipboard/drop buffer lifetimes. INCR payload is the brief's 100 KiB
(P: 100 * 1024 bytes); no new size or timeout budget was introduced.

Full check used a fresh runtime directory and the requested display. The
Makefile runs display suites through `sh tools/with_display_lock.sh` with
`DISPLAY=:99`; `EDIT_DISPLAY` was unset so selection suites kept starting
private Xvfb servers. `EDIT_REFUSE_REAL_DISPLAY=1` was exported throughout.
The runtime directory was removed by an exit trap.

```text
$ test_runtime_dir=$(mktemp -d)
$ env -u EDIT_DISPLAY XDG_RUNTIME_DIR="$test_runtime_dir" DISPLAY=:99 EDIT_REFUSE_REAL_DISPLAY=1 EDIT_X11_STRICT=1 ASAN_OPTIONS=detect_leaks=1 make -j4 check
Charging
 11:36:20 up 1 day, 13:39,  1 user,  load average: 1.84, 1.28, 1.41
check: 112 test binaries passed
== tools/test_replay_cli.sh
== tools/test_display_lock.sh contract
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
exit=0
```

The suite count is the observed make summary (M)[AC], not a new test budget.
The display-lock contract deliberately produces failed nested commands and
lock-timeout diagnostics as negative controls; the overall check passed.
Full log: `build/edit-cxho.5-check.log`. This full run includes the final
`x11_dnd_test` and `x11_clip_test` sanitizer binaries with leak detection.

Filesystem observations around the full check (M)[AC], from `df -h /`:

```text
before:
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  148G  1.6T   9% /
after:
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  151G  1.6T   9% /
```

No GB-scale fixture was added for this bead; its INCR fixture is heap-owned,
freed after the transfer, and shutdown releases platform buffers. The full
check also built the remaining sanitizer binaries in this worktree.

## What remains / outside this bead

- Nemo's actual offered type list is unverified; no live file-manager drag was
  captured. Nemo/Nautilus/Dolphin open-tab verification follows editor wiring.
- `XdndProxy` is intentionally ignored, per the brief.
- Wayland and Win32 return no drop data until their implementation beads.
  Windows was not compiled: this box has no llvm-mingw. The exact Windows
  change is the accessor stub in the existing platform translation unit above.
- No product defect outside this bead was investigated or changed.
