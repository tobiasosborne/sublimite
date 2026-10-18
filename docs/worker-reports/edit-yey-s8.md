# edit-yey (s8): menu bar, worker report

The design is recorded in docs/decisions/edit-yey.md.

## Files touched
- New: `src/menu/menu.h`, `src/menu/menu.c` (static table, mnemonics, title columns, labels, commands); `src/editor/menubar.c` (state machine, Alt handling, keys, mouse, overlay paint); `tests/menu_test.c`; `docs/decisions/edit-yey.md`; this report.
- `src/keys/keys.h`, `src/keys/keys.c`: add `KEYS_ACTION_TOGGLE_MENU` and `keys_unbound()` (strokeless commands, keysym zero).
- `src/config/actions.inc`: add `toggle_menu`. `src/config/config.h`, `src/config/settings.c`: add `menu.visible` (boolean, default false), CONFIG_ENTRY_CAP +1, and an override fallback to the unbound table so `keymap.toggle_menu` can be bound.
- `src/palette/palette.h`, `src/palette/palette.c`: `palette_command()` factored out of `palette_init`; it falls back to `keys_unbound`.
- `src/editor/private.h`: embedded `menu` state, `menu_rows`, declarations.
- `src/editor/editor.c`: `editor_text_geometry` computes `menu_rows`/`tab_rows`; focus loss resets the menu; pointer events reach the menu first.
- `src/editor/input.c`: menu key routing after the palette; Toggle Menu dispatch; `editor_event_control` uses the chrome height (`tab_rows` rows).
- `src/editor/paint.c`: the tab strip moves to row `menu_rows` (compose, hit test, font tracking); menu paint runs on full and caret-only compositions; the scroll region is off while a drop-down is open.
- `src/editor/open.c`: `menu.open = -1`. `src/editor/preferences.c`: applies `menu.visible` on reload.
- `src/editor/scrollctl.c`: scroll-restore fix (see "Found").
- `tests/config_test.c`: `menu_test` case. `tests/editor_test.c`: `menu_contract` (+ `--yey-menu`).

## Red runs
```
tests/menu_test.c:1:10: fatal error: 'menu/menu.h' file not found
tests/config_test.c:196:17: error: no member named 'menu_visible' in 'struct config_settings'
tests/editor_test.c:4912:11: error: unknown type name 'menu_def'
```
First menu_test run with the module present: `menu_test:45: FAIL command && command->action == action` (Edit: Reindent has no default-argument command, so it was removed from the table).
Queued Escape plus a key in one turn, with the scroll fix reverted:
```
editor_test:63: FAIL rc == EDITOR_OK || rc == EDITOR_MORE
editor_test:5001: FAIL settle(e) == 0      (editor_step rc=-1 = SCROLL_ERR_ARG from editor_scroll_slice)
```

## Green runs (Not charging; load average 3.31-32.24 over the session, `uptime` stamped per run)
```
menu_test: table integrity, unique mnemonics, labels, commands and malloc guard passed
config_test: menu.visible default, values and wrong type passed
edit-yey menu bar: bare Alt release shows (held Alt inert), Alt+F, chords, shared Save As, Escape levels, Alt chords, mouse, menu.visible and malloc guard passed
```

## Verification (exit codes)
- `make -j3 all` (gcc release, -Werror): 0
- build/san/tests (ASAN_OPTIONS=detect_leaks=1): menu_test 0, config_test 0, config_editor_test 0, config_find_test 0, config_worker_test 0, keys_test 0, palette_test 0
- `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh build/san/tests/editor_test`: 0 ("editor_test: all passed")
- the same command with release `build/tests/editor_test` (malloc guard active): 0
- extra, because of the scroll fix: build/san/tests/editor_scroll_test under the same display wrapper: 0

## What remains
- Visual polish (edit-b0j): separators, submenus, hover, item mnemonics, and Font Larger/Smaller items, which need payload-carrying items. Preferences currently lists only Font Size (reset).
- Follow-up (coordinator decision): "shown while Alt is held" was dropped. Only a bare Alt press-and-release shows the bar; a held Alt or a bound Alt chord leaves the layout untouched; an unbound Alt+letter still opens its menu directly. The `held` state was removed. Red: `editor_test:4933: FAIL !e->menu_rows && e->tab_rows == strip && e->v.config.rows == text_rows` (bar shown on press). Green: "edit-yey menu bar: bare Alt release shows (held Alt inert), Alt+F, chords, shared Save As, Escape levels, Alt chords, mouse, menu.visible and malloc guard passed". The test asserts unchanged rows and `geometry_generation` across Alt press, Alt+c and release.
- The other display suites (editor_large_test, editor_session_test, editor_hot_exit_test, cli_test, raster_test) were not run. The menu is hidden by default, so their geometry is unchanged, but the coordinator may run them.

## Found outside the bead
- **A pre-existing crash path, fixed here because the menu triggers it often.** `editor_scroll_edit` restored `b->scroll = b->scroll_display`, including the viewport config. If a geometry change (Escape closing the menu or find panel, or a config reload) and a document mutation land in the same turn, the restored state keeps the old row count. `scroll_plan_frame` then returns SCROLL_ERR_ARG and the editor fails. The fix reapplies the current `config(e)` with `scroll_resize` after the restore when they differ. The find panel's Escape-then-type path most likely had the same exposure; it is not separately tested.
- `src/config/config.h`'s CONFIG_ENTRY_CAP comment counts its scalars by hand; it has been kept in step (+ menu.visible).

Follow-up verification (Not charging; load average 4.75-6.06): make -j3 all 0; build/san/tests/menu_test 0; build/san/tests/editor_test on :99 under the lock (EDIT_DISPLAY_LOCK_TIMEOUT=1800, detect_leaks=1) 0; release build/tests/editor_test (malloc guard) 0.
