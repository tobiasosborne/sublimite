# edit-177r.8 — editor wait set and Windows entry point

## Wait ownership and capacity

The UI owns registration and removal. Sources and the platform borrow wake objects/native handles; only their owning modules drain or close them. Linux keeps one epoll descriptor and lends its readiness to X11/Wayland exactly as before. Construction of that borrowed native view is confined to wait_posix.c, replacing open.c's existing epoll/os_wake alias; the frozen os.h has no general native-wake constructor. Windows snapshots the current source handles through a callback, including sources registered after platform initialization. Waiting and dispatch allocate nothing.

(P) Win32's MAXIMUM_WAIT_OBJECTS is 64 (winnt.h). MsgWaitForMultipleObjectsEx reserves one position for the message queue. The existing platform owns four waits: blink timer + repeat timer + presentation event + clipboard service event. Therefore the editor may lend at most `64 - 1 - 4 = 59` native handles. Registration beyond this limit returns OS_E_NOSPC before modifying the set. The legacy single work wake and the source callback are mutually exclusive.

(P) Current editor worst case derives from three pools (editor + journal + startup), one config-runtime pool, one IPC service, one aggregate file-watch service shared by all buffers and config, and one close request: `3 + 1 + 1 + 1 + 1 = 7` Windows editor handles. Linux additionally has one raster completion descriptor: `7 + 1 = 8`. Per-buffer watch tokens do not consume wait positions. The D3D11 renderer latency event may require one additional source at integration: even then `7 + 1 = 8 <= 59`.

## Entry point and backend

Windows converts `GetCommandLineW` / `CommandLineToArgvW` output explicitly to UTF-8 before `ipc_parse_args`. Linux retains its parser, display policy, fork launcher and complete-record signalfd close handling. The trace dump uses the frozen `os_file_open` / `os_file_to_stdio` migration rule (edit-177r.2 section 9).

A Windows `--wait` launcher calls `ipc_launcher_prepare`, self-spawns with the original UTF-8 arguments plus a private endpoint/parent-pid marker, then calls `ipc_launcher_wait`. It does not read stdin; the child inherits the original standard streams through `os_proc_spawn` and reads once. The child performs the normal primary/isolated-server selection and adopts the launch pipe through `ipc_server_connect_wait`. If a primary already exists, an isolated bridge keeps the launch pipe alive while forwarding the request to that primary. At successful shutdown the IPC worker gets time to send the final close reply before server teardown: the child waits for the launcher process to exit. The launcher's process handle stays owned until its process exits; the frozen process API has no detach operation.

Backend selection uses the D3D11 factory on Windows and preserves GL/raster selection on Linux. Once the HWND and zeroed arena state exist, `render_d3d11_backend_for_window` binds them before worker init. The UI pumps native messages during worker initialization and shutdown because DXGI may send synchronous window messages. Windows does not enter the Linux raster bootstrap path; D3D11 already has its native WARP fallback. Active Windows GPU frames retain the existing GPU retry cadence, including a frame waiting to Present; idle has no GPU timer. Direct latency-HANDLE integration and a live application run remain part of W2.4d.

## Console and window close

`SetConsoleCtrlHandler` has no context parameter. Instead of adding a global pointer, main owns a close service with a caller-owned `os_wake`, a bridge thread and request/completion events named by the process id. The console callback opens the native request event, signals it, and waits on completion. The bridge signals the editor's registered `os_wake`. Completion is signalled only after editor teardown, keeping a console-close callback from returning before the clean-close path finishes. The named events and bridge are outside the UI wait set and therefore do not consume its source capacity. Shutdown unregisters the callback, wakes/joins the bridge and closes all owned handles. No os.h private Windows fields or new process globals are used.

`WM_CLOSE`, `WM_QUERYENDSESSION` and a committed `WM_ENDSESSION` produce the same platform CLOSE event. As with the existing Linux signal path, the UI sets `quit`; existing journal/close handling owns persistence. The native callback is tested directly on a helper thread, avoiding broadcasts to unrelated programs sharing the runner's console. This is not an end-to-end Windows logoff/power-off durability claim.

## Compatibility and integration boundaries

The source-snapshot callback and context are trailing Windows-only `plat_config` members. Linux layout and X11/Wayland sources are unchanged. Windows clients that still use `work_wake` retain that path; supplying both paths is rejected. The existing positional Win32 test fixtures omit the new trailing members; their test-only warning exception records that these members are zero-initialized.

`editor_config` gains a Windows-only borrowed close wake and a Windows `const os_file *` session lease; Linux keeps the existing fd lease. These are the smallest typed main-to-editor handoffs even though editor.h was omitted from the brief's file list. Shared `private.h` changes replace the poll fields with the set/readiness flag, make the owned buffer mutex and font-worker identity portable, and declare the quiescent backend-shutdown helper. Sibling W2.4b must reconcile its portable session/lease representation with the Windows cleanup in open.c; its recovery/session sources were not edited here.

`win-editor-loop` compiles the owned loop TUs and main in release and sanitizer modes without linking an application, and is a prerequisite of the Windows check target. Wait objects are linked directly into the standalone Windows wait test, rather than adding the unported rest of editor to WIN_MODULES. Config changes are limited to the requested wake accessor and declaration.

## Verification

The new Windows suite exercises actual work publication, IPC transport, aggregate file-watch completion, manual close wake, console bridge, keyboard input and window/session close. Sources are registered after platform initialization, so these cases also check dynamic source snapshots. It checks the capacity boundary, idempotent registration, removal/reuse, borrowed lifetime and allocation-free readiness. Production loop compilation and the full Linux/Windows suites are recorded in `docs/worker-reports/edit-177r.8-s13.md`.
