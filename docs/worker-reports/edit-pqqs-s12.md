# edit-pqqs-s12 worker report

Verdict: done.

## Change

`plat` now holds only the backend tag, opaque owner, dimensions, focus/quit state and iteration counter. X11 fields live in `src/x11/private.h`; Wayland's connection, timers, presentation availability and timestamp live in its existing private allocation. The inventory was recorded in `docs/decisions/edit-pqqs.md` before implementation; its final table gives original read/write locations and destinations.

`plat_native_x11()` returns the renderer's typed connection/window/visual/depth/ARGB snapshot using standard C types. `plat_present_available()` retains the renderer's existing availability check; `wayland_display()` complements the existing Wayland native accessors. Renderer native reads remain in initialization. Existing white-box fixtures use `x11_private()` for input/clipboard/XI/protocol state injection. Native renderer test reads use the public snapshot. Fixture copies explicitly copy their private blocks, preserving independent fake windows.

`plat_config.work_wake` borrows `const struct os_wake *`; native loops call `os_wake_fd()` internally and do not consume or own the wake. Worker-pool callers use `work_pool_wake()`. The editor's existing input is an epoll aggregate, not just the pool's eventfd. A borrowed editor-owned `os_wake` view preserves exactly that descriptor and all IPC/config/watch/signal/worker readiness. It is never signalled, drained or closed as an `os_wake`; the existing editor epoll lifecycle still owns it. Replacing that aggregate with only the worker wake would change Linux behavior.

X11 allocates its private block once at init after connecting, and frees it after native resources at shutdown; existing input/XI/clipboard allocations remain. Wayland extends its existing allocation. No allocation was added to input → submit. Storage evidence and its compiler derivation are in the decision doc.

## Mechanical backend audit

The event-loop, native request, clipboard, repeat and input-ordering code changes only field access. `src/x11/input.c` and `src/x11/xi2.c` are unchanged: neither accesses `plat` fields. Exceptions required by the representation/accessor change are:

- `src/x11/x11.c`: private include; `x11_plat_set_repeat` and `x11_plat_poll_event` empty-owner checks retain the former empty-input behavior; `x11_plat_init` allocates/initializes the owner and handles allocation failure, and translates the borrowed wake with `os_wake_fd`; `x11_plat_set_present_events` retains its missing-connection rejection through the owner; `x11_plat_shutdown` handles an absent owner and releases it after native cleanup.
- `src/x11/clip.c`: private include and `CL`'s absent-owner mapping to NULL retain the former empty-clipboard behavior. All protocol work is field-access substitution.
- `src/wayland/wayland.c`: private state declarations and OS include; owner allocation stores the connection after allocation and explicitly disconnects on allocation failure; descriptor sentinels move into that owner; shutdown delays freeing the owner until its descriptors and connection are closed. Empty-state guards replace access to former public descriptor sentinels. New native-display/presentation accessors have matching dependency-free stubs. No native event/poll/protocol ordering changed.
- `src/wayland/dispatch.c`: new native snapshot and presentation-availability accessors.

Final audit anchors: `src/x11/x11.c:2,469,475,485,497,572,593,818,831`; `src/x11/clip.c:2,130`; `src/wayland/wayland.c:42,91,437,448,455,500,501,739,764,775,788,789`; `src/wayland/dispatch.c:96,103`. These identify the includes, owner guards/lifecycle, wake adaptation and new accessors; every other changed backend line is a field-access substitution. No input translation, rendering algorithm or test expectation about observable editor behavior changed. Resource-unwind tests now assert a NULL opaque owner rather than removed public resource fields.

## Red runs

Before code edits, the inventory/contract grep:

```sh
rg -n 'xcb|xkb|timerfd|eventfd|\b(conn|win|colormap|visual|depth|argb|present_ok|present_opcode|present_eid|wm_protocols|wm_delete|timer_fd|work_fd|repeat_fd|in|xi|clip|last_time|xkb_event|xkb_dev|core_wheel|xi_scroll_ms|work_eventfd)\b' src/x11/plat.h
```

Exit `0` (M: matches, therefore red). Relevant original output:

```text
51:    uint32_t keysym;        /* KEY: xkb keysym with the event's modifiers applied */
67:    void (*on_blink)(void *ud);                       /* timerfd fired */
68:    void (*on_work)(void *ud);                        /* work eventfd readable */
76:    int work_eventfd;       /* -1 if none */
88:    void *conn;             /* X11: xcb_connection_t*; Wayland: wl_display* */
89:    uint32_t win, colormap, visual;
95:    int timer_fd, work_fd;
99:    int repeat_fd;          /* timerfd for our key repeat */
100:    void *in;               /* x11_input* (xkb state, repeat, queue); allocated at init, not on the typing path */
104:    uint8_t xkb_event;      /* XKB extension first-event code, 0 if none */
105:    int32_t xkb_dev;        /* core keyboard device id */
```

Full output: `build/edit-pqqs/contract-red.log`. This initial inventory grep deliberately includes native field names; the green type/fd grep below permits those names in the typed native snapshot.

After changing only `plat_config` to carry the wake pointer, before backend implementation:

```text
$ make build/rel/src/x11/x11.o
src/x11/x11.c: In function ‘x11_plat_init’:
src/x11/x11.c:569:21: error: ‘plat_config’ has no member named ‘work_eventfd’
  569 |     p->work_fd = cfg->work_eventfd;
make: *** [Makefile:174: build/rel/src/x11/x11.o] Error 1
exit: 2
```

(M: compiler exit/output) Full output: `build/edit-pqqs/wake-red.log`.

## Green checks and environment

(M) [AC, desktop Spiral]. `/sys/class/power_supply/BAT0` is absent. Initial `uptime`: `09:02:25 up 1:19, 1 user, load average: 7.22, 3.59, 2.58`. During verification: `09:08:39 up 1:25, 1 user, load average: 3.97, 4.43, 3.33`. All exit codes below are measured process results; suite counts quoted below are produced by the Makefile, derived from its discovered test list.

```text
$ rg -n 'xcb|xkb|timerfd|eventfd|\b[a-zA-Z_]*_fd\b|\b(pid_t|ssize_t|timespec|pollfd)\b' src/x11/plat.h
(no output)
exit: 1 (no matches; green)

$ make build/rel/src/x11/x11.o
[gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion ... -c src/x11/x11.c]
exit: 0

$ make -j64 all
exit: 0

$ make -j64 build/fuzz/fuzz/gl_fuzz.o build/fuzz/fuzz/x11_input_fuzz.o build/fuzz/fuzz/wayland_fuzz.o
exit: 0
```

The Wayland dependency-free stub also compiles with `gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -Isrc -c src/wayland/wayland.c -o build/edit-pqqs/wayland-no-deps.o` (exit `0`). The header also passes an isolated `gcc -std=c11 -pedantic -Wall -Wextra -Werror -Isrc -x c -fsyntax-only -` compile containing only `#include "x11/plat.h"` and a zero-initialized `plat` (exit `0`). `git diff --check` passes (exit `0`). Release output: `build/edit-pqqs/all.log`; wake compile: `build/edit-pqqs/wake-green.log`; fuzz object output: `build/edit-pqqs/fuzz-compile.log`.

The first full sanitizer command, `DISPLAY=:99 make -j64 check`, built successfully but stopped in `base_test` under the sandbox:

```text
== build/san/tests/base_test
==839==LeakSanitizer has encountered a fatal error.
==839==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:233: check] Error 1
exit: 2
```

The authorized retry lifts the sandbox restriction and explicitly keeps leak detection on:

```sh
ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99 make -j64 check
```

Full output: `build/edit-pqqs/check.log`. Display suites run through the Makefile's `tools/with_display_lock.sh`; no display `:0` use. Final measured exit code: `0`. GCC reports `13.3.0`; sanitizer Clang reports `18.1.3`. Final `uptime`: `09:22:21 up 1:39, 1 user, load average: 2.31, 1.96, 2.31` [AC, desktop Spiral].

Relevant summary lines (verbatim):

```text
editor_test: all passed
gl_test: coherent mapped cells readback PASS (direct writes, triple ring wrap, retained strip)
gl_test: PASS (snapshot law 2, failure cleanup, frozen grid/adapter checks; native lifecycle SKIP)
raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)
refwin_test: PASS 12 editor/reference pairs, complete monotonic CSV, incomplete/wrong-frame runs rejected (Xvfb phase approximate)
wayland_test: selection and headless lifecycle passed
wayland_test: mock configure/ack, xkb keyboard/repeat/focus, blink/work timers, pointer, clipboard receive/send/cancellation, frame/presentation, discard, cleanup and missing-global checks passed
x11_clip_test: ok
x11_live_test: ok
x11_order_test: all ok
x11_xi2_test: ok
check: 104 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
make check exit: 0
```

The quoted reference-pair count and suite count are (M), emitted by the runner from completed pairs/discovered binaries, respectively. GL readback used llvmpipe; the suite explicitly reports native lifecycle SKIP on this Xvfb environment. This is a passing software-display verification, not evidence of a hardware production GL lifecycle. No leak checks were disabled. The display-lock contract intentionally prints timeout/error diagnostics from rejected nested commands, then reports PASS; the full make result is successful.

## Files touched

- `bench/file_bench.c`
- `bench/prewake_bench.c`
- `bench/raster_bench.c`
- `bench/x11_bench.c`
- `docs/decisions/edit-pqqs.md`
- `docs/worker-reports/edit-pqqs-s12.md`
- `fuzz/gl_fuzz.c`
- `fuzz/wayland_fuzz.c`
- `fuzz/x11_input_fuzz.c`
- `src/editor/highlight.c`
- `src/editor/open.c`
- `src/editor/private.h`
- `src/gl/gl_driver.h`
- `src/gl/renderer.inc`
- `src/raster/raster.c`
- `src/wayland/dispatch.c`
- `src/wayland/plat.h`
- `src/wayland/private.h`
- `src/wayland/wayland.c`
- `src/x11/clip.c`
- `src/x11/plat.h`
- `src/x11/private.h`
- `src/x11/x11.c`
- `src/x11/x11_test_main.c.in`
- `tests/editor_close_test.c`
- `tests/editor_test.c`
- `tests/file_kill_test.c`
- `tests/gl_test.c`
- `tests/raster_test.c`
- `tests/refwin_test.c`
- `tests/wayland_gl_test.c`
- `tests/wayland_test.c`
- `tests/x11_clip_review_test.c`
- `tests/x11_clip_test.c`
- `tests/x11_font_size_test.c`
- `tests/x11_identity_test.c`
- `tests/x11_live_test.c`
- `tests/x11_order_test.c`
- `tests/x11_stall_test.c`
- `tests/x11_test_main.c.in`
- `tools/refwin.c`

## Found, not fixed / coordinator handoff

- `edit-v4ee` has not landed here. `src/editor/highlight.c:31` now reads the native connection through the accessor; its RandR refresh query remains. Reconcile that small accessor-line conflict when integrating the parallel platform-query migration.
- `src/editor/display_size.c` still directly connects to XCB and queries RandR/resources; `src/editor/clip.c` uses `x11_clip_buf` and X11 clipboard worker APIs. Editor input/panels still include XKB keysym constants, as the Windows design explicitly anticipates. These are future port work, outside this bead.
- `src/gl/renderer.inc` still uses XCB/Present/DRI3, EGL and a private native connection; its POSIX polling/worker compatibility accessor remains. This bead hides platform storage without porting that renderer.
- `plat_set_repeat` and the `repeat` event flag describe backend-owned repeat, and `PLAT_CLIP_PRIMARY` is X11-shaped. The API's raw keycode/state, server timestamps and presentation-completion terminology also retain current native semantics. The Windows implementation must follow the Windows design's mappings rather than copy X11 protocol behavior.
- The editor's borrowed epoll wake view must be replaced as part of the editor wait-set OS migration in W2, while preserving every existing readiness source. It is deliberately borrowed and poll-only here. No work-pool implementation changes were made.
- The X11 reference tool and white-box tests still use the private typed accessor for protocol metadata/fault injection. They remain X11-specific consumers; renderer production code uses only public accessors.

No unrelated defects were changed. Coordinator decisions: integrate the parallel RandR work and carry the aggregate-wake migration into W2; no new platform policy decision is required by this bead.
