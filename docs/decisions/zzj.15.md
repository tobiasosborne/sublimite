# edit-zzj.15 — default EGL with one raster fallback

Session 8 continuation: the rebased P4.I fixture is now green. Current
verification and scope limits supersede the historical snapshot failure below:
[edit-zzj.15.md](edit-zzj.15.md) and
[worker report](../worker-reports/edit-zzj.15-s8.md).

Read P2.4 and e6x.16 in full before editing. The confirmed EGL pick and the
internal-panel paced-scroll evidence remain settled; this bead wires that pick
into P4.I's existing loop. No dependency, frozen header, Makefile or GL source
was changed.

## Default backend and override — confirmed

`src/main.c` retains one factory selection site, now calling
`editor_backend_select(..., getenv("EDIT_BACKEND"))`. Unset means EGL;
`gl` and `raster` are the only explicit values. Invalid values return an error
without modifying the handle. The helper is startup-only. Explicitly supplied
null/delayed backends continue to work through the existing editor API.

Red (release `editor_test --selection`, before implementing selection):

```text
editor_test:257: FAIL (b.info.capabilities & RENDER_CAP_GPU) != 0
```

Green:

```text
editor_test: default EGL and gl/raster override passed
```

## Failed EGL/GL/Present init — confirmed

Previously `editor_open` failed instead of creating a usable editor. With
`editor_config.raster_fallback`, only a failed GPU backend init triggers a single
CPU init using fresh aligned state and the same platform, epoll and IPC setup.
Raster workers and both state reservations are prepared at open. Successful GL
init never runs raster init. Platform/file/journal/config errors do not trigger
fallback; CPU init failure propagates. Disabling the policy preserves the direct
GL caller's failure behavior.

The editor emits one startup diagnostic with a reason category and the exact
render error code. The GL API collapses loader, EGL capability and Present-probe
failures into `RENDER_ERR_UNSUPPORTED`; the log therefore truthfully reports
`EGL/GL/Present unavailable (code=-10)` rather than inventing a failing substage.
No GL hook or renderer changes were needed. `backend_init_error` retains that
code for test/bench reporting. There is no retry or fallback in `editor_step`.

Red (`editor_test --fallback`, with host access to Xvfb :99):

```text
editor_test:274: FAIL editor_open(&e, &cfg, &b) == 0
```

Green:

```text
editor_test: failed EGL init falls back once and stays raster passed
```

The regression forces a nonexistent EGL library, captures/asserts one diagnostic,
removes the fault after open, types and checks that the selected backend stays
CPU. It also checks the disabled-fallback error/teardown path. The selected GL
allocation/idle fixture separately exercises natural EGL/Present init failure
on :99. Both selected backends retain the existing guarded input-to-submit
boundary and journal/content assertions over 10000 keys (G fixture count).
ASan's guard remains explicitly inert; release performs the allocation count.

G1 and G11 bench rows now include null, raster and GL selections. `EDIT_BACKEND`
limits the native rows to the named selection; the null reference remains.
Each row prints requested/actual backend and init error. Failed native GL rows
are named `gl_fallback_raster`, never presented as native GL data.
`--require-gl` makes fallback fatal in the GL row. Allocation violations are
fatal for these G1 rows even under TRACK. P4 tab/IPC rows are unchanged.

## GPU completion and idle — integration gap confirmed; idle-worker concern unproven

The inherited editor pump never routed EGL's platform completion callback or
polled its outstanding fence/private Present events. Merely selecting GL would
leave its frame slot active. A headless GPU seam with completion available only
through `GL_POLL_MESSAGE` reproduces this without pretending Xvfb is hardware GL.

Red (`editor_test --gpu-completion`):

```text
editor_test:50: FAIL false
editor_test:300: FAIL settle(e) == 0
```

Green:

```text
editor_test: GPU completion progresses and idle disarms polling passed
```

The pump now routes `on_present_complete` to `gl_present_complete` and sends a
bounded nonblocking GL poll only while a GPU frame is active and presented.
A pending frame caps the next wait at 1 ms (G retry bound): the GL review's
private-XCB-buffer/fence cases can need another observation after their sole
platform callback. The editor stops scheduling these retries immediately when
T5/T6 release the slot. Renderer completion correctness itself remains with
edit-e6x.26; this integration does not alter its validation or cleanup.

No evidence supports a new GL-worker *idle* wakeup finding: EGL init's existing
one-shot thread is joined before open returns, GL creates no periodic worker
job, and src/work's idle workers wait on condition variables. The reserved CPU
workers also sleep on successful GL init. Thus there is no worker timer to fix.
The synthetic GPU test completes frames and verifies the inactive/unfocused
wait. The selected-backend fixture repeats null/raster's blink-expiry and
unfocus pattern, allowing only the external observation timeout afterward.
Native GL idle remains a required coordinator check, not a worker measurement.

```text
editor_test: requested=gl actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: requested=gl actual=cpu-raster idle/unfocused background wakeups=0 passed
editor_test: requested=raster actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: requested=raster actual=cpu-raster idle/unfocused background wakeups=0 passed
```

## Verification and coordinator check

All worker live commands use `DISPLAY=:99 EDIT_DISPLAY=:99`, with no real-display
opt-in. Host socket access is needed for Xvfb; no replacement server was started.
Leak checking is disabled in worker sanitizer runs and remains the coordinator's
leak-on check. Build/fuzz/once-only TRACK evidence is appended after completion.

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_BACKEND=gl build/tests/editor_test --backend-only
DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_BACKEND=raster build/tests/editor_test --backend-only
```

Exact coordinator-only real-display check, after separately authorizing that
fixture, placing its windows on the internal panel, confirming refresh with
`xrandr` and muting/restoring desktop notifications as in e6x.16:

```sh
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
env DISPLAY=:0 EDIT_DISPLAY=:0 EDIT_ALLOW_REAL_DISPLAY=1 EDIT_BACKEND=gl build/tests/editor_test --require-gl
env DISPLAY=:0 EDIT_DISPLAY=:0 EDIT_ALLOW_REAL_DISPLAY=1 EDIT_BACKEND=raster build/tests/editor_test --backend-only
env -u EDIT_BACKEND DISPLAY=:0 EDIT_DISPLAY=:0 EDIT_ALLOW_REAL_DISPLAY=1 build/bench/editor_bench --track --no-p4 --require-gl
```

Require `actual=egl`, active release allocation guard with zero allocations,
idle/unfocused background wakeups zero, successful subsequent typing, and
matching frame completion. The bench command runs both native rows once and
retains the null reference. Stamps accompany each row; shared-box timing is
TRACK. Xvfb fallback output is never a native-EGL G1/G11 or cadence verdict.

Scope limitations for coordinator: inherited raw-pthread init/mailbox deviation
(editor review #23), G1 ingress/fixture dimensions and G11 focused raster blink
cost are separate review work. This bead reuses the inherited initialization
handoff without refactoring P4.I. No frozen-header amendment is required here.

The stacked P4.I snapshot also contains a reproducible preexisting sanitizer
failure. Its first fixture uses a headless delayed backend with no GPU/raster
capabilities and no fallback policy, so none of the new GPU/fallback branches
runs. A temporary library in `/tmp/zzj15-baseline` rebuilt editor.c and open.c
with this bead's changes removed reproduces exactly the same failure:

```text
editor_p4_test:275: FAIL editor_get_stats(e).tabs == 1
editor_p4_test:375: FAIL ipc_pending_input(&server, runtime) == 0
```

No P4.I test or IPC code was changed. Full `make check` cannot be claimed green
in this snapshot; all remaining sanitizer binaries are run independently so
this inherited failure does not hide this bead's verification.
