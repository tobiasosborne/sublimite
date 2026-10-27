# Mode x action matrix (sublimite, 2026-10-27)

Legend: OK / WEIRD / BROKEN. V = verified on Xvfb :94 (shots in shots/). P = predicted from code
(input.c:1030-1078, findpanel.c:262-398, palettepanel.c:30-46, menubar.c:66-207, buffers.c:596-632, editor.c:571-606).
Sublime rule: panels are non-modal; global shortcuts always act on the window/active view; clicking the view moves focus to the view; arrows/Esc return focus.

| Mode \ Action | Ctrl+W close | Ctrl+Tab / Ctrl+PgUp | Ctrl+S / Z / C / X | Ctrl+G / Ctrl+F | Click in text | Typing | Menu / palette command | Middle-click |
|---|---|---|---|---|---|---|---|---|
| Find panel | BROKEN V: no-op (shots/c02) | BROKEN V: no-op (d06) | BROKEN P: swallowed findpanel.c:352 | Ctrl+G dead V (c04); Ctrl+F reselects OK | WEIRD V: caret moves, focus stays in field (c06) | WEIRD V: after a click, text goes to the query | BROKEN P: File>Close, Edit>Undo dead (synthetic re-dispatch menubar.c:66) | WEIRD P: pastes into doc |
| Replace in progress | P refused, parked | parked | parked | parked | parked | parked | palette refuses | parked; 1024 cap fatal risk (unreproduced) |
| Find in files | as find | as find | as find | as find | as find | as find | as find | Esc: first press cancels the job only, panel stays (WEIRD P) |
| Palette | WEIRD P: swallowed | WEIRD P | swallowed | Ctrl+Shift+P cannot toggle | WEIRD P: caret moves behind palette | OK (query) | n/a | BROKEN P: pastes into doc |
| Menu bar | OK P | OK | OK | OK | OK (closes bar) | swallowed (mnemonic) | OK | WEIRD P |
| Go-to / Open / Save As prompt | WEIRD V: dead, no hint (d05) | WEIRD V: dead (d05) | ignored | ignored | OK: cancels then clicks | OK | menu blocked | P pastes |
| Close-modified prompt | OK V: prompt shown (d01) | swallowed | swallowed | WEIRD V: Ctrl+F ignored (d02) | text click acts, tab click cancels | swallowed OK | Alt cancels prompt silently (WEIRD P) | BROKEN P: pastes |
| Source-changed R/K | passes | passes | save passes, rest dropped silently | WEIRD P: dropped, no feedback | ignored silently | dropped | menu blocked | n/a |
| Pending open / parked queue | parked | parked | parked | parked | parked | parked | parked | INPUT_FULL fatal risk (unreproduced) |
| Minimap / tab drag | drag ignores overlays (WEIRD P) | | | | | | | |
| Drag-and-drop from Files | no XdndAware anywhere (BROKEN P: no plat event kind) | | | | | | | |

## Verified on Xvfb (display :94, frozen binary, private XDG dirs)
1. Find open (Ctrl+F, "alpha"), Ctrl+W: tab b.txt stays, nothing happens. (c02)
2. Find open, Ctrl+PgUp / Ctrl+Tab / Ctrl+S / Ctrl+Z / Ctrl+G: no tab change, no go-to prompt. (c03, c04, d06)
3. Find open, click in text, type "ZZ": query became "alphaZZ", document untouched; caret moved in doc. (c06)
4. Modified tab + Ctrl+W (find closed): close prompt shown OK. (d01)
5. Close prompt shown, Ctrl+F: ignored, prompt stays. (d02)
6. Go-to prompt, Ctrl+W then Ctrl+Tab: prompt stays, tab unchanged, no feedback. (d05)
7. Find open, tap Alt: menu bar appears above the tab row while the find row stays; File highlighted. (d07)
8. Palette open (Ctrl+Shift+P), click in text: palette rendering showed odd "Right / Right" rows (d04), click effect not conclusively isolated.
Not run: menu File>Close with find open, middle-click paste under palette, minimap drag (code reading only).

## Principles
1. Panels are non-modal: the find panel gets an explicit focus flag (field vs document); only field-owned keys (text, Left/Right/Home/End/BackSpace/Delete, Ctrl+A, Ctrl+V, Enter variants, Tab in replace mode, Esc) go to the field.
2. Every bound global command (close, tab nav, save, undo/redo, goto, palette, menu item) reaches the dispatcher regardless of panel state; menu and palette must not re-enter the sink.
3. A click in the document moves focus to the document and leaves the panel and highlights; Ctrl+F or a click in the panel refocuses it; Esc closes.
4. Tab switch/close keeps the panel open and retargets it (already the behaviour).
5. One computed "active modal" enum owns the status row and gates mouse: true modals (close prompt, goto, palette) swallow or dismiss on mouse press; middle-click paste respects it.
6. Parked input is bounded (drop motion, coalesce wheel) and user-command rejections are never fatal.

## Table-driven test
tests/modal_matrix_test.c: rows {setup(mode), input(key or click), expect(tab_count, active_id, revision, find_visible, find_focus, query, go.active, palette_open)}. Table doubles as the behaviour doc. Red first: Find x {Ctrl+W, Ctrl+Tab, Ctrl+PgUp, Ctrl+S, Ctrl+Z, Ctrl+G, menu File>Close}, click-then-type.
Note tests/findui_test.c:1019-1024 pins x,z,y,s,w,t as consumed and must be inverted.
