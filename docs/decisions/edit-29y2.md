# edit-29y2: dark title bar, Mint-Y surface

2026-10-26, session 23, Tobias present on the Mint ThinkPad.

## Ask
"The top white window bar kind of sucks. I like the linux mint xterm look." After a live trial (the hint set by hand with `xprop` on his running window): "it is ok but now the sublimite bg colour bites the bar colour, pls make it follow."

## Findings
- The title bar is drawn by Cinnamon's window manager (Muffin), server side (`_NET_FRAME_EXTENTS` 0,0,64,0 on both windows). gnome-terminal gets the dark bar because it sets `_GTK_THEME_VARIANT(UTF8_STRING) = "dark"` on its window; sublimité set nothing, so Muffin drew the light Mint-Y variant.
- Bar colour: (P) `/usr/share/themes/Mint-Y-Dark-Aqua/gtk-3.0/gtk.css` `@define-color wm_bg #222226`; (M) sampled on :0 from an `xwd` capture: the bar and the gnome-terminal body are both #222226, i.e. the terminal is one surface.
- sublimité's dark theme used three blue-greys under that bar: page/gutter 0x20242c, tab strip 0x171b22, active tab 0x384356.

## Decisions
1. New `plat_set_dark(plat *, bool)`: X11 sets or deletes `_GTK_THEME_VARIANT="dark"`; Wayland is a no-op (no client hint for server decorations). Called before `plat_map` at startup (theme from the already-parsed preferences) and on every preference apply, so a hot-reloaded `theme = 'light'` removes the hint. One atom round trip per call: startup and reload only, never the typing path (law 2 unaffected).
2. Dark tokens: `editor.background`, `gutter.background`, `chrome.background` and `caret.foreground` are 0x222226 (Mint-Y-Dark wm_bg), so bar, tab strip, gutter and page read as one surface like the terminal. `tab.active.background` is 0x2e2e33 (Mint-Y-Dark theme_bg), a one-step lift. Foregrounds, selection and syntax colours are unchanged. The light theme is unchanged and gets no hint (light bar over light page).
3. The find panel, find-in-files rows, palette, menu dropdown, go-to/open/save-as prompt and the "not implemented" status row hard-coded the old dark literals (0x171b22, 0x20242c, 0xd8dee9, 0x8d98a9, 0xe5e9f0, 0xe5b56b, 0x53667e). They now read the equivalent theme tokens (same values in the old dark theme, so only the new surface colour changes them; as a side effect they follow the light theme too). `open.c`'s pre-preference layout defaults read the dark tokens instead of copies.

## Tests
- tests/config_test.c: the four dark surface tokens equal 0x222226 (red before the token change).
- tests/x11_identity_test.c: no hint after init; `plat_set_dark(true)` reads back "dark" as UTF8_STRING; `plat_set_dark(false)` deletes it (red: undeclared function).
- tests/editor_test.c minimap_typing_flash: the stale-orange blend probe derived its constant from the old chrome colour (0x6b5030 = rounded mean of 0x171b22 and 0xbf853d); it now computes the blend from the current chrome token so it keeps detecting instead of silently passing.
- Two more pins of the old page colour surfaced in the full check and now read the tokens: editor_test's minimap reference style (was 0x20242c) and editor_large_test's estimated-gutter colour (was 0x4c525d, now `editor_theme_estimated_gutter` of the dark gutter tokens). tests/gl_test.c renames the new `x11_plat_set_dark` in its private compile of x11.c, as it does for the other backend symbols.
- Full `make check`: exit 0, 103 test binaries, 12:02-12:13 (M)[bat] on the uncommitted tree; installed with `make install` 12:13.

## Not done
Chrome layout, typography, and the rest of the design workups (edit-b0j) are untouched; this is the "01 quiet tokens" direction applied to the surface colours only.
