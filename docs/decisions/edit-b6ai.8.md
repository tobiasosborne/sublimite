# edit-b6ai.8: raise the window when a second launch hands files to the running instance

Before this bead, a second `sublimite file` sent its files over IPC and the running instance opened them as tabs, but its window stayed where it was. It could be behind other windows, on another workspace, or minimized. Nothing in `src/x11` sent `_NET_ACTIVE_WINDOW`, the Wayland backend did not bind `xdg_activation_v1`, and the Windows handoff never raised the window (`ShowWindow` is called only in `plat_map`).

## Decisions

1. **The launcher forwards its activation ids; the running instance raises itself.**
   - The second launch is the process the desktop started. It holds the only proof of the user's action:
     - `XDG_ACTIVATION_TOKEN` on Wayland;
     - `DESKTOP_STARTUP_ID` (startup-notification 0.1, ending `_TIME<server time>`) on X11.
   - `main.c` copies both ids into the request with `ipc_args_set_activation` just before `ipc_client_send_display`. That is after stdin is read, so the size check counts the stdin bytes.
   - If a value is unusable, the helper drops it and the handoff still goes through. Unusable means NULL, empty, longer than `IPC_ACTIVATION_CAP`, or containing control characters or spaces.
2. **Wire compatibility (first worker).**
   - The ids travel in a trailing block, flagged by bit 2 of the flags byte, with the block size in header bytes 6-7. Before this bead those two bytes were always zero.
   - A request without ids is byte-identical to the previous format.
   - A server from an earlier release refuses the new flag with `IPC_PROTOCOL`. The client then resends once without the block, so the files still open but the window is not raised. Both `ipc_posix.c` and `ipc_win.c` do this; the Windows client's connect/transfer/reply sequence became a `deliver()` helper so the resend reconnects the same way the POSIX one does.
3. **Only the IPC handoff raises.** `plat_activate` is called from `open_request` in `src/editor/buffers.c`, the `ipc_server_drain` callback, after `editor_open_request` succeeds and only when a platform exists. The first launch's own open and find-in-files hit opens call `editor_open_request` directly and never raise. A handoff with no paths raises as well, because a bare `sublimite` from a launcher should bring the window forward.
4. **X11: `_NET_ACTIVE_WINDOW` with source indication 1.**
   - The client message goes to the root window with SubstructureRedirect|SubstructureNotify (EWMH 1.5): `window` is ours, `data32[0] = 1` (application), and `data32[1]` is the timestamp.
   - The timestamp comes from the first of these that applies:
     1. The startup id's last `_TIME<n>` suffix, if it is all digits, nonzero and fits 32 bits. This is the launcher's time of the user's action, the value a focus-stealing-prevention window manager compares against.
     2. A fresh server time. We append zero bytes to our own `_SUBLIMITE_ACTIVATE_TIME` property; the window already selects PropertyChange. The request goes out from the event loop when that PropertyNotify arrives, using the same asynchronous stamp technique as the clipboard claim, so the loop never blocks and no events are consumed out of order. While a stamp is in flight, a second request without a timestamp joins it.
     3. The last input time (`last_time`), but only if interning the atom fails.
   - The window manager decides what to do. Window managers that apply focus-stealing prevention may only mark the window urgent when the time predates the user's last interaction elsewhere.
   - Atoms are interned lazily, at most once per connection, on the handoff path only.
5. **Wayland: `xdg_activation_v1`.**
   - The global is bound at v1 when advertised. If it is absent, `plat_activate` returns `PLAT_ERR_FAIL` and sends nothing; the editor ignores the return.
   - **Forwarded token:** `activate(token, surface)` right away.
   - **No token:** request one with `get_activation_token`, then `set_serial(last input serial, seat)` (when known), `set_surface`, `commit`, and `activate` with the token when `done` arrives. The token object is destroyed after `done` (or at shutdown). One request is in flight at a time.
   - **Honouring a self-requested token is compositor policy.** The protocol lets the compositor refuse or downgrade a token whose serial is not recent user input on a focused surface; a background instance's last serial usually is not. Many compositors then mark the window urgent or ignore it. This path is therefore a best effort for launchers that do not set `XDG_ACTIVATION_TOKEN`. The forwarded token is the reliable path.
   - "Last input serial" is a new field, `input_serial`. It is set on key, button and keyboard-enter events only, because pointer enter/leave and modifier serials are not user actions. On the typing path this is one extra 32-bit store in `keyboard_key`, with no allocation.
   - The X11 startup id is ignored on Wayland, because it is not a Wayland token.
6. **Windows: grant, then `SetForegroundWindow`.**
   - Windows has no activation token. The rule is that only the foreground process may hand the foreground on.
   - The second launch (the foreground process the user just started) calls `AllowSetForegroundWindow(server pid)`. It gets the pid from `GetNamedPipeServerProcessId` after the peer identity check and before transfer.
   - `win32_plat.c`'s `plat_activate` restores the window if it is minimized and calls `SetForegroundWindow`.
   - Without the grant, for example from an older client, Windows only flashes the taskbar button.
   - This code compiles with `make TARGET=win all`. It has not been run here.

## Not done

- Startup-notification "remove" messages (`_NET_STARTUP_INFO`) for the launcher's busy cursor on X11. The bead did not ask for them, and the cursor times out on its own.
- An editor end-to-end case on Wayland. The editor path is backend-neutral (`open_request` -> `plat_activate`). The Wayland calls are covered at platform level by `activation_test` in `tests/wayland_test.c`.
