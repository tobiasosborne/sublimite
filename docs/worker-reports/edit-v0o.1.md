# edit-v0o.1 worker report

Implemented the optional raw Wayland platform backend and runtime selection. No `bd` or `git` commands were run. All edits are in this worktree. Acceptance remains open for actual Wayland EGL/pixel checks, recorded G1/G3 measurements and the next Astra comparison; this is not a Tobias hand-over.

## Changes

- New `src/wayland/{plat.h,private.h,dispatch.c,wayland.c,protocol.c}` and generated protocol files under `src/wayland/protocol/`: compositor/xdg-shell, seat, xkbcommon keyboard/compose/repeat, pointer/buttons/framed wheel deltas, asynchronous data-device clipboard, frame callbacks and presentation feedback. Configure dimensions are staged through acknowledgement and independently preserve client choice when unspecified. Incoming clipboard storage preserves the preceding borrowed result until completion; active sends retain their source snapshot. Native input is delivered before simultaneous worker/blink callbacks can end the pump.
- `src/x11/{plat.h,x11.c,clip.c}`: shared backend identity and explicit initialization entry; X11 exports dispatch privately while legacy plat initialization stays X11. `src/main.c` selects before the X safety redirect, with X11 preferred when both environment variables exist. `src/editor/{editor.h,open.c}` carries that choice. `src/raster/raster.c` rejects Wayland instead of opening an unrelated X connection.
- `src/gl/{gl.h,renderer.inc}`: worker-loaded Wayland EGL display/window, worker draw/fence warm-up without a swap, original-frame presentation verification. T5 still comes from the GL fence; frame callbacks do not gate admission or impersonate T6. Matching presented feedback supplies T6; discards fail explicitly.
- `Makefile`: optional client-header probe and independent server-header probe; server libraries are test/fuzzer-only. The explicit bead instruction authorizes this otherwise normally restricted file.
- `tests/{wayland_test.c,wayland_gl_test.c,wayland_bench_test.c,wayland_mock.h}` and the private export mappings in `tests/gl_test.c`; `fuzz/wayland_fuzz.c`; `bench/wayland_bench.c`.
- Documentation: the decision, module statuses, protocol provenance, this report, worklog and handoff.

## Red runs

Functional diagnostics are (M)[AC]; power derives from BAT0 reporting `Charging`. Source locations and compiler counts below derive from the actual diagnostics, not latency budgets. Protocol literals derive from upstream wire definitions; capacities reuse existing X11 constants. Test dimensions/ticks derive from minimum nonzero values and consecutive increments. Benchmark reference values and population derive from the existing perf goal sheet and interaction harness.

The test was written before the backend/header existed:

```text
clang -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -include tests/display_guard.h -MMD -MP -c tests/wayland_test.c -o build/san/tests/wayland_test.o
tests/wayland_test.c:3:10: fatal error: 'wayland/plat.h' file not found
    3 | #include "wayland/plat.h"
      |          ^~~~~~~~~~~~~~~~
1 error generated.
make: *** [Makefile:58: build/san/tests/wayland_test.o] Error 1
```

The simultaneous native-key/work wake test reproduced the ordering defect before its fix:

```text
wayland_test: tests/wayland_test.c:92: void protocol_test(void): Assertion `plat_run_for(&p, &cb, 0) == PLAT_OK && o.priority_key && !o.work' failed.
```

The clipboard lifetime test reproduced replacement of the old result before completion:

```text
wayland_test: tests/wayland_test.c:72: void protocol_test(void): Assertion `retained == bytes && retained_len == len && !memcmp(bytes, "mock clipboard", len)' failed.
```

The independent configure-dimension test was red before staging/applying the negotiated dimensions:

```text
wayland_test: tests/wayland_test.c:41: void protocol_test(void): Assertion `p.width == cfg.width + 1 && p.height == cfg.height' failed.
```

Additional preserved red evidence: `build/wayland/axis-red.log` for modern wheel binding, `parallel-send-red.log` for concurrent MIME requests, `input-red.log` for the allocation test seam, and `bench-red.log` for matching-frame T4 capture.

## Green runs

After implementation, clang ASan/UBSan functional runs, with LeakSanitizer disabled because sandbox ptrace is denied:

```text
ASAN_OPTIONS=detect_leaks=0 build/san/tests/wayland_test
wayland_test: selection and headless lifecycle passed
wayland_test: decoder exercised; allocation count inert under sanitizers
wayland_test: native key precedes simultaneous worker wake
wayland_test: mock configure/ack, xkb keyboard/repeat/focus, blink/work timers, pointer, clipboard receive/send/cancellation, frame/presentation, discard, cleanup and missing-global checks passed
ASAN_OPTIONS=detect_leaks=0 build/san/tests/wayland_gl_test
wayland_gl_test: frame callback is only a hint; mocked fence T5 and matching presentation T6, stale/duplicate/discard checks passed
ASAN_OPTIONS=detect_leaks=0 build/san/tests/wayland_bench_test
wayland_bench_test: first matching frame captures backend T4; observer delay and later caret frames cannot replace it
```

GCC release exercises the real allocator interposition, unlike the sanitizer run:

```text
build/tests/wayland_test
wayland_test: selection and headless lifecycle passed
wayland_test: decoder allocation guard passed
wayland_test: native key precedes simultaneous worker wake
wayland_test: mock configure/ack, xkb keyboard/repeat/focus, blink/work timers, pointer, clipboard receive/send/cancellation, frame/presentation, discard, cleanup and missing-global checks passed
```

`make all` passed with the required C11 warning flags. The optional build using `B=build/without-wayland WAYLAND_AVAILABLE=` built the application and Wayland test stubs successfully. The fuzzer built and its fixed byte-domain/reversed-domain seed executions passed; a mutation campaign was not performed. Replay CLI and display-lock shell contracts passed. Exact subprocess statuses are in `build/wayland/final-targets.tsv`; all focused entries succeeded. The full individual sanitizer inventory is `build/wayland/final-suite-results.tsv`, derived from the sequential run plus the affected-suite reruns.

## Aggregate check and sandbox failures

`make check` was run. The unmodified attempt reached CLI and subsequently failed IPC setup. The final attempt used the existing lock's nonblocking mode (`EDIT_DISPLAY_LOCK_TIMEOUT=0`, a mode rather than an invented wait budget) and stopped on the shared display lock:

```text
base_test: PASS
== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:120: check] Error 1
```

Every built suite was also run individually with sanitizer instrumentation, a private runtime/config directory and `DISPLAY=:99`. All fully runnable suites passed. The following need host reruns:

| Cause | Suites |
| --- | --- |
| X display connection denied | editor_close_test, editor_test, file_kill_test, raster_test, refproto_test, refwin_test, refwin_timeout_test, x11_clip_review_test, x11_clip_test, x11_identity_test, x11_live_test, x11_order_test, x11_stall_test |
| Unix socket bind / peer-credential operations denied | cli_test, editor_large_test, editor_p4_test, ipc_test; editor_test also failed its launcher-wait case |
| Native checks skipped while pure checks passed | gl_test |

Direct socket probes confirmed `Operation not permitted` for both an X connection and a private AF_UNIX bind. `ipc_test` independently reported that same errno during initialization. These IPC restrictions exceed the display-only exception anticipated in the brief; they were not repaired in another module. Some mixed suites therefore did not execute all their later cases. LeakSanitizer also needs the host rerun.

An early individual GL run used an object compiled while its source was being updated and reported a null-platform access. The current source already guarded that fixture; a forced fresh rebuild and rerun passed all pure GL checks with native diagnostics explicitly skipped. The final focused run verified that result again. No sanitizer failure remains in the affected runnable tests.

## What was and was not exercised

The mock uses real Wayland marshalling and socketpair/FD transport, real xkbcommon keymap parsing/translation, real timerfds, real clipboard pipes and real configure/ack/frame/presentation messages. It covers original/stale/duplicate/discarded frame identity, split device/presentation readiness, allocator-free decoding, focus/repeat cancellation, pointer deltas, concurrent sends, received-data lifetime, cleanup and missing globals. A test-only credential shim supplies the known forked peer identity because this sandbox denies SO_PEERCRED even on its private socketpair. No production credential override is linked. The GL integration test explicitly mocks the device fence.

Actual EGL context/swap behavior on Wayland, rendered pixels, hardware fence timings, real compositor focus/occlusion/minimize behavior, clipboard exchange with another desktop client and physical input-to-display latency were not exercised. Primary selection requires an additional protocol and is explicitly unsupported. Output scaling, custom cursor surfaces and client decorations are not implemented by this protocol set.

The benchmark is prepared for explicit `--run PATH [SAMPLES]`, uses the production editor/minimap/GL adapter, validates matching frame identities and records negotiated buffer dimensions. Its G3 workload pairs page navigation with exposure to require a full repaint. The sandbox result was:

```text
wayland_bench: SKIP unmeasured; WAYLAND_DISPLAY absent
```

G1/G3 on actual Wayland remain unmeasured; no timings were fabricated. The coordinator must record them on a compositor with presentation feedback, rerun the blocked suites and LeakSanitizer on the host/Xvfb, then run the next Astra vs Sublime drive on that same build. The absence of an extra frame-callback admission gate and the input-priority regression are verified; equality with Sublime's real latency is not yet established.

## Outside scope, reported without fixing

The current editor sources contain no `plat_clip_*` calls. The transport work therefore does not wire the previously observed application clipboard commands; that remains the editor clipboard bead. Existing IPC display-name length limits can also reject long absolute Wayland socket names. The renderer's existing terminal Skip/discard policy has been retained; actual minimized/occluded Wayland behavior needs the real-compositor drive. No further bead was started.
