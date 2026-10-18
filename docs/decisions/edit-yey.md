# edit-yey: menu bar

**Table** (`src/menu`). There is one static `menu_def` per title: File, Edit, Selection, Find, View,
Goto, Tools, Preferences, Help (`MENU_COUNT`, the brief's list). Each one
holds a `const keys_action[]` of actions taken from `config/actions.inc`. Menus are
flat: no submenus or separators (the visual pass is edit-b0j). An item
dispatches the palette's default-argument command. That lookup is factored out as
`palette_command()`, so the palette and menu share one source. Labels are the
palette's labels (`palette_label`). Chords use `keys_binding_for_command` +
`keys_chord_format` against the live overrides, so unbound items show no
chord. Reindent has no default-argument command (both of its strokes carry
variants), so it is not listed. Mnemonics follow Sublime: F, E, S, F*i*nd, V, G,
T, Prefere*n*ces, H. Sublime uses P for Project, which has no actions here. Help
holds Command Palette because no help actions exist.

**Toggle Menu.** The brief asks for a View item that pins the bar, and it also
says no action may exist only in the menu. So `KEYS_ACTION_TOGGLE_MENU` /
`toggle_menu` was added to the enum and `actions.inc`. Sublime binds no default
key to it, so it lives in a new `keys_unbound()` table: keysym zero, which no
lookup ever matches. `palette_command` and config `override()` fall back to that
table. As a result the palette lists it, and `keymap.toggle_menu = '...'` binds it. No other
action was added.

**Disabled items.** The only enable state the editor already exposes is the
`KEYS_ARG_UNIMPLEMENTED` flag on a command. Those items are drawn dim, and they are
still selectable: they reach the existing "not implemented" notice. Every other item is
enabled (Copy with no selection copies the line).

**Alt semantics.** State: `pinned` (`menu.visible` or Toggle Menu), `armed`,
`active` (the bar has keyboard focus), `open` (the drop-down index, or -1). The bar is
visible when `pinned`, `active` or `open` is set.
- A bare Alt press (no Ctrl/Shift/Super/AltGr in the press's pre-transition
  modifiers) only arms. It never shows the bar or changes the layout. The press
  still falls through to the existing inert handling.
- Any other key press disarms. An Alt+key with a binding (default
  or override) always runs its chord. An Alt+letter with no binding that matches a
  mnemonic opens that menu. Shift excludes it.
- An Alt release that is still armed latches focus on the bar. If the bar
  already had focus at the press, the release closes it instead.
- While focused, the bar owns the keys. Left/Right switch titles (and the open menu).
  Down/Enter open the menu, Up/Down move within it, Enter runs the item, and letters open menus. Escape
  closes one level (first the drop-down, then the bar). A Ctrl/Super shortcut
  closes the menu and then runs. Other keys are swallowed.
- When nothing is open or focused, the bar steals no key (even when it is pinned).
- Focus loss resets everything. While the source is stale or the go-to prompt is
  up, the menu is ignored.

The brief's "shown while Alt is held" was dropped by coordinator decision
(2026-10-18). Showing the bar on the press made every bound Alt chord (such as Alt+1)
shift the text down one row and back. Now only a bare press-and-release shows the bar, and a
bound Alt chord leaves the layout untouched. The test asserts the row count and
`geometry_generation` across Alt press, chord and release.

**Dispatch.** Enter or a click closes the menu, synthesizes the command's own stroke,
and calls `editor_dispatch_action`, exactly as the palette does. Toggle Menu is
dispatched first in that function.

**Row placement.** `editor_text_geometry` recomputes `menu_rows` (one row when
the bar is visible and at least one text row remains) and `tab_rows = strip +
menu_rows`. `tab_rows` therefore means "all rows above the text", and every
existing text offset keeps working. The tab strip moved to row `menu_rows` (in
`compose_chrome`, in the chrome pointer hit test and in font tracking). Hidden costs zero
rows. Each visibility change is one full geometry pass, the same as a find panel
opening.

**Overlay.** The drop-down starts below the bar at its title's slot, shifted left
to fit, and its rows clip and scroll like the palette's. `editor_menu_paint` runs at the end of
every composition, including caret-only frames. It writes cells with change
detection and drops `caret_only` when it changes a cell. The scroll region is
disabled while a drop-down is open, as it is with the find panel. Mouse: a click on a
title opens or closes it, a click on an item runs it, and any other click closes a
focused menu and is consumed. Hover is polish.

**Scroll restore (found while testing).** `editor_scroll_edit` restored
`b->scroll` from `scroll_display`, including its viewport config. Suppose a geometry
change (Escape hiding the bar, or a config reload) and a mutation happen in the
same turn. The edit then reinstated the old row count, and the next frame's plan
failed with SCROLL_ERR_ARG, which made the editor fail. The fix: after the
restore, the current `config(e)` is reapplied with `scroll_resize` when it
differs. The find panel's geometry changes share this path.
