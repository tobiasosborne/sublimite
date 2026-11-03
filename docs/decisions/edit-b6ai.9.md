# edit-b6ai.9: Wayland primary selection

- One transfer path for both selections (index = PLAT_CLIP_*), not a second copy of the clipboard code.
- The editor's publication entry is now backend-neutral `plat_clip_set_buf` (src/wayland/dispatch.c). Previously the editor called `x11_clip_set_buf` on a Wayland plat, which reads Wayland state as X11 state. Wayland adopts the editor's buffer (no 64 MiB copy on the UI thread); budget and ceilings are the clipboard's (WL_CLIP_*).
- Absent global: `plat_clip_set_buf(PRIMARY)` returns OK after freeing the buffer, so Ctrl+C / selecting reports no error; request/set(PRIMARY) still fail as before.
- Serial: `s->serial` (last input serial), as the clipboard path.
- Mime: text/plain;charset=utf-8 and text/plain only (same as CLIPBOARD).
