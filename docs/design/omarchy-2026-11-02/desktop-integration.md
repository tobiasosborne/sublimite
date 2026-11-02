# Omarchy desktop integration (research, 2026-11-02)

Scope: how a native app fits Omarchy's Hyprland desktop, and where sublimite's chrome clashes. The theme/palette system is covered by another report. Sources are the live install on this box (Omarchy `/usr/share/omarchy/version` = 4.0.0.alpha, Hyprland 0.56.2 from `hyprctl version`) plus the Hyprland source on GitHub. Nothing was built or launched: no `build/sublimite` binary exists, so every "how it looks" claim below is derived from code and config, not from a run.

Battery stamp: `/sys/class/power_supply/BAT0/status` does not exist (desktop box, AC). No measurements were taken.

## 0. Corrections to the brief

- This install has no walker, mako, swayosd or waybar. `pacman -Q` reports all four "not found". Popups, bar, menu, notifications, OSD and polkit are one Quickshell instance (`pacman -Q quickshell` = 0.3.2; `/usr/share/omarchy/shell/README.md`: "`omarchy-shell` is a single long-running Quickshell instance"). Layer namespaces seen live via `hyprctl layers`: `omarchy-bar`, `omarchy-background`.
- Hyprland config is Lua (`~/.config/hypr/hyprland.lua`, `hl.config{}`, `o.window{}`), not hyprland.conf.
- The online manual (learn.omacom.io/2/the-omarchy-manual) says the launcher is Super+Space and the Omarchy menu Super+Alt+Space. The local bindings say Super+Space = `omarchy-menu toggle` (root menu) and Super+Alt+Space = "Apps menu" (`/usr/share/omarchy/default/hypr/bindings/utilities.lua`). Local wins.

## 1. Window look

All from `/usr/share/omarchy/default/hypr/looknfeel.lua` and live `hyprctl getoption`:

| Item | Value | Source |
|---|---|---|
| gaps_in / gaps_out | 5 / 10 | looknfeel.lua `gaps_in = 5, gaps_out = 10`; `hyprctl getoption general:gaps_in` = `5 5 5 5`, `gaps_out` = `10 10 10 10` |
| border_size | 2 | `general:border_size` = 2 |
| active border | theme colour, live `#7aa2f7` (solid) | `hyprctl getoption general:col.active_border` = `ff7aa2f7`; written by `~/.local/state/omarchy/current/theme/hyprland.lua`. Omarchy's built-in fallback is a 45deg gradient `33ccffee`->`00ff99ee` (looknfeel.lua line 3) |
| inactive border | `rgba(595959aa)` | looknfeel.lua line 4, live `aa595959` |
| rounding | 0 (square) | `decoration:rounding` = 0; manual: "Omarchy's default design is one of square corners" |
| shadow, blur | both disabled | looknfeel.lua `shadow = {enabled=false}`, `blur = {enabled=false}`; live `decoration:blur:enabled` = false |
| animations | on: windowsIn "popin 87%" speed 4.1, fade, layers fade; workspaces animation disabled | looknfeel.lua animation lines |
| resize_on_border | false | looknfeel.lua; live false. No edge-drag resize; resize is Super+right-drag or Super+arrow keys |
| opacity | every window gets tag `default-opacity` -> `opacity = "0.985 0.96"` (active, inactive) unless an app rule removes the tag | `windows.lua`: `o.window({ tag = "default-opacity" }, { opacity = "0.985 0.96" })`; live: foot windows carry tag `default-opacity*` |
| groupbar | exists (Super+G), height 22 | looknfeel.lua `groupbar` |

Title bars: Hyprland draws none. There is no window-rule or config in Omarchy that adds one. The only chrome is the 2px border and the gaps. The top 26 px of each monitor is reserved by the shell bar (`hyprctl monitors -j` `reserved: [0,26,0,0]`; `omarchy-bar` layer is 26 high).

xdg-decoration: Hyprland supports `zxdg_decoration_manager_v1` and `org_kde_kwin_server_decoration` (`strings /usr/bin/Hyprland`). Per Hyprland source `src/protocols/XDGDecoration.cpp` (fetched, lines 19-48): it sends SERVER_SIDE at creation (`xdgDefaultModeCSD()` returns `ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE`), and answers every `set_mode` and `unset_mode` with SERVER_SIDE ("Ignoring and sending MODE_SERVER_SIDE as reply"). "Server-side" here means the compositor draws nothing: the app must draw no title bar and gets the border only. An app that never binds xdg-decoration (sublimite's Wayland backend does not) is not told anything and simply has no titlebar either, because it draws none.

Apps that do draw CSD: GTK3/4 headerbars and Chromium's own frame draw themselves when they do not use xdg-decoration. Omarchy does not hide or restyle them. I found no `gtk.css`, `headerbar` or `decoration-layout` handling anywhere under `/usr/share/omarchy`, and `~/.config/gtk-4.0` does not exist. Only `omarchy-theme-set-gnome` sets colour scheme; live `gsettings`: color-scheme `prefer-dark`, gtk-theme `Adwaita-dark`, `button-layout` `appmenu:close` (a close button only). Omarchy's own terminals opt out of frames: `config/alacritty/alacritty.toml` line 18 `decorations = "None"`, padding 14x14 (lines 16-17); `config/foot/foot.ini` `pad=14x14`; ghostty `window-padding-x/y = 14`. Chromium app windows are tiled (`browser.lua`: `tile = true`).

## 2. Window rules

Rules live in `/usr/share/omarchy/default/hypr/apps/*.lua` and `windows.lua`. Match is a regex on class (= Wayland app_id, or the second WM_CLASS string for XWayland); `terminals.lua` notes "The class is matched in full".

- Global: `o.window(".*", { suppress_event = "maximize" })`; all windows tagged `default-opacity` (`windows.lua`); empty-class empty-title XWayland floats get `no_focus` (`windows.lua` lines 8-17).
- `terminals.lua`: tag `+terminal` for `Alacritty|kitty|com.mitchellh.ghostty|foot|org\.codeberg\.dnkl\.foot|wezterm|org\.omarchy\..*|TUI\..*`.
- `browser.lua`: chromium/firefox families tagged and given `opacity = "1.0 0.985"`, `tile = true`.
- `system.lua`: tag `floating-window` -> `float`, `center`, `size = {875, 600}` for `org.omarchy.btop|org.omarchy.terminal|org.omarchy.bash|org.codeberg.dnkl.foot|org.gnome.NautilusPreviewer|org.gnome.Evince|Omarchy|About|TUI.float|imv|mpv`, and for `xdg-desktop-portal-gtk`.
- `system.lua` line 18: `class = "(sublime_text|DesktopEditors|org.gnome.Nautilus)"` with title `^(Open.*Files?|Open [F|f]older.*|Save.*Files?|Save.*As|Save|All Files|.*wants to [open|save].*|[C|c]hoose.*)` -> floating. Real Sublime Text is already special-cased for its file dialogs. sublimite's class `sublimite` does not match, and it has no native dialogs, so nothing is needed.
- Media/video apps get `opacity = "1 1"` and tag `-default-opacity` (system.lua, steam.lua, qemu.lua, retroarch.lua, davinci-resolve.lua).

What sublimite needs: Wayland `app_id` = `sublimite` (`src/wayland/wayland.c:484` `xdg_toplevel_set_app_id(s->top, "sublimite")`); X11 `WM_CLASS` = `sublimite\0sublimite` (`src/x11/x11.c:576-578`); `tools/sublimite.desktop` has `StartupWMClass=sublimite`. Those three agree, and the .desktop basename is `sublimite.desktop`, which is what a Wayland app_id needs to match for icon lookup. No rule is required to work. A rule is optional and would be a user-level one in `~/.config/hypr/` (or an Omarchy default added upstream): the only decision is opacity (section 7, Q2). The tiling default (new editor window splits the workspace) is already what Omarchy users expect of an editor.

## 3. Wayland vs XWayland, scaling

- `DISPLAY=:0` here is XWayland: `pgrep -a Xwayland` -> `Xwayland :0 -rootless -core ...`; `WAYLAND_DISPLAY=wayland-1`.
- Omarchy forces Wayland for toolkits: `/usr/share/omarchy/default/hypr/envs.lua` sets `GDK_BACKEND="wayland,x11,*"`, `QT_QPA_PLATFORM="wayland;xcb"`, `MOZ_ENABLE_WAYLAND=1`, `ELECTRON_OZONE_PLATFORM_HINT=wayland`, `OZONE_PLATFORM=wayland`, `XDG_SESSION_TYPE=wayland`. XWayland is the fallback, not the intent: a native app should be a Wayland client.
- Scale: `hyprctl monitors -j`: DP-9 3840x2160 scale 1; HDMI-A-1 1920x1080 scale 1. `~/.config/hypr/monitors.lua` sets `scale = "auto"` plus `GDK_SCALE=2`. Omarchy's own scale steps are `SCALES=(1 1.25 1.6 2 3 4)` (`bin/omarchy-hyprland-monitor-scaling`), so fractional scale is a normal Omarchy state on laptops even though this box is at 1.
- `xwayland:force_zero_scaling = true` (envs.lua; live true): XWayland clients are never compositor-scaled, so an X11 app on a 1.6x monitor draws at 1x pixels (small) and must scale itself.
- sublimite backend choice: `src/wayland/dispatch.c:22-23` `plat_backend_select`: if `DISPLAY` is non-empty -> X11, else if `WAYLAND_DISPLAY` -> Wayland. **On this box DISPLAY=:0 is always set, so sublimite runs as an XWayland client by default.** The Wayland backend is only reachable with `env -u DISPLAY sublimite`. `main.c:104-130` guards only act when `EDIT_REFUSE_REAL_DISPLAY` is set; no `EDIT_*` variable is set in this shell (`env | grep ^EDIT_` empty).
- Wayland backend builds here: the Makefile probe (`Makefile:18`) succeeds (`gcc -E` on wayland-client.h, wayland-egl.h, xkbcommon.h printed `WAYLAND_AVAILABLE=yes`; wayland-server.h also present). EGL: `ldconfig -p` lists libEGL.so.1, libEGL_nvidia, libEGL_mesa; GPUs are two GTX TITAN X (`lspci`). Whether it runs correctly under Hyprland is untested: `src/wayland/STATUS.md` says "No compositor exists in this sandbox" and "Missing: a Wayland CPU raster renderer" (`raster.c:376` returns UNSUPPORTED for Wayland CPU), so Wayland needs working GL plus `wp_presentation` (Hyprland offers presentation-time; I did not verify the global list on this box, there is no `wayland-info`).
- Wayland scale handling is integer only: `src/wayland/output.c:29-30` takes `wl_output.scale`; only xdg-shell and presentation protocols are vendored (`src/wayland/protocol/`: presentation-*, xdg-shell-*). No fractional-scale, viewporter, xdg-decoration, cursor-shape, xdg-activation, primary-selection, text-input. At Omarchy's 1.25 and 1.6 steps the compositor will resample the integer-scaled buffer (blurry text); I did not confirm what integer Hyprland advertises for fractional outputs.
- Wayland cursor: `src/wayland/wayland.c` never calls `wl_pointer_set_cursor` (grep empty) and `plat_set_cursor` returns early for Wayland (`dispatch.c:48-49`, "cursor-shape-v1 is not vendored"). Expect no I-beam, and possibly no cursor image at all; needs a test on Hyprland.

## 4. Launching, defaults, how an app appears

- Default editor: a file `~/.local/state/omarchy/defaults/editor` holds one command name (absent on this box, so it falls back to `nvim`). `bin/omarchy-default-editor` (case at lines 18-28) writes it only for a fixed list `code|cursor|zed|sublime_text|helix|vim|emacs|nvim`; any other argument prints usage and exits 1. Setting it by hand works: `printf 'sublimite\n' > ~/.local/state/omarchy/defaults/editor`.
- `bin/omarchy-launch-editor`: reads that file; if the command is not on PATH it falls back to nvim (`omarchy-cmd-present`); for nvim/vim/nano/micro/hx/helix/fresh it opens a TUI; for anything else it runs `exec setsid uwsm-app -- "$editor" -- "$@"`. So sublimite is launched as `sublimite -- <paths>` detached under uwsm. sublimite already accepts `--` and `path[:line[:col]]` (`src/main.c:64` usage; `src/ipc/args.c:174`), and a second launch hands off to the running instance unless `--new-instance` (`main.c:132`).
- Bindings that call it: Super+Shift+N = "Editor" (`bindings/applications.lua`); `omarchy-launch-config-editor` (every "Setup > Config" menu entry, e.g. `omarchy-menu.jsonc` lines 106, 121-123, 181-183) shows a toast then runs `omarchy-launch-editor <file>`. Lua/conf files edited from the Omarchy menu would open in sublimite.
- `$EDITOR` is `omarchy-launch-editor --inline` (live env). With `--inline` and a GUI editor the script still does `exec setsid uwsm-app -- ...` with no wait flag, so `git commit` would get an editor that returns immediately. sublimite has `--wait` (usage string, main.c:64) but Omarchy never passes it. Same limitation applies to VS Code/Zed in stock Omarchy.
- Menu entry: `omarchy-menu.jsonc` has Defaults>Editor rows (lines 159-167) and Install>Editor rows (204-232) for the fixed list. The user extension file `~/.config/omarchy/extensions/omarchy-menu.jsonc` (exists, header documents the format: dotted id = path, fields `icon label action when checked`) can add `setup.default.editor.sublimite` with `"action":"printf 'sublimite\\n' > ~/.local/state/omarchy/defaults/editor"`; the editor-name check (`omarchy-default-editor`) would not report it, so the "checked" test needs `cat` of the state file. MenuModel's GUARD_READERS list (MenuModel.js lines 381-388) caches `omarchy-default-editor` output.
- MIME: Omarchy's `default/applications/mimeapps.list` maps `text/plain`, `text/x-c`, `text/x-csrc`, `application/xml`, `application/x-shellscript` etc. to `nvim.desktop`. The user's `~/.config/mimeapps.list` here has none of the text types. `make install` already runs `xdg-mime default sublimite.desktop text/plain` (`Makefile:180`); `xdg-mime query default text/plain` currently prints `nvim.desktop`. Other text types (text/x-c etc.) are listed in `tools/sublimite.desktop` MimeType= but not set as default by the install rule.
- `.desktop` convention: Omarchy launches apps via `uwsm-app -- ...` and `gtk-launch <desktop-id>`. The app list ("Super+Alt+Space") is Quickshell `DesktopEntries` (`shell/plugins/menu/Menu.qml:289`), which reads standard XDG .desktop files incl. `~/.local/share/applications`. `tools/sublimite.desktop` (Exec=sublimite %F, Icon=sublimite, Categories=Utility;TextEditor;Development;) will appear there once `make install` has copied it (not installed now: `~/.local/bin` has no sublimite, `~/.local/share/applications` has no sublimite.desktop). `StartupNotify=false` is fine.

## 5. Keybindings

Omarchy binds almost everything on SUPER. Non-SUPER global binds (grep of `default/hypr/bindings/*.lua`):

- `ALT + TAB` / `ALT + SHIFT + TAB` (focus next/prev window, tiling.lua), `CTRL + ALT + TAB` / `CTRL + ALT + SHIFT + TAB` (monitors), `CTRL + ALT + DELETE` (close all windows), `PRINT`, `ALT + PRINT`, `F9` (only if `voxtype` installed; voxtype.lua line 1-4), XF86 media keys, and `RETURN`, `CTRL+RETURN`, `TAB`, `CTRL+TAB`, arrow keys that exist only while a screenshot-selection layer is open (utilities.lua).
- Everything else starts with SUPER: Super+C/V/X universal clipboard, Super+W close, Super+F fullscreen, Super+T float, Super+J split, Super+1..0 workspaces, Super+Tab workspace cycle, Super+Space menu, Super+K keybindings, etc.

Clash check against sublimite defaults (`src/keys/keys.c`):

- Ctrl+Shift+P (line 157), Ctrl+P (line 100), Ctrl+G (line 91), Ctrl+Tab (line 112), Ctrl+PageUp/Down (119-120), Alt+1..9 (185-193), Alt+W/R/C, Alt+arrows, Shift+Insert/Ctrl+Insert (55, 122), Shift+Delete (63): **no clash**. Omarchy does not bind plain Ctrl or plain Alt+digit globally. Ctrl+Tab is only bound on SUPER+CTRL+TAB.
- Alt+Tab never reaches the app (compositor grabs it). The Alt-tap menu reveal is unaffected (the Alt key press itself still arrives) but Alt+Tab will flash it open; check `menubar.c` alt-tap logic (`alt_key`, `armed`) does not open the menu on Alt-down then Tab.
- sublimite binds no SUPER chords (grep: no SUPER entry in the default binding table); `editor/input.c:1049` returns false for any event with SUPER, so Super+anything is passed through harmlessly.
- Universal clipboard: `bindings/clipboard.lua` Super+C/V/X dispatch `send_key_state` with explicit mods to the focused window: Ctrl+C / Ctrl+V / Ctrl+X for normal windows, Ctrl+Insert / Shift+Insert for tagged terminals, so sublimite just needs plain Ctrl+C/V/X to work, and keeps Shift+Insert/Ctrl+Insert (already bound). The injected events can arrive with the physical Super still held (the file comment: "the physically held SUPER merges into the injected chord at the seat"); `input.c:1013` and `palettepanel.c:47` treat Ctrl as a modifier that blocks text insertion, so no problem, but an editor action handler that rejects chords containing SUPER (input.c:1049) could drop Super+V-injected Ctrl+V. Test on Hyprland. Super+Ctrl+V opens Omarchy's clipboard history (a shell layer); that history is Omarchy's own, not sublimite's.

## 6. Notifications, menus, OSD

All are Quickshell surfaces sharing one style singleton, `shell/Commons/Style.qml`:
- Corner radius = Hyprland `decoration:rounding` (Style.qml header: "`cornerRadius` mirrors Hyprland's `decoration:rounding`"), i.e. 0 here; screen-edge gap = half of `gaps_out` (header: "`gapsOut` is half of Hyprland's `general:gaps_out`", so 5 px here).
- Border 2 px: `Menu.qml:91` `Border.surfaceSpec("menu","border", border, Math.max(1, Style.space(2)))`; OSD the same, `Osd.qml:148`.
- Menu: layer namespace `omarchy-menu`, card width `Style.space(300)` (520 for two sub-menus), centred horizontally and vertically, row height 28 (`Style.spacing.popupRowHeight` default 28), padding 14 (`popupPadding`), `Menu.qml:112-114,1036-1061`.
- OSD: `omarchy-osd`, bottom-centre, `anchors.bottomMargin: Style.space(67)` (`Osd.qml:146`).
- Typography: `fontFamily = "monospace"` (the fontconfig alias, live `fc-match monospace` = JetBrainsMono Nerd Font), base size 12 px (`Style.qml fontBaseSize: 12`), tokens body 12, title 14.
- State fills: normal 4 %, hover 8 %, selected 18 %, selection 35 % alpha (Style.qml lines 70-80 `normalFillAlpha 0.04`, `hoverFillAlpha 0.08`, `selectedFillAlpha 0.18`, `selectionFillAlpha 0.35`); border alpha 0.4/0.25/1.0.
- These layer rules skip compositor animation: `apps/omarchy-shell.lua` `no_anim = true` for menu/emojis/clipboard panels (they pop instantly).

An in-app panel (find bar, palette, prompts) should echo: square corners (or read `hyprctl getoption decoration:rounding` as the shell does), 2 px border in the popup border colour, monospace at the system font, 4/8/18 % alpha state fills, no fade/slide animation, 14 px inner padding.

## 7. sublimite's own chrome

Does sublimite draw a title bar or frame? **No.** Evidence: `src/wayland/wayland.c:482-484` creates a bare xdg_toplevel (title + app_id only), no decoration protocol; `dispatch.c:44-47` `plat_set_dark` is a Wayland no-op "no client hint for server decorations"; `x11.c:568-581` sets only name/class/pid/protocols. The chrome is a cell grid: optional menu bar (row 0, hidden by default, `config.h:60` `menu.visible default false`), tab strip (one row, `editor/paint.c:145-170`, tabs are 16 cells wide: `tabs_strip{... 16 ...}` at paint.c:150), text, minimap, find/palette panels as grid rows, and the bottom row doubling as status line (`paint.c:261`).

Under Hyprland today (inferred, not run): launched with default env (`DISPLAY=:0`) it is an XWayland tiled window, class `sublimite`, no title bar, 2 px border, 10/5 px gaps, with `default-opacity` 0.985/0.96. The theme was designed against a Linux Mint WM title bar.

Conflicts, in rough order of visual impact:

1. **Theme assumes a WM title bar above it.** `src/config/theme.c:5-9` ("(M) the window manager's dark title bar #222226 (edit-29y2, Mint-Y-Dark wm_bg), so title bar, tab strip and page are one surface in the dark theme"); the dark `editor.background`, `gutter.background`, `chrome.background` are all 0x222226 (theme.c:12,15,21). Under Hyprland there is no title bar: the tab strip becomes the window's top edge, adjacent to a 2 px border and the 26 px bar. No reason for the match remains, and a blended-in strip has no top separation.
2. **`plat_set_dark` / `_GTK_THEME_VARIANT` is dead code under Hyprland.** `x11.c:637-646` sets the property for Muffin/Mutter; Hyprland ignores it. Callers `editor/preferences.c:172`, `editor/open.c:319`. Harmless; remove or keep for Mint/Windows.
3. **Tab strip uses cell-quantized tabs and an in-band `•` modified marker and `…` truncation** (`tabs/tabs.c:~262, 268-290`, header comment tabs.h:116-118: "modified U+2022, ellipsis U+2026"). No close button glyph and no mouse close: fine, but unlike Omarchy tools (no tabs at all in most apps; Omarchy's idiom is one window per tile, groups via Super+G).
4. **Menu bar (File/Edit/View/Goto...)** `src/menu/menu.c:63-65`, `editor/menubar.c`: hidden by default, so no conflict unless enabled; Omarchy apps do not have menu bars.
5. **No outer padding.** Omarchy terminals pad 14 px (alacritty.toml:16-17, foot.ini `pad=14x14`, ghostty 14); sublimite relies on cell margins (`editor/theme.h:4-6`, `paint.c:150` strip) with no 14 px frame. Content sits flush against the 2 px border.
6. **Opacity.** Windows get 0.985/0.96 unless opted out (`windows.lua` last line); no blur is enabled, so a translucent editor shows the wallpaper through text at 4 %. sublimite's design does not compensate. Chromium's rule shows the pattern for opting out.
7. **Resize/placement.** sublimite requests an initial pixel size from DPI (`editor/open.c:150-174`, `main.c:257-281`); under tiling the compositor's configure overrides it. Wayland path does handle configure (`wayland.c:172-179`, zero = client chooses) and has no min/max size; XWayland path sets no WM_NORMAL_HINTS (`x11.c` grep empty), which Hyprland tolerates. Floating (`omarchy-hyprland-window-pop` default 1300x900, `bin/omarchy-hyprland-window-pop` lines 8-9) works with any size.
8. **Popups don't echo the shell.** Find/palette/prompt panels are grid rows (`paint.c`, `findui/`, `palettepanel.c`) with no border, 0 radius is fine, but no 2 px bordered card, different font (sublimite's own font via `EDIT_FONT_FAMILY`/`editor/fonts.c`, not the fontconfig `monospace` alias; I did not verify which default font it takes).
9. **Wayland gaps relevant to Hyprland** (no code evidence of anything breaking, only absence): no cursor-shape or `set_cursor` (see section 3), no fractional-scale/viewporter, no xdg-activation, no primary-selection paste (middle-click), no text-input (`QT_IM_MODULE=fcitx`, `INPUT_METHOD=fcitx` set by `default/environment.d/10-omarchy-fcitx.conf`; X11 gets XIM/fcitx through XWayland, Wayland needs text-input-v3 or fcitx's wl frontend), no wp_content_type. DnD on Wayland is a stub (`git log`: "Wayland/Win32 stubs").
10. **Single-instance handoff** (`main.c:132`): second `omarchy-launch-editor file` focuses nothing itself; Hyprland's `focus_on_activate = true` (looknfeel.lua `misc`) is honoured only if the Wayland path uses xdg-activation, which it does not. Opening a file in a running sublimite from the menu may not raise the window.

## 8. What an Omarchy-native sublimite must do (summary)

1. Run as a Wayland client by default (needs a decision on `plat_backend_select`, `dispatch.c:22`: prefer Wayland when `WAYLAND_DISPLAY` and XDG_SESSION_TYPE=wayland), with cursor-shape, fractional scale, xdg-activation, text-input added.
2. Draw no title bar or frame; own only the 2 px compositor border. Optionally bind xdg-decoration to be explicit (Hyprland answers SERVER_SIDE regardless).
3. Retarget the theme: stop matching the Mint title bar colour; read the Omarchy palette (other report); square corners; echo the shell popup style for panels.
4. Ship `sublimite.desktop` (exists), install to `~/.local/share/applications`, set `defaults/editor` to `sublimite`, add the Defaults>Editor row via the user menu extension, set `xdg-mime` for the text types.
5. Accept `--` and file args (done) and `--wait` for `$EDITOR`; consider a `--wait` wrapper so `git commit` works.
6. Keep all editor chords off SUPER (already true); no clash found.

## 9. Open questions for Tobias

- Q1. Default backend: should `DISPLAY` no longer win over `WAYLAND_DISPLAY` when both are present? (Today an Omarchy session always gets XWayland.)
- Q2. Opacity: should sublimite opt out of the global 0.985/0.96 (like browsers' `1.0 0.985`) or accept Omarchy's look?
- Q3. Should the dark chrome stay one surface (0x222226) or take Omarchy's palette (the theme report)? This also decides what the tab strip separates from.
- Q4. 14 px outer padding to match Omarchy's terminals, or keep flush?
- Q5. Do you want `git commit` ($EDITOR) in sublimite? Omarchy's launcher does not wait; a wrapper would be needed.
- Q6. Fractional scale priority: on this box scale is 1, but Omarchy's own steps include 1.25 and 1.6.
- Q7. Panels as a bordered 2 px card echoing the shell menu vs the current full-width grid rows.

## Not verified

No GUI launch, no `wayland-info`, no confirmation that Hyprland shows a cursor for a client that never calls `set_cursor`, no `make` build, and nothing about the integer scale Hyprland advertises on fractional outputs. Online manual pages were only summarised, not read in full.
