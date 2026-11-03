# edit-b6ai.8 (s29): raise the window when a second launch hands files to the running instance

Two workers did this bead in turn. The first stopped at its token budget, and the second finished it.

Outcome: done. `make all`, full `make check` and `make TARGET=win all` are green in this worktree. The design is in docs/decisions/edit-b6ai.8.md.

## What changed

### First worker (IPC and protocol)

- **`src/wayland/protocol/`:** vendored the client header, server header and `.inc` for `xdg-activation-v1`. Also changed `protocol.c` and `README.md` there.
- **`src/ipc/ipc.h`:**
  - Added `ipc_request.activation_token` and `startup_id`.
  - Added `IPC_ACTIVATION_CAP` (512, with its derivation in the header).
  - Added `ipc_args_set_activation`.
- **`src/ipc/wire.c`:**
  - The ids go in a trailing block, flagged by bit 2 of the flags byte, with the block size in header bytes 6-7.
  - A request without ids is byte-identical to the previous format.
- **`src/ipc/args.c`:** the `ipc_args_set_activation` helper. It drops unusable values and keeps the request within `IPC_MAX_WIRE`.
- **`src/ipc/ipc_posix.c`:** when the server answers `IPC_PROTOCOL` (an earlier release), the client resends once without the block.
- **`tests/ipc_test.c`:** added `t_activation`, covering:
  - the codec and the byte-identical legacy encoding;
  - malformed blocks;
  - the helper;
  - the legacy-server fallback.

### Second worker (this session)

- **`src/ipc/ipc_win.c`:**
  - The connect/transfer/reply sequence is now a `deliver()` helper, and the `IPC_PROTOCOL` resend works the same as on POSIX.
  - After the peer check, the client calls `AllowSetForegroundWindow(GetNamedPipeServerProcessId)` so the running instance may take the foreground.
- **`src/main.c`:** before `ipc_client_send_display`, it calls `ipc_args_set_activation(&args, getenv("XDG_ACTIVATION_TOKEN"), getenv("DESKTOP_STARTUP_ID"))`.
- **`src/x11/plat.h`:** new `int plat_activate(plat *, const char *token, const char *startup_id)`.
- **`src/wayland/dispatch.c`:** routes `plat_activate` to the selected backend.
- **`src/wayland/private.h`:** declarations for the two backend functions.
- **`src/x11/x11.c` and `src/x11/private.h`:**
  - `x11_plat_activate` sends `_NET_ACTIVE_WINDOW` to the root with SubstructureRedirect|SubstructureNotify, source 1, and our window.
  - The timestamp is the startup id's `_TIME<n>` if valid. Otherwise it is a fresh server time: a zero-length append to `_SUBLIMITE_ACTIVATE_TIME`, sent from the PropertyNotify in `dispatch`. If interning the atom fails, it is `last_time`.
  - Stores `root`.
- **`src/wayland/wayland.c`:**
  - Binds `xdg_activation_v1` v1.
  - With a forwarded token it calls `activate(token, surface)`.
  - Without one it calls `get_activation_token`, then `set_serial(input_serial, seat)`, `set_surface` and `commit`, and activates when `done` arrives.
  - New `input_serial`, set on key, button and keyboard-enter events.
  - The token and the global are destroyed at shutdown.
  - Added a stub for builds without Wayland.
- **`src/win32/win32_plat.c`:** `plat_activate` does `ShowWindow(SW_RESTORE)` if the window is minimized, then `SetForegroundWindow`. It is a small change and is safe without the grant: Windows then only flashes the taskbar button. It compiles but has not been run here.
- **`src/editor/buffers.c`:** `open_request`, the IPC drain callback, calls `plat_activate` after `editor_open_request` succeeds when a platform exists. `editor_open_request` itself, used by the first launch and by find-in-files opens, does not.
- **`tests/x11_activate_test.c` (new):**
  - Runs on a private Xvfb. It unsets `EDIT_DISPLAY`, so `xvfb_start` always picks an explicit free number and never uses the shared `:99`, which must keep no root redirect.
  - A second client selects SubstructureRedirect on the root and acts as the window manager (maps on MapRequest).
  - Platform cases: a startup id timestamp, a fresh server time (at least the manager's own fresh stamp), and a malformed `_TIME` suffix.
  - Editor end-to-end case: the first launch's own open sends nothing. A forked `ipc_client_send` handoff with a token and a startup id raises exactly once, on the editor's window, with source 1 and the startup id's time. A quiet window afterwards checks that no duplicate follows.
  - The Makefile picks the test up through `TESTS := $(wildcard tests/*_test.c)`. It is in the display lane through the `x11%` filter.
- **`tests/wayland_mock.h`:**
  - Adds an `xdg_activation_v1` global, enabled by `mock_shm.activation`. A committed token is answered with `mock-token-<n>`.
  - Shared fields record `set_serial`, `set_surface` and `activate`.
  - Key, button and keyboard-enter serials are now recorded through `mock_input()`.
- **`tests/wayland_test.c`:** `activation_test` covers three cases:
  - a forwarded token: activate with it, no token request;
  - a self-requested token: the serial equals the last input serial, and the seat and surface are ours;
  - no global: `PLAT_ERR_FAIL` and nothing sent.
- **`tests/gl_test.c`:** this test compiles `src/x11/x11.c` privately under renamed symbols. It needed `#define x11_plat_activate gl_review_plat_activate` and the matching `#undef`, otherwise `make all` failed to link with a duplicate definition.
- **Docs:** added docs/decisions/edit-b6ai.8.md and this report. Deleted `src/ipc/STATUS-edit-b6ai.8.md`; its content is above.

## Red run

**IPC part (first worker):** its red run was not captured. Its green run was `build/tests/ipc_test activation` and `wire`.

**Platform part.** I declared `plat_activate` with a stub returning `PLAT_ERR_FAIL` and wrote the tests first:

```
$ build/tests/x11_activate_test
x11_activate_test: FAIL tests/x11_activate_test.c:116: plat_activate(&p, NULL, "sublimite-4242-host_TIME98765") == PLAT_OK
$ build/tests/wayland_test
wayland_test: tests/wayland_test.c:239: activation_test: Assertion `plat_activate(&p, "launcher-token", NULL) == PLAT_OK' failed.
```

**Editor part.** With the platform implemented but `open_request` not yet calling it, the platform case passed and the editor case failed:

```
x11_activate_test: _NET_ACTIVE_WINDOW with source 1: startup-id timestamp, fresh server time, malformed id
x11_activate_test: FAIL tests/x11_activate_test.c:98: now_ms() < deadline
```

Line 98 is the wait for the handoff's message.

## Green run

```
x11_activate_test: _NET_ACTIVE_WINDOW with source 1: startup-id timestamp, fresh server time, malformed id
x11_activate_test: editor first open does not raise; IPC handoff raises once with its startup id
x11_activate_test: all passed
wayland_test: xdg_activation_v1 forwarded token, self-requested token (input serial + surface), absent global
```

- **`make all`:** exit 0.
- **`make TARGET=win all`:** exit 0. `plat_activate` is present in `build/win/rel/src/win32/win32_plat.o`.
- **`make check`:** exit 0, with `check: 120 test binaries passed in 99.3 s (parallel, 36 jobs, 6 displays)`. The replay, Windows-wrapper (skipped, not WSL), install-omarchy and display-lock contract scripts all passed.
- The `make[1]: *** ... Error 1` and "display lock timeout" lines in the log come from `tools/test_display_lock.sh contract`. That script provokes those failures on purpose, and it reports PASS.

## Measurements

None. No module bench applies; the IPC handoff is off the typing path. Machine: [AC, desktop monolith]; there is no BAT0.

## Typing path

- No allocation is added.
- `keyboard_key` and `pointer_button` on Wayland now also store `input_serial`, one 32-bit store each.
- The X11 `dispatch` PropertyNotify case gains one `activate_pending` branch ahead of the clipboard handler.

## Found (not fixed)

- The X11 launcher's busy cursor is not cleared, because startup-notification "remove" (`_NET_STARTUP_INFO`) is not sent. It times out on its own. This is out of scope.
- Whether the window actually rises is window-manager or compositor policy, on both X11 and Wayland. In particular, Hyprland's handling of a self-requested Wayland token, for launchers without `XDG_ACTIVATION_TOKEN`, is unverified. It cannot be checked here without touching the real session, which is forbidden by rule 13.
- The Windows behaviour has only been compiled, not run.
- `/tmp/.X11-unix/X0` was present throughout; I did not need to restore it.
