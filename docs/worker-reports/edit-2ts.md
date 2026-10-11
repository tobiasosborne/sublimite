# edit-2ts — worker report

Implemented startup tracing and removed GPU discovery from the raster first-paint dependency chain. The headless startup regressions and gcc release build pass. Full acceptance remains with the coordinator: native startup/scanout is unmeasured here, and the full sanitizer run has socket-blocked suites plus an outside-scope reload/wait failure described below. No `bd` or `git` command was run; source work stayed in this worktree. The prohibited input, paint, clip, save and large implementation files were not edited.

## Changes and ownership

- `src/editor/backend.c`, `open.c`, `editor.c`, `private.h`, `editor.h`: initialize raster first, or null for a headless GPU descriptor. Initialize the requested GPU on an independent discovery pool, preserving the ordinary bulk lane for file-prefix work. Keep GPU state/arguments exclusively owned until mailbox receipt **and physical completion**. Adopt only after bootstrap presentation and retirement. Retain raster resources through close rather than joining raster workers during adoption. The existing failure diagnostic prints once on the worker; no retries or discovery timer are added. Drain late discovery notifications after selection to prevent idle epoll spinning.
- `src/render/render.h`: `background_init` and an optional worker-only `finish_init` phase express that lifecycle. The finishing worker runs after bootstrap retirement and before GPU adoption; typing continues on raster while it runs.
- `src/gl/renderer.inc`: reject absent DRI3 before library loading, trace the loader calls, and warm draw state without swapping diagnostic frames over raster text. Subscribe to private Present events in the finishing worker, after the first raster serial has retired. This avoids confusing a queued bootstrap completion with the first EGL swap. Strict initialization retains its existing warm-swap proof; background GL validates its first actual frame through the existing fence/PIXMAP completion/error machinery.
- `src/main.c`, `src/trace/trace.h`, `trace.c`, `trace_fmt.h`, `tools/tracedump.c`: retain process entry, display connect, EGL probe start/end, raster bootstrap/fallback, atlas ready, file prefix, first layout, first submit/present, and library-load start/end. Startup stamps survive frame-ring wrap and use appended event IDs in the existing dump format. `tracedump` prints a `startup` section with absolute timestamps and elapsed-time arithmetic.
- `tests/startup_test.c`: actual file-prefix acquisition, visible leading-word cells, held failing probe, successful adoption, held worker finishing, typing allocation guards, late notification drainage, retained-stamp round-trip/reset. The successful case also follows the application's empty-editor-then-open startup shape.
- `tests/editor_test.c`, `bench/editor_bench.c`: diagnostic capture, allocator fixtures, backend labels and `--require-gl` wait for **final selection** before making assertions or measurements. This is harness setup; the application paints during discovery.
- Decision/status documentation: `docs/decisions/edit-2ts.md`, editor/GL/trace `STATUS.md`, this report, worklog and HANDOFF entry.

## Where the observed delay can go

Before this change, `editor_open` submitted GL initialization to a worker but waited for its result and physical return before file acquisition. Thus a driver/loader stall was still a first-paint stall. `dlopen` of EGL/GL, EGL initialization and native round trips have no application deadline. The Present warm-swap probe's existing polling deadline is (E, code bound) **1500000000 ns / 1000000000 ns per second = 1.5 s** per attempted swap; an absent completion aborts at that attempt. Its native calls and OS scheduling can add further delay. The quiescent initializer wait was likewise unbounded. The ASCII atlas is checked-in data selected by pointer, not baked at launch. File-prefix acquisition, metadata, layout/composition and raster initialization/presentation remain necessary first-paint work, now observable separately.

The largest stage proved here is the synchronous GPU wait: holding a failing probe prevents any submit before the fix, and does not prevent prefix acquisition, text presentation or typing after the fix. This is a dependency proof, **not attribution of the Astra video interval**. That attribution requires the next coordinator trace/drive.

No production deadline was added. The (P, control-flow) derivation of no UI wait for discovery is: independent pool + nonblocking receipt/physical-completion tests + return to raster work when pending. The only watchdog reused in tests and benchmark setup is (G, existing harness liveness) **5000000000 ns / 1000000000 ns per second = 5 s**, from the existing editor settle watchdog. It is not a startup performance gate. Strict diagnostic polling cadence and native completion deadlines are unchanged.

`process_start` is main entry, excluding process creation and pre-main loader work. `file_prefix_read` is UI observation of PREFIX_READY, not a separate read-syscall timestamp. `first_layout` can describe initial empty layout in empty-editor startup. `first_present` is backend acceptance; null completion does not measure scanout. Missing native stages are reported as absent. Harness origins precede fixture preparation/headless open and are not comparable with Sublime/video startup.

## Red run, before implementation

Command: `make build/san/tests/startup_test`, then `ASAN_OPTIONS=detect_leaks=0 build/san/tests/startup_test`.
The watchdog is the inherited bound derived above. Submit count is the submit-hook count; error codes are the public module enums. The original headless GPU also lacked a working headless fallback; the timeout and absent submits expose the synchronous dependency.

```text
startup measurement stamp: BAT0=Discharging
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
startup_test: FAIL editor_open=-9; probe timed_out=1; first submits=0
```

Additional red regressions caught late-notification spinning and omission of the worker finishing phase:

```text
startup measurement stamp (M)[bat]: BAT0=Discharging; origin=headless open harness; ms=ns/1000000 (SI)
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
startup_test: FAIL poll(&ready, 1, 0) == 0
startup_test: FAIL held_case(false) == 0
```

```text
startup measurement stamp (M)[bat]: BAT0=Discharging; origin=headless open harness; ms=ns/1000000 (SI)
startup_test: FAIL atomic_load(&p.attached)
startup_test: FAIL held_case(true) == 0
```

## Green run, final source

Command: `ASAN_OPTIONS=detect_leaks=0 build/san/tests/startup_test` — success. Every stage value below is (M) with the printed power stamp; elapsed milliseconds derive from the absolute monotonic timestamp subtraction divided by the SI conversion shown in the output. Allocation counts derive from allocator interposition between guard begin/end; this sanitizer guard is inert, so the release run below is also required.

```text
startup measurement stamp (M)[bat]: BAT0=Discharging; origin=headless open harness; ms=ns/1000000 (SI)
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=70979355223833 elapsed_ms=0.000000
  display_connect      not recorded
  egl_probe_start      ns=70979358243763 elapsed_ms=3.019930
  egl_probe_end        ns=70979359221680 elapsed_ms=3.997847
  raster_fallback      ns=70979357801186 elapsed_ms=2.577353
  font_atlas_ready     ns=70979355801522 elapsed_ms=0.577689
  file_prefix_read     ns=70979358430323 elapsed_ms=3.206490
  first_layout         ns=70979358729891 elapsed_ms=3.506058
  first_submit         ns=70979358940567 elapsed_ms=3.716734
  first_present        ns=70979358944732 elapsed_ms=3.720899
  egl_load_start       not recorded
  egl_load_end         not recorded
startup_test: failing probe: text and typing submitted/presented while held; allocations=0 guard=ASan-inert
startup (M): elapsed_ms=(stage_ns-process_start_ns)/1000000 (SI)
  process_start        ns=70979361302819 elapsed_ms=0.000000
  display_connect      not recorded
  egl_probe_start      ns=70979363146555 elapsed_ms=1.843736
  egl_probe_end        ns=70979363780255 elapsed_ms=2.477436
  raster_fallback      ns=70979362846559 elapsed_ms=1.543740
  font_atlas_ready     ns=70979361769266 elapsed_ms=0.466447
  file_prefix_read     ns=70979363146694 elapsed_ms=1.843875
  first_layout         ns=70979362954165 elapsed_ms=1.651346
  first_submit         ns=70979363465466 elapsed_ms=2.162647
  first_present        ns=70979363466766 elapsed_ms=2.163947
  egl_load_start       not recorded
  egl_load_end         not recorded
startup_test: finish-init runs on worker after bootstrap; typing progresses while finish held; late wake drained
startup_test: successful probe: text and typing submitted/presented while held; allocations=0 guard=ASan-inert
startup_test: startup stamps survive frame-ring wrap and round-trip; reset clears them
```

`build/tests/startup_test` also succeeds with **active** allocator interposition. Both held phases report (M)[bat] allocations=0, derived by summing intercepted allocation calls across their typing guard intervals. Exact output and stage derivations: [release startup log](edit-2ts-release-startup.log). Actual `build/tools/tracedump build/startup.trace` output: [startup section](edit-2ts-tracedump.log). Stamp retention survives a complete existing ring-capacity workload, and reset removes the retained stamps.

## Full validation and limitations

- `make -j$(nproc) all`: success under gcc, C11 and the requested warning flags. Job concurrency derives from `nproc`, not an invented budget. [Build log](edit-2ts-make-all.log).
- `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j$(nproc) check`: ran as required; make exits unsuccessfully at `cli_test`'s listener creation. [Full make-check log](edit-2ts-make-check.log). ASan/UBSan are enabled; LSan is disabled per HANDOFF's sandbox limitation and requires coordinator rerun with leaks enabled.
- A continuation executed every remaining suite and the shell contracts after make's fail-fast stop. Its result is **49 successful / 17 unsuccessful / 66 total (M, enumerated statuses)**, derived respectively from `len(passed)`, `len(failed)` and `len(results)` in [the result inventory](edit-2ts-suites.json). The inventory includes sanitizer binaries plus replay/display-lock shell contracts. [Complete suite output](edit-2ts-suites.log). The continuation runs directly because denied X access prevents display ownership/injection; actual make check retains its normal display lock.
- Display failures: `editor_close_test`, `file_kill_test` (its viewport fixture opens a display), `raster_test`, `refproto_test`, `refwin_test`, `refwin_timeout_test`, `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`, `x11_stall_test`. `editor_test` also reaches display-dependent review failures. `gl_test` passes pure snapshot/completion checks but explicitly skips native GL sections; no native GL pass is claimed. Some X11 protocol-only suites pass without a display.
- Unix-listener failures: `cli_test`, `ipc_test`, `editor_p4_test`, `editor_large_test`. The latter's pure large-file cooperative/warm/coordinate cases pass in the final run, then its mapped-startup IPC fixture fails at listener initialization. Direct probes confirm both X socket connection and a Unix listener inside this worktree return **EPERM**. IPC listener creation is also denied, so these cannot be called display-only failures.
- Outside scope: `editor_test`'s reload/wait review additionally reports `reload launcher result=4`, where **4 derives from the public enum IPC_IO** following IPC_OK=0, IPC_EXISTS, IPC_INVALID and IPC_LIMIT. Its launcher child fails after tab close. This uses an isolated server/socketpair and is **not proven to be caused by listener denial**. Reported, not fixed; full non-display acceptance is therefore not claimed for the mixed editor suite. Earlier headless open/watchdog failures passed on final rerun; a fresh targeted Keep audit also passed ([audit log](edit-2ts-outside-ck4.log)).
- Outside scope: after its display fixtures fail, `x11_order_test` subsequently dereferences a null XCB connection and reports an ASan SEGV in `xcb_send_request_with_fds64`; its exact stack is in the suite log. No headless startup sanitizer error was reported. No X11 test hardening was attempted.

## Bench evidence

[Trace bench](edit-2ts-trace-bench.log) succeeds. Its call count derives from the existing batch count multiplied by batch size; per-call time is batch elapsed time divided by batch size, and percentile ranks use the existing nearest-rank method. Power/load are read from BAT0 and `/proc/loadavg`. Its existing reference gate is not a newly added budget.

[Editor bench smoke](edit-2ts-editor-bench.log) runs the existing default serial workload on a small startup-text fixture, with `--track --no-idle --no-p4 --serial-only --file=build/startup-bench-fixture.txt`. The null row completes with active allocation guard and no intercepted allocations, then raster setup fails because the display is unavailable. This is not the large-corpus G1 qualification or a native startup measurement. [GL bench](edit-2ts-gl-bench.log), invoked with its existing `--quick` smoke option, explicitly skips for X11 unavailability. No native or Sublime-relative timing verdict is claimed. Some inherited suite output hardcodes an AC tag; do not use those incidental fixture timings as power-certified performance measurements.

## What remains

The coordinator must rerun full checks with permitted Unix sockets, Xvfb :99 and leaks enabled; triage the unrelated reload/wait result; measure raster startup and native GL adoption on the requested displays; and run the next Astra comparison on the same build. Background GL's first-real-swap serial/MSC/mode validation needs native verification. Existing terminal device-failure behavior is unchanged. Raster/discovery resources remain owned through shutdown; an external driver call may still delay **close**, but it cannot gate first paint. Legacy frame-zero gate aggregation is unchanged; use the explicit startup section for startup analysis.

No other bead was implemented or repaired. This worker stops with the tree building and module status/report documentation in place.

## Post-rebase fallback follow-up — 2026-10-11

The cause is the **test injection**, not production backend selection or an
expectation that discovery takes the old 10 s timeout. The fallback fixture's
`injected_gpu_init_failure` delegated to `worker_failed_init`, which returns
`RENDER_ERR_INIT` (-9) only on `cfg->workers->threads[0]` and otherwise returns
`RENDER_ERR_ARG` (-1). `cfg->workers` is the ordinary rendering/file-work pool;
fallback-capable GPU discovery deliberately runs on `e->startup_pool`. The
injection therefore supplied the wrong error, and production correctly retained
that error in `backend_init_error`. Strict initialization still uses the ordinary
pool, which explains why that half of the fixture had a different result.

Ran the requested red command,
`ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test`. In this sandbox it
stopped before fallback at the previously reported reload/launcher result 4 and
native-display setup failures: [full red output](edit-2ts-rebase-editor-red.log).
The original `--fallback` also failed at native `opened == 0`:
[native red output](edit-2ts-rebase-native-fallback-red.log). To reproduce the
reported error-code assertion here, added null-backed GPU variants to the same
fallback fixture and ran `--fallback-headless` **before fixing the injection**:
[headless red output](edit-2ts-rebase-fallback-red.log).

```text
editor_test:1780: FAIL editor_get_stats(e).backend_init_error == (injected ? RENDER_ERR_INIT : RENDER_ERR_UNSUPPORTED)
editor_test:1816: FAIL backend_fallback_case(true, true) == 0
```

Fixed `tests/editor_test.c`: the injected init failure now returns
`RENDER_ERR_INIT` independently of which initializer pool calls it. Worker
ownership remains covered by review 23 and the held startup probes. The new
`--fallback-headless` cases exercise immediate INIT and UNSUPPORTED failures
without X11, stable final selection/error through typing, exactly one diagnostic
through close, cleanup, and exact synchronous error propagation with fallback
disabled. `--fallback` includes these cases and retains both native fixtures,
including the loader fault. No production code or timeout changed; no test error
expectation was weakened. No `git` or `bd` command was run.

Final validation used `ASAN_OPTIONS=detect_leaks=0` for every sanitizer run.
`make -j"$(nproc)" all` plus the requested sanitizer binaries exited 0 under
C11 `-Wall -Wextra -Werror -Wshadow -Wconversion`:
[build output](edit-2ts-rebase-build.log).

| Check | Exit | Output |
| --- | --- | --- |
| Sanitizer `editor_test --fallback-headless` | 0 | [Both errors, one log through typing/close](edit-2ts-rebase-fallback-headless.log) |
| Release `editor_test --fallback-headless` | 0 | [Both errors, one log through typing/close](edit-2ts-rebase-fallback-release.log) |
| Sanitizer `startup_test` | 0 | [Held failing/successful probe, held finish, adoption, late wake drain, retained stamps and composition round trip](edit-2ts-rebase-startup-san.log) |
| Release `startup_test` | 0 | [Same probes; active allocation guard, zero typing allocations](edit-2ts-rebase-startup-release.log) |
| Sanitizer `editor_test --minimap-flash` | 0 | [95 keys; zero orange frames, pending pixel changes and allocations](edit-2ts-rebase-minimap-flash.log) |
| Sanitizer `editor_test --clipboard` | 0 | [Round trip, bulk/journal, undo, PRIMARY, deep horizontal position and no-tab cases](edit-2ts-rebase-clipboard.log) |
| Sanitizer `editor_test --ck4-save` | 0 | [Abrupt exit and exact selected/unrelated/closed recovery](edit-2ts-rebase-ck4-save.log) |
| Sanitizer review 23 | 0 | [Synchronous init success/failure worker ownership](edit-2ts-rebase-init.log) |
| Sanitizer `editor_test --gpu-completion` | 0 | [GPU completion and idle polling](edit-2ts-rebase-gpu-completion.log) |
| Sanitizer `trace_test` / `render_test` | 0 / 0 | [Trace](edit-2ts-rebase-trace.log), [render](edit-2ts-rebase-render.log) |
| `tracedump build/startup.trace` | 0 | [Retained startup stages and elapsed-time arithmetic](edit-2ts-rebase-tracedump.log) |
| Sanitizer `gl_test` | 0 | [Pure completion/trace checks pass; native diagnostics explicitly skip](edit-2ts-rebase-gl.log) |

The held startup probes still prove that file-prefix text and typing present
before discovery completes, and that typing continues while worker finishing is
held. The immediate-failure fixtures impose no ordering between failure
selection and first paint; they wait for final selection only in harness setup.

The final no-argument sanitizer editor run exits 1 at the same unrelated
reload/launcher result 4 and native setup assertions, then `review_suite(NULL)`:
[final full output](edit-2ts-rebase-editor-green.log). Native `--fallback` exits 1
at `opened == 0` after both headless cases pass:
[output](edit-2ts-rebase-native-fallback.log). `--require-gl` exits 1 at
`editor_open`, before its final-selection check:
[output](edit-2ts-rebase-require-gl.log). These native runs used
`DISPLAY=:99 sh tools/with_display_lock.sh`. A fresh direct X99 socket connection
returns errno 1, `Operation not permitted`:
[environment output](edit-2ts-rebase-environment.log). Native discovery timing
and the native `--require-gl` result remain unverified here; no native pass is
claimed. No ASan/UBSan diagnostic appeared in these runs.
