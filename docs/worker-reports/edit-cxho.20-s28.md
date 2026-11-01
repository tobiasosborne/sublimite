# edit-cxho.20 + edit-osn3 (Win32 part), session 28

Verdict: done (live checks below remain). Decisions: docs/decisions/edit-cxho.20.md.

## Changes
- src/win32/win32_plat.c: DragAcceptFiles (+ UIPI filter only if elevated), WM_DROPFILES handler, uri-list builder, real `plat_drop_data`, WM_MOUSEMOVE arms TME_LEAVE, WM_MOUSELEAVE -> PLAT_EV_LEAVE, drop_data freed at shutdown.
- src/win32/private.h: `drop_data`, `drop_len`, `tracking`.
- src/dnd/dnd.c: `_WIN32`-only drive/UNC mapping to native paths.
- tests/win/win32_drop_test.c (new; built by the generic `tests/win/%_test` rule in mk/win.mk, so no mk edit was needed and the check recipe is untouched). 9 cases: ACCEPTFILES style, drive path with space + u-umlaut (exact URI and round trip), UNC, reserved chars, 133 files (IPC_MAX_PATHS+5: overflow counted), >260-char path, failed conversion (lone surrogate: no event, old data intact; mixed list keeps good entries; empty HDROP), real file created under %TEMP% opened through the parsed path (stat + CreateFileW), pointer leave.

## Red / green (tools/winrun.sh, private station)
- Red (before implementation): `win32_drop_test: FAIL (29 failures)`, exit=1 (rel); every drop case and pointer leave failed.
- Green: rel and san both `win32_drop_test: PASS (0 failures)`, exit=0.
- Note: TME_QUERY cannot prove arming here (with no pointer over the window the system answers immediately and clears the request), so the test accepts armed-or-LEAVE-delivered, plus an explicit WM_MOUSELEAVE giving exactly one LEAVE.

## Verification
See the coordinator message for exit codes (make all / check / TARGET=win all).

## Live check list (cannot be tested here)
1. Drag a file from Explorer onto the window: opens. Drag several, a folder (notice "folders ignored"), a file from a network share / `\\wsl.localhost` path, a file with spaces/non-ASCII/`#`/`%` in its name, a path longer than 260.
2. Same drag when sublimite runs elevated (UIPI filter).
3. Pointer leaves the window: hover highlight and I-beam clear (move out fast, over the title bar, and while a button is held).
4. Drop onto a window on a second monitor / non-100% DPI (drop point coordinates are client pixels).

## Found, not fixed
- `make all` on Linux fails in this tree: Makefile:190 compiles tools/win_quiet_run.c (windows.h) under gcc (from edit-177r.10, d451648); not mine.
- editor.c ~640 uses `gethostname` and `stat`/`S_ISDIR` with POSIX headers; needs Winsock init or a Windows branch (the dnd hostname argument may be NULL).
- Entries that cannot be converted are dropped silently (no editor notice): the editor cannot see them.
