# edit-upwq: Win32 platform backend

(M) Live observations below are from `[AC, desktop Spiral]`; the final Windows run began at uptime `4:05`, load averages `1.40, 1.58, 1.73` (captured with `uptime`; raw log in `build/worker-upwq/win-load.txt`).

The portable `plat` remains unchanged. `plat.h` gains only the Win32 backend tag and borrowed `plat_native_win32()` HWND accessor. Windows selects this backend regardless of the Linux display environment. The Windows build links `src/win32`; the existing Linux dispatcher remains the Linux implementation and supplies a NULL Win32 accessor. `os.h` and `render.h` are unchanged.

## Ownership and loop

`win32_plat` owns its window, unique window class, input ring, clipboard receive buffer, blink and repeat timers, and presentation mailbox. It borrows the configured `os_wake`. Window-class identity includes the private object's address, allowing independent platform instances without a process-global registration counter. Shutdown destroys the window, unregisters its class, closes owned handles and frees its buffers. Renderer producers must stop before shutdown.

`MsgWaitForMultipleObjectsEx(MWMO_INPUTAVAILABLE)` waits for messages, the borrowed work event, both timers and the presentation event. The work callback drains its borrowed wake before reading its mailbox, per `os.h`; the platform never drains or closes it. `PeekMessageW` processes the calling UI thread's messages. Each translated event is delivered before another raw message is dispatched. Quit from any callback stops further callbacks. Native CLOSE requests produce a contract event and leave destruction to shutdown.

(G) The input and presentation rings each have the existing P2.2 input capacity, `256` records. Input overflow increments the private dropped counter. The normal loop applies backpressure by delivering before decoding another message. A presentation producer gets `PLAT_ERR_FAIL` on saturation and retains/retries its record. (G) Message slices use the existing X11 bound of `64` iterations, with deadline checks between operations. Finite runs stop at their monotonic deadline even with buffered input; zero timeout is a nonblocking slice. Callback execution itself cannot be preempted.

`WM_PAINT` is validated using BeginPaint/EndPaint and queues EXPOSE. It performs no drawing. The renderer owns presentation. Focus changes reset composition, repeat and capture, and arm/disarm the configured blink interval. Waitable timers are one-shot, rearmed by the loop, avoiding idle repeat wakeups.

## Keys, text and generated tables

Run `python3 tools/win32_keysyms.py` from the worktree to reproduce `src/win32/keysyms.h`. The generator's explicit VK-to-XKB name pairs use Windows SDK `winuser.h` virtual-key names. Numeric XKB values and Unicode-to-legacy-keysym mappings come from the repository's `vendor/xkbcommon/xkbcommon-keysyms.h`. No host XKB installation is needed. Function and numeric keypad ranges are expanded by name. Unicode mappings are sorted for binary search; legacy aliases use the first vendored definition. Latin-1 maps directly, legacy non-Latin characters use their XKB values, and remaining scalars use XKB's Unicode keysym encoding.

KEYDOWN/SYSKEYDOWN and KEYUP/SYSKEYUP produce contract KEY events. Generic modifier VKs are resolved to left/right variants using the scan/extended bits. Printable keys use `ToUnicodeEx` with the message's effective keyboard state. For ordinary Ctrl/Alt chords, their bits are removed only in the symbol translation state: Ctrl+Shift+A has keysym A and no text. The event retains its command modifiers. Control characters and Ctrl/Alt/Super text are suppressed, as P2.2 requires. Numeric keypad keys retain their KP keysym and carry printable text when Num Lock produces it. Release events never insert text.

For Ctrl+Alt, a non-mutating `ToUnicodeEx` probe determines whether the layout produces printable text or a dead key. Such events become ALTGR and clear CTRL/ALT, so German AltGr+Q inserts `@`. Other Ctrl+Alt combinations remain chords. This decision is per key and layout, not a hard-coded German exception.

`ToUnicodeEx` owns the thread's dead-key state. Dead-key presses and releases are swallowed; the composed scalar is emitted once. Invalid compositions consume the cancelling key. Focus/layout changes flush native compose state with a neutral space. A composed result cancels repeat, matching the X11 compose path. (G) The non-mutating translation flag requires the project's Windows baseline, Windows 10 with the 1607-or-later ToUnicodeEx behavior.

The loop deliberately does not call `TranslateMessage`: doing so after ToUnicodeEx would translate twice, mutate dead-key state again and generate duplicate WM_CHAR text. Independent WM_CHAR/SYSCHAR input accepts UTF-16, including surrogate pairs; WM_UNICHAR accepts a Unicode scalar and answers the UNICODE_NOCHAR capability probe. WM_DEADCHAR is consumed because composition already belongs to ToUnicodeEx.

(M) The live suite exercised US `0000000004090409` and German `0000000004070407`; installed layouts also included `FFFFFFFFF0010809` (enumerated with GetKeyboardLayoutList). German AltGr+Q and acute+E passed. Layouts absent on another host are explicitly skipped with the installed-layout list printed. Other layouts and native IME composition are not claimed as tested.

## Repeat and timestamps

The brief's explicit requirement that repeat remain ours takes precedence over the earlier Windows design prose saying to use OS repeats. KEYDOWN/SYSKEYDOWN with lParam's previous-key-state bit set are dropped. A newest repeatable key arms our timer; modifier transitions update translation without replacing that key. Repeats use current keyboard modifiers without advancing compose state. Release, focus/layout reset, compose start/completion, or disabling repeat cancels it.

(G) Defaults are the existing contract's `400 ms` delay and `30 Hz` rate; the maximum is X11's `1000 Hz` clamp. (P) Repeat interval is `1,000,000,000 ns/s / rate_hz`. (G) Catch-up follows P2.2: at most the current interval and two missed intervals; longer stalls skip ahead instead of bursting. (P) Waitable timers use `100 ns` units; remaining nanoseconds are rounded up to those units. This changes no Linux repeat behavior.

(P) GetMessageTime and GetTickCount share the unsigned Windows millisecond epoch. Their wrap-safe difference gives queued-event age; subtracting age times `1,000,000 ns/ms` from `os_now_ns()` places ingress on the shared QPC monotonic clock. Repeats use current monotonic time. Events are never stamped later than ingestion.

## Pointer and clipboard

Button identities follow `plat.h`, with capture retained until all held buttons are released. Capture is acquired only when not already owned: setting capture to the same window again caused WM_CAPTURECHANGED to erase held-button state in the red live test. Capture loss clears the held mask.

(P) Wheel fixed-point displacement is `native_delta * PLAT_WHEEL_UNIT / WHEEL_DELTA`, where the frozen contract defines `PLAT_WHEEL_UNIT = 256` and Win32 defines `WHEEL_DELTA = 120`. Each axis retains the signed division remainder. Thus three native deltas of `40` sum to exactly one contract notch without rounding drift. Positive vertical native movement scrolls up, hence negation for contract +dy-down; horizontal native movement is already +dx-right. A native delta not divisible by WHEEL_DELTA is marked smooth. Windows wheel messages do not identify device provenance, so this denotes fractional wheel input, including precision devices, rather than proving touchpad hardware.

Clipboard writes convert strict UTF-8 to CF_UNICODETEXT and transfer the HGLOBAL to Windows only after SetClipboardData succeeds. Reads bound the UTF-16 scan by GlobalSize, convert to UTF-8 and queue success/failure for the loop; clipboard ownership loss queues the contract notification. Allocation occurs only in setup/clipboard operations. PRIMARY writes are successful no-ops; PRIMARY reads queue an explicit failed request and do not modify the Windows clipboard.

## DPI and display timing

Setup establishes per-monitor DPI awareness v2 before creating a window or querying display sizing. The already-established context is accepted only when it is v2. Initial client size is adjusted for DPI-aware nonclient borders. WM_DPICHANGED records the DPI and applies the supplied suggested rectangle without activation. WM_SIZE emits RESIZE; a DPI-only change still emits RESIZE when dimensions did not change. Display sizing uses the calling thread's active window monitor (primary before window creation), its work area and GetDpiForMonitor. DwmGetCompositionTimingInfo supplies the refresh rational; (P) interval is `1,000,000,000 * denominator / numerator` nanoseconds, with zero for unavailable timing.

The DPI regression uses synchronous SendMessageW with a live RECT. (M) PostMessageW rejected WM_DPICHANGED with Windows error `1159`: a pointer-bearing system message cannot be posted asynchronously. No rectangle address escapes the synchronous call.

The brief assumes an existing editor font-DPI update on RESIZE. Inspection found only geometry updates in `src/editor/editor.c`'s `nonkey()` and `editor_resize_now()`; runtime DPI is initialized in the startup/font path. The platform delivers the required RESIZE and retains the new DPI, but automatic font/cell-metric rescaling requires an editor integration decision. That code is outside this platform bead and was not changed.

## Presentation handoff for W1.2

`src/d3d11` was absent at the start of this worktree. Its glue can include `win32/private.h` and call:

```c
int win32_plat_present_complete(plat *p, uint32_t serial,
                                uint64_t ust, uint64_t msc);
```

Supply the matching frame identity, actual displayed-frame timestamp converted from QPC to `os_now_ns()` nanoseconds, and DXGI displayed-frame counter. Submission is not completion. The fixed mailbox is protected by an SRW lock and signalled with a manual-reset event; resetting while holding the lock avoids a lost producer wake. The UI loop delivers `on_present_complete(ud, serial, ust, msc)`. `plat_set_present_events(false)` discards queued records and suppresses later ones, without changing renderer ownership. A full mailbox returns an error for producer retry. The suite sends a record from an OS thread and verifies callback payload and UI-thread delivery.

The Windows timestamp convention is explicit here; X11's raw UST has a different native epoch/unit and is not converted by this module. W1.2/W2 must route this callback to the D3D11 completion consumer: the current editor callback is still GL-specific. No frozen render-interface change is needed.

## Coordinator decisions (session 22, 2026-10-26)

1. **Editor-side DPI/font-metric rescaling on `WM_DPICHANGED`** is the editor's concern (W2.4, the editor loop on os.h and the win32 platform); the platform delivers RESIZE with the new DPI as documented here.
2. **T6 wiring** between this module's documented entry and the D3D11 backend (edit-xgcn) is the W1 integration bead (sublimite.exe typing in one file on the Windows desktop), filed after W2.4 lands, since the editor loop must run on Windows first.
3. The Codex review (docs/reviews/edit-upwq.md) is triaged before landing; landing rebases over W0.5 and the other W1 trees (mk/win.mk lists kept from all).

## Round 2: review findings → change → test

This section supersedes the round-1 descriptions above where they differ: Windows now owns mutating text translation through `TranslateMessage`; clipboard I/O runs on a dedicated worker; refresh timing follows the HWND's output. The finding numbers are identifiers from [the review](../reviews/edit-upwq.md). Recorded red/green commands, transcript line numbers, verification results and coverage limits are in [the round-2 worker report](../worker-reports/edit-upwq-s12-r2.md).

| Finding | Change | Test |
|---|---|---|
| 1: sent-message sleep | Recheck the event ring after an empty `PeekMessageW` result and before blocking; flush pending text at the empty-queue boundary. | `sent`: helper-thread `SendMessageW(WM_CLOSE)` at the empty-queue boundary; assert no blocking wait over the queued event and no rescue wake. |
| 2: native modal loops | Service events, work, T6, repeat and blink from a guarded modal service path and native timer; coalesce RESIZE/EXPOSE to their newest state. Callback-depth guards prevent recursive application dispatch. | `modal`, `move`, `menu`: native sizing, moving and Alt+Space loops; a resize burst larger than the ring retains final geometry and CLOSE, delivers work/T6, and reports no dropped events. |
| 14: HWND affinity/lifetime | Record the creating UI thread. Wrong-thread UI status entries fail and void entries do nothing. Shutdown must be retried after the active pump returns and retains state if `DestroyWindow` fails; `WM_NCDESTROY` clears userdata. The documented T6 producer remains cross-thread. | `affinity`: wrong-thread run/configuration/shutdown leaves the HWND, userdata and configuration alive for owner-thread teardown. |
| 3: AltGr state | Detect layout AltGr capability without mutating translation state; normalize sided RightAlt consistently for key and pointer events, suppress only its synthetic Ctrl, and retain independently held Ctrl/LeftAlt. Explicit LeftCtrl+LeftAlt remains a command chord. | `altgr`, original German keyboard cases: mapped/unmapped German keys, navigation/pointer, explicit Ctrl+Alt, real Ctrl with AltGr, and ordinary RightAlt on US. |
| 4: text ownership | `TranslateMessage` is the mutating translation owner; `ToUnicodeEx` probes for symbols/repeat do not mutate compose state. Printable KEYDOWN waits for its native CHAR commit. Scan provenance distinguishes owned physical repeats from independent CHAR/UNICHAR commits. | `ownership`: an external translating dispatcher inserts once; standalone CHAR repeat counts and bit-30 UNICHAR commits are retained; original German compose/repeat cases remain green. |
| 5: packet/IME route | Translate `VK_PACKET` and `VK_PROCESSKEY` natively and exclude them from physical shortcut/repeat dispatch. Preserve default IME handling and isolate preedit/cancel state from committed text. | `packet`: foreground `KEYEVENTF_UNICODE` BMP/supplementary input; `ime`: native IME message commit/cancel isolation. A real IME engine is unavailable. |
| 6: multiscalar output | Accumulate native UTF-16 commits as validated UTF-8 and stream complete scalars through fixed event payloads. Repeat also streams the whole returned span. Multiscalar payloads have no single keysym; invalid dead-key composition remains consumed. | `multiscalar`: ordinary multiscalar output, a supplementary commit larger than one event payload, and complete repeated spans; original invalid-compose case. |
| 7: Alt+numpad | Preserve native Alt-numeric translation through Alt release; suppress intermediate digits/chords and owned repeat. | `altcode`: native Alt+numpad sequence commits its character once on Alt release. |
| 8: system keys | Pass Alt+F4, Alt+Space and appropriate Alt-release handling to `DefWindowProcW`. | `system`: Alt+F4 delivers CLOSE while the HWND remains live; `menu`: Alt+Space enters and cancels the native system menu. |
| 21: surrogate isolation | Clear incomplete UTF-16 state at independent UTF-32 commits, physical-key/IME and focus/layout boundaries; pair halves only with matching scan provenance and reject invalid scalars. | `surrogate`: intervening UNICHAR, orphan halves, focus reset and out-of-range scalar; `packet` and original Unicode cases exercise valid pairs. |
| 10: blocking clipboard work | A dedicated worker owns a message-only HWND, native clipboard access and bulk conversion. The UI queues copied jobs and receives signalled completions. Shutdown can detach a busy foreign operation using self-owned worker state without a platform pointer. | `clip-async`, `clip-foreign`: API-thread checks plus a separate delayed-rendering process and large payload; the UI work callback releases the foreign renderer without rescue. |
| 11: clipboard contention | Retry native open on the worker with bounded exponential backoff, interruptible by shutdown. Copy and paste share FIFO jobs; terminal outcomes are retained. | `clip-contention`, `clip-foreign-contention`: injected failures, a separate native holder, expiry and pending-operation shutdown. |
| 12: completion/data association | Each completed request owns its bytes until popped; install its receive buffer only when delivering that result. Clarify `plat_clip_data` lifetime in `src/x11/plat.h`; no public fields/signatures or Linux implementation change. | `clip-results`: success followed by PRIMARY plus a request submitted inside the success callback; the current pointer/data stays valid until the next completion is delivered. |
| 13: newline interoperability | Export lone LF as UTF-16 CRLF without doubling existing CRLF; import CRLF as LF and retain isolated CR. This implements the round-2 brief's explicit import-normalization requirement. | `clip-newlines`: inspect native exported UTF-16 and verify mixed-newline/supplementary-text round trip. |
| 23: embedded NUL | Reject counted text containing NUL before queueing; preserve the previous clipboard instead of silently publishing a prefix. | `clip-nul`: rejected embedded-NUL copy leaves the previous value intact. |
| 9: capture loss | Emit releases for held buttons at the last pointer position on capture theft/cancellation; clear each bit before delivery to avoid a duplicate release on normal capture teardown. | `capture`: same-thread capture theft, later buttonless motion, normal release and `WM_CANCELMODE`. |
| 15: display interval | Match `MonitorFromWindow` to the active `QueryDisplayConfig` source and invert its output refresh rational; return zero for unavailable or conflicting cloned-output timing. | `display`: distinct output-rate fixtures and a live HWND-output query. Physical mixed-refresh movement is unavailable. |

Clipboard policy: `plat_clip_set` success accepts an owned copy into the worker queue; it does not synchronously guarantee native publication. Successful copies are silent; terminal copy failures use CLIPBOARD code `2`, while paste success/failure uses the existing request completion codes. (E) The retry envelope uses `GetDoubleClickTime()` as a native tolerance heuristic, with backoff beginning at `USER_TIMER_MINIMUM` and doubling, capped by the remaining envelope. These are responsiveness choices, not measured latency guarantees. (G) Pending jobs are capped by the existing `WIN32_EVENT_CAP`; saturation returns failure. (G) UTF-8 chunks use the existing `PLAT_UTF8_MAX` payload bound and preserve scalar boundaries.

(M) The recorded final `make TARGET=win -j64 check` exited `0`: platform release and sanitizer PASS with no failures, the ASan probe fired, and the check recipe reported `4` release plus `4` sanitizer binaries. The recorded Linux `DISPLAY=:94 make -j64 check` exited `0` with LeakSanitizer enabled and `104` sanitizer test binaries, replay CLI checks and display-lock contract passing; final `make -j64 all` also exited `0`. Counts are the check recipes' emitted totals, quoted with transcript locations in the report. Measurements are `[AC, desktop Spiral]`; recorded near-completion `uptime` was `12:42:42 up 4:58`, load averages `0.53, 2.62, 4.52` (transcript L7350). The continuation only wrote documentation and did not rerun these checks.

Review findings 16, 17, 18, 19, 20, 22, 24, 25, 26 and the remaining coverage work in 27 stay with coordinator backlog triage. Regressions required by the scoped fixes overlap 27; they do not complete its broader hardware, fault-injection and saturation coverage. Real IME engine and physical mixed-DPI/mixed-refresh coverage still require a suitable environment. The existing editor DPI/font-metric and T6 integration assignments above remain in force. Landing must retain this clipboard source and the platform-test link rules alongside the other W1 `mk/win.mk` additions.
