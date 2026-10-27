# X11 drop target

Advertise XDND version 5 (P: the brief's protocol version), accept only
`text/uri-list`, and reply with `XdndActionCopy`. The version field and inline
atoms follow the XDND wire layout. Timestamp forwarding starts at version 1;
`XdndFinished` starts at version 2 (P: XDND protocol). Future versions are
ignored. `XdndProxy` is deliberately unsupported in this bead.

`dnd.c` owns negotiation and replies. `XdndTypeList` is fetched with a pollable
XCB request, capped at the existing clipboard wire-slice ceiling; an incomplete
or malformed property is rejected. Pending Position/Drop messages wait for that
reply without blocking the UI. Enter/Position use the clipboard's existing peer
timeout, including its test override. Leave resets negotiation; Leave arriving
after an accepted Drop preserves the selection transfer. A conflicting source
is rejected without replacing the active source.

`clip.c` adds a dedicated receive slot alongside PRIMARY and CLIPBOARD (P:
independent protocol transactions). Selection, target, property and timestamp
are explicit for the drop conversion. The drop property lives on our target
window. INCR handling, paging, worker copies, capacity admission, cleanup and
timeouts use the existing receive machinery. No new transfer budget or timeout:
the existing 64 MiB data ceiling, 128 MiB shared budget, 256 KiB wire slice and
5 s peer timeout are inherited (P: `X11_CLIP_DATA_MAX`,
`X11_CLIP_OWN_BUDGET`, `X11_CLIP_CHUNK_MAX` and clipboard initialization).
URI-list bytes receive no text conversion or parsing here.

Completed drop bytes have their own charged blob, so clipboard completions
cannot invalidate them and drops cannot replace clipboard data. Publish a
successful drop only when the input queue can accept its event; preserve the
borrowed buffer through event delivery. A failed receive completes without an
event. Wayland shares the new event ABI and returns no drop bytes in dispatch;
Win32 has the same accessor stub. Their real implementations remain
`edit-cxho.19` and `edit-cxho.20`.

Nemo's actual offered type list is **unverified**. This session exercised a
hand-written raw-XCB source on private Xvfb, not a live file-manager drag.
No claim is made about observed Nemo offers or proxy behavior. Tobias's manual
Nemo/Nautilus/Dolphin open-tab check remains after `edit-cxho.6` supplies editor
wiring. The raw peer verifies both inline offers and `XdndTypeList` offers.
