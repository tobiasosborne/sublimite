# edit-q0g-s9 — worker report

Scope: P2-1 X11 input review §§6, 21–25, 36–37, 39. Worktree branch `wt/edit-q0g`. Read CLAUDE.md, the assigned bead/review sections, src/x11/STATUS.md, and the edit-og2 IPC-isolation diff. Git was read-only; no bd, HANDOFF.md, or docs/worklog edits. No globals were added.

**Not ready to close the whole bead:** §21 exact consecutive server mapping generations remain. All other listed software corrections are implemented. Hardware-specific XI2 verification remains unmeasured.

Decisions: [docs/decisions/edit-q0g.md](../decisions/edit-q0g.md).

All pasted runs below are measured execution evidence (M)[AC], not performance gate verdicts. Release compiler is gcc 13; sanitizer compiler is clang 18, with C11 `-Wall -Wextra -Werror -Wshadow -Wconversion`. Live runs use `DISPLAY=:99 EDIT_DISPLAY=:99` through the shared display lock. Local Xvfb socket access required sandbox escalation; no automatic review rejected the requests. Initial sandbox-blocked runtime alignment checks were discarded and the pre-fix negative control was rerun with socket access before accepting red/green evidence.

Run commands: `make build/tests/x11_order_test build/tests/x11_alignment_test build/tests/x11_xi2_test build/tests/cli_test build/tests/x11_display_guard_test`; use `sh tools/with_display_lock.sh build/tests/x11_order_test <case>` for alignment, map_boundary, repeat_order, xi_mask_pending, wheel_pending. Pure alignment/xi2/display-policy binaries do not need a display. CLI tests use a task-specific EDIT_IPC_NAMESPACE to avoid foreign primaries. Red was observed before each corresponding correction; test log excerpts omit unrelated renderer warnings and temporary fixture paths.

## §6 — done

Runtime and clipboard work-pool owners use extended-alignment allocation and initialization. Release and sanitizer fixtures reject calloc for these owner types and assert actual pointer alignment.

Red (M)[AC]:

```text
x11_order_test: FAIL fixture initialization
x11_order_test: alignment FAILED
```

Green (M)[AC]:

```text
x11_order_test: alignment ok
```

Red (M)[AC]:

```text
x11_alignment_test: FAIL §6 clipboard pool uses fundamental allocator
```

Green (M)[AC]:

```text
x11_alignment_test: §6 ok
```

## §21 — partial; exact consecutive server generations remain

A notification now blocks subsequent raw dispatch until the prepared map/state is adopted; prior translated input is delivered first. Retained XCB events preserve their original timestamps, and repeat is cancelled. The default test includes a real US-to-German server map change with worker I/O delayed and immediate before/after keys. The existing worker fetches current server state, so exact snapshots for multiple already-processed map generations are not implemented. Do not close this finding on the single-change evidence alone.

Red (M)[AC]:

```text
x11_order_test: FAIL §21 later raw keys wait for matching map publication
x11_order_test: FAIL §21 replayed physical key retains identity
x11_order_test: map_boundary FAILED
```

Green (M)[AC]:

```text
x11_order_test: map_boundary ok
```

## §22 — done

Repeat synthesis waits while earlier raw input remains buffered/readable. Deterministic release and focus-out fixtures place cancellation beyond the first drain slice while the repeat timer is ready. Bounded blink/work service remains covered by the existing slices tests.

Red (M)[AC]:

```text
x11_order_test: FAIL §22 queued release/focus beyond first slice precedes repeat
x11_order_test: FAIL §22 queued release/focus beyond first slice precedes repeat
x11_order_test: repeat_order FAILED
```

Green (M)[AC]:

```text
x11_order_test: repeat_order ok
```

## §23 — done

XI2 decodes held buttons through button 32, keeps extended buttons separate, and merges only core buttons 1–5 into core state. Button-9 regression is enabled in the default suite, with multigroup and button-32 checks.

Red (M)[AC]:

```text
x11_order_test: FAIL §19 XI motion preserves held button 9
x11_order_test: FAIL §23 pointer buttons cannot change repeat group
x11_order_test: FAIL §23 repeat remains in US group
x11_order_test: FAIL §23 pointer buttons cannot change repeat group
x11_order_test: FAIL §23 XI motion preserves held button 32
x11_order_test: xi_mask_pending FAILED
```

Green (M)[AC]:

```text
x11_order_test: xi_mask_pending ok
```

## §24 — done in software

XI2 button press/release selection and decoding replace global timestamp deduplication. Source identity and pointer-emulation flags suppress only the smooth source’s own emulation. Independent discrete input survives both orders. Native core input remains the fallback without XI2; explicit SendEvent packets remain supported. Real multi-device/touchpad hardware verification is not available on Xvfb.

Red (M)[AC]:

```text
x11_order_test: FAIL §24 smooth + independent notches counted, own emulation suppressed in both orders
x11_order_test: FAIL §24 XI button press/release selected
x11_order_test: FAIL §24 smooth + independent notches counted, own emulation suppressed in both orders
x11_order_test: FAIL §24 XI button press/release selected
x11_order_test: wheel_pending FAILED
```

Green (M)[AC]:

```text
x11_order_test: wheel_pending ok
```

## §25 — done in software

Runtime query adoption captures a sample boundary and merges newer matching live axis history/remainder. DeviceChanged seeds axis values from its class snapshot and does not invalidate unrelated sources. Tests cover stale reply adoption, intervening motion, and class seeding; the fuzzer also exercises adoption and group isolation.

Red (M)[AC]:

```text
FAIL tests/x11_xi2_test.c:224: §25 adoption cannot replay already decoded motion
FAIL tests/x11_xi2_test.c:231: §25 unrelated source preserves live baseline
FAIL tests/x11_xi2_test.c:240: §25 intervening first motion uses class seed
FAIL tests/x11_xi2_test.c:243: §25 changed-source live history survives stale reply
x11_xi2_test: FAILED
```

Green (M)[AC]:

```text
x11_xi2_test: ok
```

## §36 — done

Window timeout after fixture preflight now fails. The default CLI suite forks a forced-timeout negative control and requires failure, then verifies successful ordinary startup and trace-dump failure handling. Default production EGL-to-raster fallback is exercised. A test-local factory also delays the real raster initializer on the production initialization worker, restores backend identity, and requires a completion marker plus successful production startup/shutdown.

Red (M)[AC]:

```text
cli_test: SKIP: window not created within 0 s (load1=0.91) [forced test timeout]
cli_test: FAIL §36 forced startup timeout succeeded/skipped
```

Green (M)[AC]:

```text
cli_test: FAIL: window not created within 0 s (load1=0.77) [forced test timeout]
cli_test:137: FAIL ran == 0
cli_test: §36 forced startup timeout correctly failed
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
```

## §37 — done with main-owned endpoint

Capture/record startup ingress at production main entry, before arguments, IPC, and journal setup. The production trace test requires one frame-zero T0 and ordered connection/map endpoints. Design choice: record directly at main instead of expanding editor configuration outside the assigned files. Launcher/loader time is not measured.

Red (M)[AC]:

```text
cli_test: FAIL: window not created within 0 s (load1=0.55) [forced test timeout]
cli_test:148: FAIL ran == 0
cli_test: §36 forced startup timeout correctly failed
cli_test:121: FAIL ingress == 1 && connected == 1 && mapped == 1
cli_test:148: FAIL ran == 0
```

Green (M)[AC]:

```text
cli_test: FAIL: window not created within 0 s (load1=0.39) [forced test timeout]
cli_test:148: FAIL ran == 0
cli_test: §36 forced startup timeout correctly failed
cli_test: §37 production startup trace endpoints passed
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
```

## §39 — done

The test/bench constructor shares production display-number policy and validates alternate displays. Pure tests cover leading-zero, transport/host, and IPv6 zero aliases without connecting to those displays.

Red (M)[AC]:

```text
display_guard_test: FAIL §39 alias :00
display_guard_test: FAIL §39 alias localhost:00.0
display_guard_test: FAIL §39 alias unix/:0
display_guard_test: FAIL §39 alias hostname/unix:0
display_guard_test: FAIL §39 alias 127.0.0.1:00
display_guard_test: FAIL §39 alias [::1]:0
display_guard_test: FAIL §39 unsafe alternate refused
display_guard_test: FAILED
```

Green (M)[AC]:

```text
display guard: real display hostname/unix:0 refused, using :99
display guard: real display localhost:00.0 refused, using :99
display_guard_test: §39 ok
```

## Acceptance and remaining verification

- `make -j4 all`: exit 0 (M)[AC].
- `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_IPC_NAMESPACE=q0g-check make -j4 check`: exit 0 (M)[AC], all test binaries and replay/display-lock contracts passed. The second full run on the final test tree also exited 0 (M)[AC].
- X11 fuzz: `ASAN_OPTIONS=detect_leaks=0 build/fuzz/x11_input_fuzz -max_total_time=60 -max_len=512`, exit 0; `Done 2469408 runs in 61 second(s)` (M)[AC]. Battery status was Not charging, power AC per the assigned environment and an online supply confirmed during final verification, one-minute load 3.38 (M)[AC]. No sanitizer crash. No timing gate inferred from this run.
- LeakSanitizer was disabled as required by the sandbox constraint. Coordinator must rerun with leaks enabled.
- No hot-path heap allocation was introduced. Release allocation-guard and full sanitizer-suite results will be appended below.
- No performance verdict is claimed; the host is shared. No benchmark variants or single-run gate decision were made.
- IPC endpoint-isolation changes from edit-og2 were not copied or modified. Coordinator merge should preserve its changed session signature/decoy cases while applying the deadline failure and startup-trace assertions to each session.
- Existing clipboard overflow, clipboard worker admission, rendering/startup critical-path, and other review findings outside this bead were left unchanged.

Exact consecutive-generation red (M)[AC], run after the single-boundary correction:

```text
x11_order_test: FAIL §21 exact snapshots retain both successive server mapping generations
x11_order_test: map_generations_pending FAILED
```

The optional `x11_order_test map_generations_pending` is a retained reproduction for the unimplemented exact multi-generation map snapshot path; it is intentionally not part of the default green suite and must remain explicitly accounted for when continuing §21.

Final release rebuild and pure sanitizer checks (M)[AC]: `make -j4 all` exited 0; the clipboard alignment, XI2 parser/adoption, and display-policy sanitizer binaries exited 0.

```text
x11_alignment_test: §6 ok
x11_xi2_test: ok
display guard: real display hostname/unix:0 refused, using :99
display guard: real display localhost:00.0 refused, using :99
display_guard_test: §39 ok
x11_input_test: ok
```

The shared display lane was occupied by another raster test during final verification. Its process was inspected read-only; no other worker's process or display lock was modified.

§36 delayed-backend negative control red (M)[AC]:

```text
cli_test:136: FAIL delayed == 1
cli_test:164: FAIL ran == 0
```

§36 delayed-backend sanitizer green (M)[AC], exit 0:

```text
cli_test: §36 forced startup timeout correctly failed
cli_test: §37 production startup trace endpoints passed
cli_test: §36 delayed production backend initialization completed
cli_test: §37 production startup trace endpoints passed
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
```

At the required minute-30 scope cutoff (G), production/test implementation was frozen. The report already contained all findings, decisions, pasted red/green evidence, and the remaining §21 failure. Only final verification and documentation updates continue.

Final-tree X11 fuzz run (M)[AC], exit 0:

```text
ASAN_OPTIONS=detect_leaks=0 build/fuzz/x11_input_fuzz -max_total_time=60 -max_len=512
Done 2423547 runs in 61 second(s)
```

Final fuzz start stamp: battery Not charging, AC supply online, one-minute load 0.58 (M)[AC]. No sanitizer crash or assertion failure. The earlier campaign also passed; this second campaign used the final headers/implementation and was performed after implementation was frozen. Leak detection remains disabled for coordinator rerun.

Final acceptance run (M)[AC], all commands exit 0:

```text
DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_IPC_NAMESPACE=q0g-check-final make -j4 check
check: 62 test binaries passed
test_replay_cli: all passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

The display-lock contract deliberately exercises failed nested acquisition and timeout exits; those expected negative-control messages do not indicate a failed outer check. Final `git diff --check` passed (M)[AC]. Release `x11_input_test` passed with its allocation checks; sanitizer allocation hooks are inactive, so no sanitizer allocation-count claim is made. The final CLI test tree includes both the forced-timeout failure and real worker backend-initialization delay cases.

Remaining for the coordinator: implement/adjudicate §21 exact consecutive map snapshots, enable its retained failing reproduction once fixed, rerun leaks on, and perform hardware XI2 verification. No code was changed after the scope cutoff. No Git mutation was performed. This bead is not fully complete.
