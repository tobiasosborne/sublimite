# edit-1zm worker report — s6

Disposition: incomplete. The worktree builds, and the Wayland headless suites
pass. The missing-wp_presentation CPU fallback is larger than a local backend
fix and remains missing, as allowed by the worker brief's scope escape.
No compositor exists in this sandbox. No bd or git command was run. No editor
source was edited; the prohibited editor files and coordinator documents were
left untouched.

**Files touched**

- `src/main.c`: shared Wayland display policy, output-derived font/window startup,
  and diagnostics captured at the failing setup call.
- `src/wayland/wayland.c`: drop-and-count input overflow, startup buffer scaling,
  configure and pointer conversion, shared socket connection setup.
- `src/wayland/connect.c`: Unix socket ABI checks and directory-descriptor paths.
- `src/wayland/output.c`: startup current-output mode/scale query and cleanup.
- `src/wayland/dispatch.c`, `src/wayland/plat.h`, `src/wayland/private.h`: pure
  guard/error policy, output query interface and drop counter.
- `tests/display_guard.h`: inherited Wayland name/descriptor protection for
  tests and benches, with explicit private compositor selection.
- `tests/wayland_test.c`, `tests/wayland_mock.h`: overflow, scale/buffer/pointer,
  missing-presentation, socket-path, guard and calibrated allocator coverage.
- `tests/wayland_startup_test.c`: actual application guard and missing-compositor
  diagnostic, isolated temporary journal directory and cleanup.
- `docs/decisions/edit-1zm.md`, `src/wayland/STATUS.md`, this report.

Build logs and supplemental runner output are under `build/edit-1zm/` and are
verification artifacts. Existing Makefile display classifications were used;
the Makefile was not edited.

**Red runs**

Each functional assertion below was added and run against its preceding
implementation before the corresponding change. The initial new output query
interface also failed to link; the subsequent buffer/configure test supplies
an actual behavioral red run. The allocation red probe intentionally included
native transport; its failure motivated the documented bracket decision.

(P) Numeric source positions in these verbatim logs come from the compiled
assertion locations. Boolean/status/argument literals come from C/POSIX and the
existing platform ABI. The socket bound printed by libwayland derives from
sizeof(sockaddr_un.sun_path), including its NUL requirement. These are not
invented performance budgets or test populations.

Overflow:

```text
$ make build/tests/wayland_test && build/tests/wayland_test
wayland_test: tests/wayland_test.c:73: protocol_test: Assertion `plat_run_for(&p, &cb, 0) == PLAT_OK' failed.
```

Display guard:

```text
$ make build/tests/wayland_test && build/tests/wayland_test
wayland_test: tests/wayland_test.c:142: guard_test: Assertion `!getenv("WAYLAND_DISPLAY") && !getenv("WAYLAND_SOCKET")' failed.
```

Application diagnostic:

```text
$ make build/tests/wayland_startup_test && build/tests/wayland_startup_test
wayland_startup_test: tests/wayland_startup_test.c:34: main: Assertion `readable' failed.
```

Socket pathname:

```text
$ make build/tests/wayland_test && build/tests/wayland_test
error: socket path "/tmp/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" plus null terminator exceeds 108 bytes
wayland_test: tests/wayland_test.c:155: socket_path_test: Assertion `plat_init_backend(&p, &cfg, PLAT_BACKEND_WAYLAND) == PLAT_ERR_SOCKET_PATH' failed.
```

Output query interface:

```text
$ make build/tests/wayland_test
gcc -O2 -g -msse2 -pthread build/rel/tests/wayland_test.o build/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -lwayland-server -o build/tests/wayland_test
/usr/bin/ld: build/rel/tests/wayland_test.o: in function `output_test':
/home/tobias/Projects/sublimite/.wt/edit-1zm-s6/tests/wayland_test.c:42:(.text.startup+0x3e8): undefined reference to `wayland_display_size'
collect2: error: ld returned 1 exit status
make: *** [Makefile:119: build/tests/wayland_test] Error 1
```

Physical buffer/configure dimensions:

```text
$ make build/tests/wayland_test && build/tests/wayland_test
wayland_test: tests/wayland_test.c:49: output_test: Assertion `p.width == (uint32_t)scale * cfg.width && p.height == cfg.height' failed.
```

Native allocator probe:

```text
$ make build/tests/wayland_test && build/tests/wayland_test
wayland_test: native transport allocations=1 (M)[AC], derivation=guarded read_events+dispatch_pending for the mock key packet
wayland_test: tests/wayland_test.c:52: allocation_test: Assertion `!transport' failed.
```

**Green runs**

`make all` completed successfully with the repository gcc release flags.
The toolchain was verified using its version commands and matches the required
compiler generations. (P) Exit success is the process ABI's zero status.

```text
$ make all
exit: success
```

The release run exercises the real libwayland-client socketpair transport,
decoder/queue and actual editor mutation/layout/submit with a null renderer.
(M)[AC] Power was checked before counting using BAT0; “Not charging” is the
repository's AC stamp. Allocation totals derive from the counting interposer
between the named boundaries; calibration derives from the explicit malloc.
The transport and present totals are observations, not new enforced budgets.
The enforced zero typing count derives from the user's no-malloc law.

```text
$ build/tests/wayland_test
wayland_test: real compositor environment refused; private selection and opt-in preserved
wayland_test: long runtime directory resolves through its descriptor
wayland_test: oversized socket basename returns a distinct error
wayland_test: selection and headless lifecycle passed
wayland_test: missing wp_presentation is detected; CPU fallback remains unsupported
wayland_test: allocation counts transport=1 typing=0 post_submit_present=7 calibration=1 (M)[AC] derivation=read+dispatch,decoded_input_through_editor_submit,frame_arm,single_malloc guard=active
wayland_test: wl_output scale supplies startup DPI and physical mode size
wayland_test: decoder allocation guard passed
wayland_test: overflow drops input without poisoning the connection
wayland_test: native key precedes simultaneous worker wake
wayland_test: mock configure/ack, xkb keyboard/repeat/focus, blink/work timers, pointer, clipboard receive/send/cancellation, frame/presentation, discard, cleanup and missing-global checks passed
wayland_startup_test: application guard and missing compositor have readable diagnostics
```

Sanitizer runs use clang ASan/UBSan. Leak detection was disabled only after the
normal full check failed with the sandbox LeakSanitizer diagnostic below.
Allocator interposition is inert under ASan, and the test says counts are
unavailable rather than presenting synthetic zeros as measurements.

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/wayland_test
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/wayland_gl_test
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/wayland_startup_test
wayland_test: real compositor environment refused; private selection and opt-in preserved
wayland_test: long runtime directory resolves through its descriptor
wayland_test: oversized socket basename returns a distinct error
wayland_test: selection and headless lifecycle passed
wayland_test: missing wp_presentation is detected; CPU fallback remains unsupported
wayland_test: native transport, typing and present exercised; allocator counts unavailable under ASan
wayland_test: wl_output scale supplies startup DPI and physical mode size
wayland_test: decoder exercised; allocation count inert under sanitizers
wayland_test: overflow drops input without poisoning the connection
wayland_test: native key precedes simultaneous worker wake
wayland_test: mock configure/ack, xkb keyboard/repeat/focus, blink/work timers, pointer, clipboard receive/send/cancellation, frame/presentation, discard, cleanup and missing-global checks passed
wayland_gl_test: frame callback is only a hint; mocked fence T5 and matching presentation T6, stale/duplicate/discard checks passed
wayland_startup_test: application guard and missing compositor have readable diagnostics
```

The new connection/output/backend sources and the headless test also compile
with Wayland feature macros absent under the required strict release warnings.
This is stub compile coverage, not an optional-dependency full application run.

The final application-only rerun after capturing diagnostics at their setup
stage also passed in release and sanitizer configurations:

```text
wayland_startup_test: application guard and missing compositor have readable diagnostics
wayland_startup_test: application guard and missing compositor have readable diagnostics
```

The touched-module benchmark was invoked, and correctly remains unmeasured:

```text
$ build/bench/wayland_bench
wayland_bench: SKIP unmeasured; use --run PATH [SAMPLES] on a compositor with wp_presentation; no fabricated G1/G3 values
```

**Full-check limits and findings outside the bead**

The normal `make check` builds the sanitizer suites but stops at `base_test`:
The same LeakSanitizer failure recurred in the final full-check invocation after
the last application diagnostic edit (`build/edit-1zm/make-check-final.log`).

```text
gcc -O2 -g -msse2 -pthread build/rel/tools/replay.o build/libedit.a -lm -ldl -lwayland-client -lwayland-egl -lxcb-randr -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/tools/replay
== build/san/tests/base_test
==678==LeakSanitizer has encountered a fatal error.
==678==HINT: For debugging, try setting environment variable LSAN_OPTIONS=verbosity=1:log_threads=1
==678==HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
make: *** [Makefile:133: check] Error 1
```

(P) Diagnostic process IDs and exit statuses in verbatim logs are emitted by
the runtime/process ABI; error values below refer to the existing errno and
module enums, and source positions to the compiled test locations.

`ASAN_OPTIONS=detect_leaks=0 make check` passed `base_test`, waited for the
shared display lock, then failed in `cli_test` at IPC endpoint creation:

```text
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:133: check] Error 1
```

An explicit X11 probe confirms the sandbox cannot open the private X socket:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/x11_live_test
FAIL tests/x11_live_test.c:118: §22 colormap protocol error returned -1
FAIL tests/x11_live_test.c:118: §22 window protocol error returned -1
FAIL tests/x11_live_test.c:118: §22 present protocol error returned -1
x11_live_test: private display connection failed
§22 startup errors: FAILED
```

(P) The private X display identifier above is the coordinator rerun target from
the worker brief, not a performance number. The coordinator must rerun display
suites there with a working X socket and run LeakSanitizer on the host.

The supplemental serial runner completed every binary outside the Makefile's
display classification with ASan/UBSan and leak detection disabled.
(M)[AC] Population = 60 discovered binary/result rows; passes = 57 rows with
success status; failures = 60 - 57 = 3 rows. Full results and individual logs
are in `build/edit-1zm/nondisplay-results.tsv` and its sibling logs. No arbitrary
population or duration limit was introduced.

- `file_kill_test`: its viewport path calls the real X platform and raster
  initializer via the included file benchmark, although the Makefile classifies
  it as non-display. The absent X socket causes initialization failure; its
  child visibility checks also failed. Re-run on the host and review its test
  classification. No Makefile or file module change was made.
- `ipc_test`: Unix endpoint setup returns Operation not permitted in this
  sandbox. The production Wayland mock deliberately uses a private socketpair
  and its already-established test-only credential seam instead.
- `journal_kill_test`: the replay failure below is outside this bead. It was
  not fixed or assumed to be caused by this change; the coordinator must triage.

```text
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
SELF_CHECK section=26 PASS
SELF_CHECK section=27 PASS
SELF_CHECK section=28 PASS
file_cancelled_open: PASS queued/pending-prefix/pending-map getters and filtered delivery
file_save_alloc: PASS pooled fresh ack guard=ASan-inert (release required)
file_kill_test: FAIL visibility/cancellation; power-loss barriers unverified
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
```

**What remains**

The acceptance probe without wp_presentation remains red:

```text
$ build/tests/wayland_test --require-presentation-fallback
wayland_test: tests/wayland_test.c:43: presentation_fallback_test: Assertion `armed == PLAT_OK' failed.
```

The default suite checks that this limitation is detected; that is not evidence
of a CPU fallback. The existing GL guard and explicit CPU rejection still need
a Wayland wl_shm renderer, ownership/release lifecycle and a contract decision
for missing display timestamps. No fake frame-callback display timestamp was
added. This keeps the tree buildable while preserving the unmet acceptance
probe and module status, under the user's larger-than-one-bead instruction.

Output handling is a startup snapshot of the highest-scale advertised mode.
Surface-enter/output-move selection and runtime DPI/font migration remain
unimplemented. Main preserves an inherited WAYLAND_SOCKET for platform init
instead of consuming it in a preliminary query; that launch form retains
legacy font defaults. Old compositor versions without buffer scaling also
need host coverage.

Real compositor checks still required: EGL pixel correctness, buffer-scale
negotiation, mixed-output behavior and actual driver allocation during submit.
The null editor test enforces decoded input through submit and exposes foreign
native/proxy allocation; it does not certify the full physical-input EGL G1
path or provide fabricated latency measurements. No compositor is available
in this sandbox. Tobias's X11 trial remains outside this Wayland work.

Design choices and numeric derivations are in `docs/decisions/edit-1zm.md`;
missing work is also recorded in `src/wayland/STATUS.md`. Stop after this bead.
