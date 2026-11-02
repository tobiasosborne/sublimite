# Omarchy theme system and the contract for a themed app

Research only, 2026-11-02. Nothing in the repo or in Omarchy was changed (the
one exception: this file). Source of truth is the install on this box
(Omarchy `4.0.0.alpha`, `/usr/share/omarchy/version`), then the web. Paths are
absolute; "L123" means line 123 of the file named. All line numbers were read
from the installed files today. No performance numbers are reported, so no
power/load stamp applies (box: no BAT0 present, `/proc/loadavg` 1.88 at the time).

Evidence tags: (M) read or measured on this box, (W) from the web, (D) derived
here, with the arithmetic shown.

Scope note: Hyprland window/desktop integration (rules, opacity, gaps as a
window-manager matter) is the other agent's half. Where theme tokens touch it
(rounding, border) it is recorded only as design-rule evidence (section 6).

---

## 1. How a theme is laid out on disk

### 1.1 The three places

| Place | Role | Evidence |
|---|---|---|
| `/usr/share/omarchy/themes/<slug>/` | stock themes, read-only, 22 of them (M: `ls /usr/share/omarchy/themes \| wc -l` = 22, `colors.toml` in all 22). The manual says "nineteen" (W: learn.omacom.io/2/the-omarchy-manual/52/themes), so it lags the package. | |
| `~/.config/omarchy/themes/<slug>/` | user themes and overlays. Same slug as a stock theme = overlay, user files win. Currently empty on this box. | `omarchy-theme-set` L268-276 |
| `~/.local/state/omarchy/current/` | the live result. `theme/` is the whole applied theme, `theme.name` holds the slug, `background` is a symlink. This is the path apps read. | `omarchy-theme-set` L12 |

Note that `~/.config/omarchy/current` does not exist on Omarchy 4. The brief
guessed that path; the real one is `~/.local/state/omarchy/current/theme`. It is
hard-coded as `$HOME/.local/state` (not `$XDG_STATE_HOME`) in the CLI
(`/usr/share/omarchy/bin/omarchy-theme-set` L12) and in the shell
(`/usr/share/omarchy/shell/Commons/Color.qml` L16-17:
`stateHome: home + "/.local/state"`). An app should do the same.

The current theme on this box (M): `cat ~/.local/state/omarchy/current/theme.name` = `tokyo-night`.

### 1.2 What a theme directory contains

Stock `tokyo-night` ships only (M, `ls /usr/share/omarchy/themes/tokyo-night`):
`backgrounds/ colors.toml icons.theme keyboard.rgb neovim.lua preview.png
preview-unlock.png shell.lock.toml unlock.png vscode.json`.

Stock `flexoki-light` ships `backgrounds colors.toml icons.theme neovim.lua
chromium.theme preview.png preview-unlock.png unlock.png vscode.json`.

So a theme author writes one palette file (`colors.toml`) plus a few
non-colour or "pointer" files. Everything per-app is generated.

### 1.3 `colors.toml` is the canonical palette; per-app files are generated

Yes, there is one canonical source. `omarchy-theme-set` copies stock theme, then
overlays the user theme, then runs `omarchy-theme-set-templates`
(`/usr/share/omarchy/bin/omarchy-theme-set` L268-284). The generator renders
every `*.tpl` into the staged theme dir, with sed substitution of `{{ key }}`
placeholders (`/usr/share/omarchy/bin/omarchy-theme-set-templates` L375,
L393-397):

```
template_files=("$USER_TEMPLATES_DIR"/*.tpl "$TEMPLATES_DIR"/*.tpl)
...
    # Don't overwrite configs already exists in the output directory (copied from theme specific folder)
    if [[ ! -f $output_path ]]; then
      sed -f "$sed_script" "$tpl" >"$output_path"
```

Consequences: (a) a file the theme ships by hand wins over the template; (b) user
templates are listed first and so a user `foo.tpl` produces `foo` before the
built-in `foo.tpl` is considered; (c) templates only run when the theme has a
`colors.toml` (L372 area, `if [[ -f $COLORS_FILE ]]`). Themes lacking one get
it synthesised from their `alacritty.toml` (`omarchy-theme-set` L278-281,
`omarchy-theme-colors-from-alacritty`). So `colors.toml` is always present in
`current/theme/` for a successfully applied theme.

Built-in templates (M: `ls /usr/share/omarchy/default/themed`): `alacritty.toml`
`btop.theme` `chromium.theme` `claude.json` `foot.ini` `ghostty.conf`
`gum_env.lua` `helix.toml` `hermes.yaml` `hyprland.lua`
`hyprland-preview-share-picker.css` `keyboard.rgb` `kitty.conf` `neovim.lua`
`obsidian.css` `pi.json` `shell.toml` `t3code.json` `vscode-theme.json`, each
with a `.tpl` suffix. The generated result for the current theme (M,
`ls ~/.local/state/omarchy/current/theme`) is those files plus the
theme-shipped ones (`colors.toml`, `icons.theme`, `keyboard.rgb`, `neovim.lua`,
`vscode.json`, `shell.lock.toml`, images). Waybar, walker and mako are not in
the list: the manual's "Making your own theme" page still names them (W:
learn.omacom.io/2/the-omarchy-manual/92/making-your-own-theme, "Ghostty,
Alacritty, Kitty, btop, Chromium, Hyprland, Hyprlock, Mako, SwayOSD, Walker,
Waybar"), but on Omarchy 4 the bar, launcher, notifications, OSD, lock and
polkit prompt are the Quickshell "Omarchy shell" and read `shell.toml` and
`shell.lock.toml` instead (`/usr/share/omarchy/shell/Commons/Color.qml` L1-10).
The web manual is stale on this point; the install wins.

### 1.4 What `~/.config/omarchy/themed/` is

User template overrides and additions. One file ships in it (M):
`~/.config/omarchy/themed/alacritty.toml.tpl.sample`, a documented sample whose
header says (L11-13): "User templates in ~/.config/omarchy/themed/ take priority
over the built-in templates in the Omarchy default/themed/ directory. This lets
you override any built-in template or create templates for applications not
included by default." Remove `.sample`, run a theme switch (or
`omarchy theme refresh`, `omarchy-theme-refresh`), and the output file appears in
`~/.local/state/omarchy/current/theme/`. This is the sanctioned way for a
third-party app to get its own generated theme file (section 7).

### 1.5 Installed-from-git themes are restricted

`omarchy theme install <url>` clones into `~/.config/omarchy/themes/<name>`.
`omarchy-theme-set` L30 then drops anything that runs code from a cloned theme:
`INSTALLED_THEME_DENIED=(alacritty.toml foot.ini ghostty.conf kitty.conf vscode.json)`
plus every `*.lua`; those are regenerated from `colors.toml` instead. Everything
else, including colour-only files, is kept
(`/home/tobias/.claude/skills/omarchy/theming.md`, section "What a Theme
Installed From a Repo May Not Contain"). A new app's theme file would be
colour-only and therefore allowed, but only if listed in the theme repo; the
simplest robust design is for the app to read `colors.toml` itself.

---

## 2. Palette vocabulary

### 2.1 File format

Flat TOML: `key = "value"` lines, no tables, `#` comments, one string value per
key. The parser is a bash `read` loop, not a TOML library
(`/usr/share/omarchy/bin/omarchy-theme-color` L84-113 area). It accepts keys
`[A-Za-z0-9_-]+` and values from the charset `[A-Za-z0-9#(),._+/% -]` (hex,
`rgb(...)`/`rgba(...)` lists, decimals, bare words, gradient angles like `45deg`).
Anything else is skipped with a stderr message, not an error. A parser for
sublimité needs only: comments, blank lines, `key = "string"`.

### 2.2 Keys

Union of keys across all 22 stock `colors.toml` (M: `cat */colors.toml | grep -o
'^[a-z_0-9]* *='` and counted):

| Key | Themes defining it (of 22) | Meaning / resolution |
|---|---|---|
| `mode` | 22 | `"dark"` or `"light"` |
| `accent` | 22 | selection of UI emphasis; terminal tab/active border |
| `selection` | 22 | selection background; alias `selection_background`; `selection_foreground` derived = `bright_foreground` |
| `muted` | 22 | ANSI colour8 (bright black); also dim text/borders. Alias for `color8`. |
| `background` `dark_background` `darker_background` `lighter_background` | 22 | neutral ramp. `dark_`/`darker_` derive by mixing background with black 25%/50% when missing (`omarchy-theme-color` L231-233 area) |
| `foreground` `dark_foreground` `light_foreground` `bright_foreground` | 22 | text ramp. `bright_foreground` doubles as cursor (no separate cursor key): `THEME_COLORS[cursor]="${THEME_COLORS[bright_foreground]}"` (`omarchy-theme-color` L225) |
| `red` `yellow` `green` `cyan` `blue` `magenta` | 22 | hue slots |
| `orange` `brown` | 19 | optional; fallback `orange`<-`yellow`, `brown`<-`mix orange black 50%` |
| `bright_red` `bright_yellow` `bright_green` `bright_cyan` `bright_blue` `bright_magenta` | 22 | bright hues; derived by mixing 20% white when missing |
| `hyprland_active_border`, `hyprland_inactive_border` | 3 and 2 | optional gradient strings for window borders (Hyprland) |
| `active_border_color`, `active_tab_background` | 3 each | optional overrides (lumon etc.) |

Derived by the resolver, available through `omarchy-theme-color --all` and as
template placeholders: `color0..color15` (ANSI aliases: `color0`=background,
`color1`=red ... `color7`=foreground, `color8`=muted, `color9..14`=bright hues,
`color15`=bright_foreground), `cursor`, `selection_background`,
`selection_foreground`, `purple`, `bright_purple`, `theme_type` (=mode). The
legacy short names `bg fg dark_bg darker_bg lighter_bg dark_fg light_fg
bright_fg` are accepted as input aliases. All from `omarchy-theme-color`
L96-150 area and L225-310.

Template modifiers (`omarchy-theme-set-templates` L200-230): `{{ key }}`,
`{{ key_strip }}` (no `#`), `{{ key_rgb }}` (`r,g,b` decimal), and
`{{ mix a b 30% }}` / `mix_strip` / `mix_rgb` (linear blend, channel
`int(a*(1-t)+b*t+0.5)`, L34-58). Gradient helpers `hypr_gradient`,
`gradient_start`, `shell_gradient`.

The web docs agree and add the explicit statement "there is no `urgent` palette
key (one defined in `colors.toml` is ignored)" and "Terminal and editor cursors
use `bright_foreground`" (W, via fetch of
raw.githubusercontent.com/basecamp/omarchy/master/docs/theming.md; the fetch
tool summarised the page, so treat quoted phrases as the summariser's rendering
of the page, not byte-exact).

### 2.3 Examples (M)

Dark, `/usr/share/omarchy/themes/tokyo-night/colors.toml` (the active theme):

```
mode = "dark"
accent = "#7aa2f7"      selection = "#292e42"      muted = "#414868"
background = "#1a1b26"  dark_background = "#13141c"
darker_background = "#0e0e14"  lighter_background = "#24283b"
foreground = "#a9b1d6"  dark_foreground = "#565f89"
light_foreground = "#b4bee6"   bright_foreground = "#c0caf5"
red = "#f7768e" yellow = "#e0af68" orange = "#eb927b" green = "#9ece6a"
cyan = "#449dab" blue = "#7aa2f7" magenta = "#ad8ee6" brown = "#75493d"
bright_red = "#ff7a93" ... bright_magenta = "#bb9af7"
```

Light, `/usr/share/omarchy/themes/flexoki-light/colors.toml`:

```
mode = "light"
accent = "#205EA6"      selection = "#CECDC3"      muted = "#B7B5AC"
background = "#FFFCF0"  dark_background = "#f2efe4"
darker_background = "#e5e2d8"  lighter_background = "#E6E4D9"
foreground = "#100F0F"  dark_foreground = "#878580"
light_foreground = "#403E3C"   bright_foreground = "#100F0F"
red = "#D14D41" yellow = "#D0A215" orange = "#d0772b" green = "#879A39"
cyan = "#3AA99F" blue = "#205EA6" magenta = "#CE5D97" brown = "#683b15"
```

Also checked: `catppuccin-latte/colors.toml` (light; `background = "#eff1f5"`,
`accent = "#1e66f5"`, `foreground = "#4c4f69"`).

Ramp direction (W, docs/theming.md summary): "Dark themes should read from
darkest to lightest; light themes should read from lightest to darkest" and
"The neutral ramp is centered on `background -> bright_foreground`". In
practice `lighter_background` is "lighter" only in dark themes: in flexoki-light
`lighter_background` (#E6E4D9) is darker than `background` (#FFFCF0) (M). Treat
`dark_/darker_/lighter_background` as ramp positions, not literal luminance.

### 2.4 Light or dark

Resolution order, in `omarchy-theme-color` header (L17-18): "Theme mode precedence:
`mode` key, legacy `theme_type` key, a light.mode file beside the colors.toml,
background luminance auto-detect, dark." The luminance fallback (L131):
`lum = R+G+B` of `background`; `lum > 382` = light (D: 382 is the file's own
constant, about 127.3 per channel). In the 22 stock themes every `colors.toml`
has an explicit `mode` (M: `grep -m1 '^mode'` over all 22 : 5 light, 17 dark;
light themes are catppuccin-latte, flexoki-light, lupine, rose-pine, white = 5;
dark = 17; none uses `light.mode`). The manual (W, "Making your own theme")
still documents the empty `light.mode` marker file; both work.

Other system-wide light/dark signals that Omarchy sets:

- GNOME: `omarchy-theme-set-gnome` L21/L24 runs
  `gsettings set org.gnome.desktop.interface color-scheme "prefer-light"` or
  `"prefer-dark"` (also `gtk-theme` Adwaita / Adwaita-dark and `icon-theme`).
  Live check (M): `gsettings get org.gnome.desktop.interface color-scheme` =
  `'prefer-dark'`. The XDG portal answers `org.freedesktop.appearance
  color-scheme` = 1 (M: busctl returned `v v u 1`; 1 = prefer dark per the
  portal spec, W-known, not re-fetched). This is the signal for toolkit-style
  light/dark but carries no palette.
- tmux `COLORFGBG` (`omarchy-theme-set-tmux` L47): terminal convention only.

Recommended for sublimité: read `colors.toml` `mode`; fall back to the
luminance rule above; do not depend on gsettings.

---

## 3. How a theme switch reaches apps

### 3.1 `omarchy-theme-set <name>` step by step
(`/usr/share/omarchy/bin/omarchy-theme-set`, line numbers from that file)

1. L12-17 paths. `CURRENT_THEME_PATH=$HOME/.local/state/omarchy/current/theme`,
   `NEXT_THEME_PATH=.../current/next-theme`.
2. Normalise name (lowercase, spaces to `-`), validate directory exists in
   stock or user themes.
3. L262 `flock` on `$XDG_RUNTIME_DIR/omarchy-theme-set.lock` serialises switches.
4. L266-276 stage: `rm -rf next-theme; mkdir`; copy stock theme; overlay user
   theme (or the restricted copy for git-cloned themes).
5. L278-281 synthesise `colors.toml` from `alacritty.toml` if missing.
6. L284 `omarchy-theme-set-templates`: render all `*.tpl` into next-theme.
7. L292-293 **swap**: `rm -rf "$CURRENT_THEME_PATH"; mv "$NEXT_THEME_PATH" "$CURRENT_THEME_PATH"`.
   So `current/theme` is deleted and a new directory is renamed into place.
   Files inside are never rewritten in place.
8. L296 `echo "$THEME_NAME" >"$HOME/.local/state/omarchy/current/theme.name"`
   (after the swap).
9. L300-316 push `colors.toml` and `shell.toml` (base64) to the running Quickshell
   shell over `omarchy-shell` IPC (`shell applyTheme` / `background themeTransition`)
   and release the lock. The shell does not watch the files for this; it reads
   at startup and takes the IPC payload at runtime
   (`/usr/share/omarchy/shell/Commons/Color.qml` L222 comment: "Startup load only.
   Runtime theme switches push the payload explicitly through shell IPC.").
10. L318-338 `run_parallel` of the retint commands:
    `omarchy-restart-terminal omarchy-restart-hyprctl omarchy-restart-btop
    omarchy-restart-opencode omarchy-restart-helix omarchy-theme-set-foot
    omarchy-theme-set-tmux omarchy-theme-set-gnome omarchy-theme-set-pi
    omarchy-theme-set-claude omarchy-theme-set-hermes omarchy-theme-set-t3code
    omarchy-theme-set-browser omarchy-theme-set-vscode omarchy-theme-set-obsidian
    omarchy-theme-set-keyboard`.
11. L341 `omarchy-hook theme-set "$THEME_NAME"`: user hooks run last.

### 3.2 Per-app reload mechanisms (all M, from the scripts named)

| App | Mechanism | Script |
|---|---|---|
| alacritty | config does `general.import = ["~/.local/state/omarchy/current/theme/alacritty.toml"]`; reload by `touch ~/.config/alacritty/alacritty.toml` (alacritty watches its config) | `config/alacritty/alacritty.toml` L1; `omarchy-restart-terminal` |
| kitty | `include .../current/theme/kitty.conf`; `killall -SIGUSR1 kitty` | `config/kitty/kitty.conf` L2; `omarchy-restart-terminal` |
| ghostty | `config-file = ?"~/.local/state/omarchy/current/theme/ghostty.conf"`; `killall -SIGUSR2 ghostty` | `config/ghostty/config` L2; `omarchy-restart-terminal` |
| foot | `include=` same pattern; running foot gets OSC 4/10/11/12/17/19 escape sequences written to each pty (`omarchy-theme-osc`) | `omarchy-theme-set-foot` |
| btop | `~/.config/btop/themes/current.theme` is a symlink into `current/theme/btop.theme`; `pkill -SIGUSR2 btop` | `omarchy-restart-btop`; M: `ls -la ~/.config/btop/themes` |
| helix | `~/.config/helix/themes/omarchy.toml` symlink to `current/theme/helix.toml`; `pkill -USR1 helix` | `omarchy-install-editor-helix`; `omarchy-restart-helix` |
| opencode | `killall -SIGUSR2 opencode` | `omarchy-restart-opencode` |
| tmux | `tmux set-environment`, `window-style`, OSC to every pane tty, `kill -WINCH` | `omarchy-theme-set-tmux` |
| Hyprland | `current/theme/hyprland.lua` required by config; `hyprctl reload` | `omarchy-restart-hyprctl` |
| neovim | `current/theme/neovim.lua` (LazyVim colourscheme spec); a config plugin re-applies it on `User LazyReload` | `~/.config/nvim/lua/plugins/omarchy-theme-hotreload.lua` (M) |
| VS Code / VSCodium / Cursor | writes `workbench.colorTheme` in `settings.json` (sed in place) and installs generated `vscode-theme.json` as a local extension; VS Code watches settings | `omarchy-theme-set-vscode` |
| Obsidian | copies `obsidian.css` into every vault's `.obsidian/themes/Omarchy/theme.css` | `omarchy-theme-set-obsidian` |
| Claude Code, Pi, T3 Code | atomic write (`mktemp` + `mv`) of the generated JSON into the app's own theme dir; the app **watches** it. Script comment: "Claude Code watches ~/.claude/themes and hot-reloads the file on change, so running sessions retint without a restart." / "T3 Code watches this file and retints every connected client on change, so the write has to be atomic" | `omarchy-theme-set-claude`, `-t3code`, `-pi` |
| Chromium family | writes a browser policy colour then `--refresh-platform-policy` | `omarchy-theme-set-browser` |
| GNOME/GTK | gsettings (see 2.4) | `omarchy-theme-set-gnome` |
| Omarchy shell | IPC payload | step 9 |

### 3.3 Hooks for third parties

`omarchy-hook <name> [args]` (`/usr/share/omarchy/bin/omarchy-hook`): runs
`~/.config/omarchy/hooks/<name>` if a file, then every non-`.sample` file in
`~/.config/omarchy/hooks/<name>.d/` with `bash "$hook" "$@"` (executable bit not
needed; failures print "Hook failed" and carry on). Directories exist on this
box (M): `battery-low.d font-set.d post-boot.d post-update.d
pre-refresh-pacman.d theme-set.d`. The `theme-set` hook gets the slug as `$1`
(sample: "called with the snake-cased name of the theme that has just been set").
Install with `omarchy hook install theme-set <script>`
(`omarchy-hook-install`: copy + `chmod 755`). The `font-set` hook gets the font
name as `$1`. Hooks fire after all retint commands, so `current/theme` is
complete when they run.

### 3.4 What a new app has to do to switch live

Three workable options, in order of how little the user must configure:

A. **Watch the directory `~/.local/state/omarchy/current/`** with inotify and
   re-read `theme/colors.toml` on the completion marker. No hook, no install
   step. Because `theme` is removed and renamed in, watching the file or the
   `theme/` directory itself breaks (the inode dies); the watch must be on the
   stable parent. I reproduced the exact operation order from L292-296 in a
   scratch directory under `/tmp` and ran `inotifywait -m` on the parent (M,
   today; not on the live theme, no theme was switched):

   ```
   CREATE,ISDIR next-theme
   DELETE,ISDIR theme
   MOVED_FROM,ISDIR next-theme
   MOVED_TO,ISDIR theme
   MODIFY theme.name
   MODIFY theme.name
   CLOSE_WRITE,CLOSE theme.name
   ```

   `CLOSE_WRITE theme.name` is the last event and fires after the new
   `theme/` is in place, so it is the best "theme is ready" trigger; it also
   fires when the same theme is re-applied (`omarchy-theme-refresh` re-runs
   `omarchy-theme-set` with the current name). Filter by name, ignore
   `next-theme`, debounce, and tolerate a window where `theme/` is briefly
   absent. This is the same shape as how Claude Code and T3 Code do it, except
   they watch their own copy.

B. **Install a `theme-set.d` hook** that signals the app (e.g. `pkill -USR1
   sublimite`, or touches a file in sublimité's config dir that its existing
   config watcher already sees). Matches how Omarchy treats alacritty/kitty/
   ghostty/btop/helix (signal). Needs an install step the user must run.

C. **Ship a user template** `~/.config/omarchy/themed/sublimite.toml.tpl`
   (section 1.4) that renders a sublimité-native file into `current/theme/`,
   plus A or B for the reload. Only worth it if sublimité wants Omarchy to do
   the colour arithmetic (`mix`, `_rgb`) rather than doing it in C.
   User templates only render on the next switch or an `omarchy theme refresh`.

Both A and B are compatible; A is zero-config.

---

## 4. Fonts

- Command: `omarchy font set "<family>"` (`/usr/share/omarchy/bin/omarchy-font-set`).
  It requires the family to appear in `fc-list`, then edits terminal configs by
  `sed` (alacritty `family = ...`, kitty `font_family`, ghostty `font-family`,
  foot `font=`) and signals them.
- **Canonical store: fontconfig.** The script's own comment (L53-55): "fontconfig
  is the canonical source of truth — the omarchy shell, Qt apps, and anything
  resolving "monospace" all read from here." It writes
  `~/.config/fontconfig/fonts.conf` (L57-72) with a `monospace` match that
  `prepend_first`s the chosen family, `binding="strong"`, then restarts the shell
  and runs the `font-set` hook.
- Default (M): `/usr/share/fontconfig/conf.avail/50-omarchy.conf` (symlinked in
  `/etc/fonts/conf.d`) assigns monospace -> `JetBrainsMono Nerd Font`
  (`default/fontconfig/conf.avail/50-omarchy.conf` L27), sans-serif -> Liberation
  Sans, serif -> Liberation Serif.
- Read side: `omarchy font current` is `fc-match monospace -f '%{family}\n'`
  (`omarchy-font-current`). The shell does `fc-match -f "%{family[0]}" monospace`
  and watches `~/.config/fontconfig/fonts.conf` for change
  (`shell/Commons/Style.qml` L480-487).
  Live (M): `fc-match monospace` = `JetBrainsMonoNerdFont-Regular.ttf:
  "JetBrainsMono Nerd Font" "Regular"`; `~/.config/fontconfig/fonts.conf` does
  not exist yet on this box (default still in force).
- gsettings is **not** updated: `org.gnome.desktop.interface monospace-font-name`
  is `'Adwaita Mono 11'` on this box (M), so GTK-side monospace and Omarchy's
  choice disagree. Do not read gsettings for the font.
- Size is not a system setting. Terminals hard-code 9 pt:
  `/usr/share/omarchy/config/alacritty/alacritty.toml` L13 `size = 9`;
  `config/ghostty/config` L7 `font-size = 9`; foot via `font=...:size=9` in
  `omarchy-font-set`. The shell's size is `[font] base-size = 12` px
  (`current/theme/shell.toml`, `[font]` block) and a machine-level override at
  `~/.config/omarchy/shell.toml` written by `omarchy display text size`
  (`Color.qml` comment above `userShellFile`). Terminals' 9 pt is the nearest
  precedent for a text surface.

Fit for sublimité: its font setting is a fontconfig pattern already
(`docs/config.md`: "`family` is a Fontconfig pattern such as `Iosevka
Fixed:semibold`"). Resolving the pattern `monospace` through fontconfig gives
Omarchy's choice with no new code beyond the default. Caveat from
`docs/config.md`: sublimité rejects proportional advances and wants no
ligatures; JetBrainsMono Nerd Font (Omarchy default) is monospace but has
ligature substitutions that sublimité's grid path ignores, which is fine.
Watch `~/.config/fontconfig/fonts.conf` (or hook `font-set.d`) for live change.

---

## 5. Precedent for editors

| Editor | How Omarchy wires it | Evidence |
|---|---|---|
| **Neovim** (shipped default editor) | Theme ships `neovim.lua` naming an existing colourscheme (tokyo-night: `folke/tokyonight.nvim`, colorscheme `tokyonight-night`); themes without one get the generated `neovim.lua.tpl`, which fills `bjarneo/aether.nvim` with the full palette (`bg dark_bg darker_bg lighter_bg fg dark_fg light_fg bright_fg muted red yellow orange green cyan blue magenta brown bright_* accent cursor selection selection_foreground selection_background`). Live switch = lazy.nvim spec reload hook in the user config. Transparency is layered by a separate file making `Normal`, `NormalFloat`, etc. backgrounds nil. | `default/themed/neovim.lua.tpl`; `themes/tokyo-night/neovim.lua`; `~/.config/nvim/lua/plugins/omarchy-theme-hotreload.lua`; `~/.config/nvim/plugin/after/transparency.lua` |
| **VS Code / VSCodium / Cursor** | Two routes. (1) Theme ships `vscode.json` = `{"name": "Tokyo Night", "extension": "enkia.tokyo-night"}` and Omarchy installs that extension and sets `workbench.colorTheme`. (2) Otherwise the generated `vscode-theme.json` (from a 59 931-byte template, M: `ls -l`) is installed as a local extension `local.omarchy-theme`, symlinked to the live file, `uiTheme` `vs`/`vs-dark` from `type`. Per-editor opt-outs via toggles `skip-vscode-theme-changes` etc. | `omarchy-theme-set-vscode`; `themes/tokyo-night/vscode.json` |
| **Zed** | `omarchy install editor zed` installs the AUR/extra packages `zed omazed` and runs `omazed setup`; theme tracking lives in that external tool, not in Omarchy. `omazed` is not installed here (M: `pacman -Qi omazed` not found), so its mechanism is unverified. | `omarchy-install-editor-zed` |
| **Helix** | Symlink `~/.config/helix/themes/omarchy.toml` -> `current/theme/helix.toml`; `theme = "omarchy"`; `SIGUSR1` on switch. Colours reference `color0..15` and `foreground`, i.e. the ANSI aliases. | `omarchy-install-editor-helix`; `default/themed/helix.toml.tpl` |
| **Sublime Text** | No theme integration. The only mentions are a default-editor menu entry (`default/omarchy/omarchy-menu.jsonc` L164, L229) and a window rule that floats its file dialogs (`default/hypr/apps/system.lua` L17-20). | M, `grep -rln -i sublime /usr/share/omarchy` |
| **Emacs, Obsidian, T3 Code** | Emacs has an installer only (`omarchy-install-editor-emacs`, not read). Obsidian and T3 Code get generated CSS/JSON copied or watched, see 3.2. | |

**Closest precedents for sublimité**, in this order:
1. Claude Code / T3 Code / Pi: a native app with its own theme format that
   Omarchy renders from `colors.toml` and drops, atomically, where the app
   watches. Sublimité could be the same with an inotify on the Omarchy side
   (option A, 3.4) instead of Omarchy writing into our config dir.
2. Helix: a TUI editor that consumes the generated file through a symlink and a
   signal.
3. VS Code `vscode-theme.json.tpl`: the best existing mapping of Omarchy palette
   to editor-surface tokens (used for the table in section 8).

---

## 6. Written design rules and conventions

Omarchy has no prose "style guide". The rules below are what is written down or
encoded in defaults. None says "apps must".

Written:
- Palette contract (W, docs/theming.md summary): neutral ramp
  `background -> bright_foreground`; "Dark themes should read from darkest to
  lightest; light themes should read from lightest to darkest"; "Prefer
  generated files when templates can express the theme"; cursor is
  `bright_foreground`; no `urgent` key. Preview with `omarchy dev theme-preview
  [theme]` (`/usr/share/omarchy/bin/omarchy-dev-theme-preview`).
- Shell style singleton comment (`/usr/share/omarchy/shell/Commons/Style.qml`
  L12-18): "`cornerRadius` mirrors Hyprland's `decoration:rounding`. `gapsOut` is
  half of Hyprland's `general:gaps_out`". So UI chrome follows the window
  manager's rounding; it is read live via `hyprctl -j getoption
  decoration:rounding` (Style.qml L443-445).
- Shell state vocabulary (`current/theme/shell.toml`, comments): states
  `normal`, `hover-cursor`, `selected`, `focus`; fills are the text colour at a
  low alpha rather than a new colour. Defaults in the generated file:
  `normal-fill-alpha = 0.04`, `hover-cursor-fill-alpha = 0.08`,
  `selected-fill-alpha = 0.18`, `pressed-fill-alpha = 0.22`,
  `selection-fill-alpha = 0.35`, `normal-border-width = 1`,
  `normal-border-alpha = 0.4` (all M, `shell.toml` `[controls]`).
- Accent usage in the shell (`shell.toml`, M): popup/notification borders are
  the Hyprland active border (`border = "hyprland.active-border"`); menu/launcher
  card borders use `active-border-foreground`; selected-row text is `accent`
  (`selected-text = "#7aa2f7"` in tokyo-night) on a foreground tint at alpha 0.08;
  alerts use `red` (`bar.active`). `accent` is used for focus and the single
  selected item, never for body text.
- Bar: `size-horizontal = 26` px at `base-size = 12` px, `scale-with-font = true`
  (`shell.toml` `[bar]`); spacing scale and tokens are commented defaults
  (`xxs = 2 ... huge = 18`, `control-height = 28`, `panel-padding = 18`,
  `popup-padding = 14`, `shell.toml` `[spacing]`); type scale `caption 10`,
  `body 12`, `title 14`, `heading 16` (`[font]`).

Encoded in defaults (Hyprland side, recorded only because theme/design depends
on it; owned by the other agent):
- `border_size = 2`, `gaps_in = 5`, `gaps_out = 10`, `rounding = 0`, blur and
  shadow disabled: `/usr/share/omarchy/default/hypr/looknfeel.lua` L8-10, L23,
  L25-31. Live (M): `hyprctl getoption decoration:rounding` = 0, `border_size` = 2,
  `gaps_in` = 5.
- Default window opacity `"0.985 0.96"` (active, inactive) applied by tag to
  every window unless an app rule opts out:
  `default/hypr/windows.lua` L5-6, L25; browsers opt to `"1.0 0.985"`
  (`apps/browser.lua` L4). Terminals are tagged `terminal` (`apps/terminals.lua`)
  but get the default opacity. Neovim's `transparency.lua` removes backgrounds
  so the terminal shows through.
- Rounding is a **theme** choice only in `solitude`: `themes/solitude/hyprland.lua`
  L18-19 `rounding = 6, rounding_power = 3`. Popped windows are `rounding = 8`
  (`default/hypr/apps/system.lua` L54). So the Omarchy look is square corners
  with 2 px borders unless a theme says otherwise.
- Terminal padding 14 px both axes (alacritty `window.padding` L16-17, ghostty
  L11-12 in the stock configs); kitty padding is a commented example.
  Terminal font 9 pt (alacritty L13, ghostty L7). These are the nearest
  "how much breathing room does a text surface get" numbers; they are Omarchy
  defaults for terminals, not a rule for editors.

Contrast reality check (D). `muted` is also ANSI colour8 and Omarchy's own VS Code
template uses it for comments and line numbers (`vscode-theme.json.tpl`: comment
`foreground` = `{{ muted }}`, `editorLineNumber.foreground` = `{{ muted }}`).
WCAG 2.x contrast of `muted` against `background`, computed today from the 22
stock `colors.toml` files with the standard relative-luminance formula:
min 1.48:1 (rose-pine, matte-black), tokyo-night 1.91:1, catppuccin-latte 1.91:1,
flexoki-light 2.00:1, max 4.90:1 (ethereal, vantablack 4.89:1). `dark_foreground`
ranges 1.72:1 (everforest) to 7.78:1 (osaka-jade). Sublimité's own palette
(`/home/tobias/sublimite/src/config/theme.c` L12-44) gates comments at 5.73:1
dark / 5.37:1 light and gutter numbers at 3.13:1 / 2.59:1, so a literal
`muted` mapping would fall below sublimité's own ladder in most Omarchy themes.
See open question 2.

Monochrome themes: `white` has `red`..`magenta` all in grey #1a1a1a-#4a4a4a, and
other stock themes collapse hue similarly (M: `themes/white/colors.toml`), so
hue-only syntax distinctions vanish; keep syntax tokens distinguishable by
something else (italic comments, as VS Code's template does) if wanted.

---

## 7. Recommended integration path (no design beyond the contract)

1. **Read `~/.local/state/omarchy/current/theme/colors.toml` directly.** It is
   always present, flat, and stable; no Omarchy-side change or user install
   step. Reuse sublimité's existing `config_toml_parse` (it handles bare keys,
   quoted strings and comments; unknown keys reach the visitor, so
   `hyprland_active_border` etc. are harmless).
2. Resolve in sublimité the same fallbacks the shell resolver applies (section
   2.2): `orange`<-`yellow`, `selection_foreground`<-`bright_foreground`,
   `cursor`<-`bright_foreground`, `dark_/darker_background` by mixing with black
   25%/50%, `mode` per 2.4.
3. **Live switch via inotify on `~/.local/state/omarchy/current/`** (3.4 A),
   trigger on `CLOSE_WRITE theme.name`, debounce, re-read on a worker, swap the
   palette at a frame boundary (the same pattern `docs/config.md` already uses
   for config and font reload).
4. Keep `~/.config/sublimite/config.toml` as the override: `theme = "light" |
   "dark"` stays valid and would mean "use sublimité's built-in palette"; add a
   value (name TBD, e.g. `"omarchy"`) for following the system. Do not make
   Omarchy a hard dependency: if `current/theme/colors.toml` is missing
   (non-Omarchy box), fall back to the built-in palettes.
5. **Font:** default family pattern `monospace`, resolved by fontconfig; watch
   `~/.config/fontconfig/fonts.conf` for changes (the shell does the same).
6. Optional later: ship `~/.config/omarchy/themed/sublimite.toml.tpl` plus a
   `theme-set.d` hook if Tobias wants Omarchy-side rendering or an upstream-style
   integration; not needed for the contract.

---

## 8. Mapping table: Omarchy palette -> sublimité tokens

Sublimité's tokens are the 30 names in `/home/tobias/sublimite/src/config/theme.c`
L12-44 (accessed via `config_theme_color`, cached into `e->colors.*` by
`editor_theme_cache` in `/home/tobias/sublimite/src/editor/preferences.c`
L130-143). The task said `src/render/theme.h`; that file does not exist. The
tokens live in `src/config/theme.c`, `config.h` (L43 `config_theme` enum
`CONFIG_DARK`/`CONFIG_LIGHT`) and `src/editor/theme.h`.

"Precedent" = what Omarchy's own VS Code template does for the nearest
equivalent (`/usr/share/omarchy/default/themed/vscode-theme.json.tpl`).

| Sublimité token | Omarchy source | Precedent / note |
|---|---|---|
| `editor.background` | `background` | VS `editor.background` L206 |
| `editor.foreground` | `foreground` | VS L207 |
| `gutter.background` | `background` | same as editor, as sublimité does now |
| `gutter.foreground` | `muted` literal, or `mix(foreground, background, N%)` | VS `editorLineNumber.foreground` = `muted` L208; contrast issue (section 6) |
| `caret.background` | `cursor` (= `bright_foreground`) | Omarchy terminals/editors use `bright_foreground` for cursor (alacritty/kitty templates, VS `editorCursor.foreground` L212). Sublimité today uses a blue accent caret; `accent` is the alternative. Open question 3. |
| `caret.foreground` | `background` | alacritty `cursor.text = background` |
| `selection.background` | `selection_background` (= `selection`) | VS L213 (adds alpha 0x60) |
| `selection.foreground` | `selection_foreground` (= `bright_foreground`) | VS L214 |
| `chrome.background` | `background` (or `dark_background`) | VS sidebar/tab bar use `background`; zed template uses `dark_background` for sidebar. Sublimité's dark theme has chrome = editor. |
| `chrome.foreground` | `dark_foreground` or `mix(foreground, background, N%)` | VS `tab.inactiveForeground` = `muted` L192 |
| `tab.active.background` | `background` | VS `tab.activeBackground` L183 |
| `tab.active.foreground` | `foreground` | VS L185 |
| `tab.hover.foreground` | `light_foreground` or `foreground` | VS `tab.hoverForeground` = `foreground` L197 |
| `minimap.foreground` | `muted` | VS minimap slider uses `muted` at low alpha |
| `warning.foreground` | `yellow` | VS `inputValidation.warning*` = `yellow` |
| `error.foreground` | `red` | VS `errorForeground` = `red` |
| `error.background` | `mix(background, red, 15%)` | Omarchy claude.json: `diffRemoved = mix background red 15%` |
| `bracket.background` | `mix(background, accent, 20%)` or `selection` | VS word-highlight = accent at alpha 0x20 |
| `panel.background` | `lighter_background` (dark) / `dark_background` (light), i.e. the ramp step next to `background` that reads as a raised surface | zed template `sidebar` = `dark_background`; ramp direction caveat in 2.3 |
| `hover.background` | `mix(background, foreground, 6%)` | zed `sidebarRowHover = mix dark_background foreground 6%` |
| `syntax.comment` | `muted` literal, or `dark_foreground`, or a mix | VS comment = `muted` italic; contrast issue |
| `syntax.keyword` | `bright_magenta` | VS semantic `keyword` |
| `syntax.string` | `green` | VS `string` |
| `syntax.number` | `orange` | VS `number` |
| `syntax.constant` | `bright_yellow` | VS `variable.readonly` / `variable.other.constant` |
| `syntax.function` | `blue` | VS `function` |
| `syntax.type` | `yellow` | VS `type`, `class` |
| `syntax.variable` | `foreground` | VS `variable` |
| `syntax.operator` | `bright_blue` | VS `operator` |
| `syntax.punctuation` | `light_foreground` or `foreground` | VS: punctuation not separately coloured in the keys read; helix uses `color8` (`muted`) |
| `syntax.attribute` | `cyan` (VS `property`) or `yellow` (helix `attribute` = `color3`) | precedents disagree |
| (new) accent for focus / find highlight | `accent`; find match `yellow` | VS `focusBorder` = accent, `editor.findMatchBackground` = yellow at alpha |

Mixes use the resolver's rounding: `int(a*(1-t) + b*t + 0.5)` per channel
(`omarchy-theme-set-templates` L53-55 equivalent in its awk). Note: the 30
sublimité tokens are an exact fit for what Omarchy can supply, with no Omarchy
key left unused except `dark_background`/`darker_background`, `brown`,
`bright_red/green/cyan`, `light_foreground` and `color0..15`.

---

## 9. Open questions for Tobias

1. **Follow or keep the ladder?** Omarchy's `muted`/`dark_foreground` are
   frequently below 3:1 against `background` (section 6), so literal mapping makes
   comments and line numbers harder to read than sublimité's current palette
   (comments 5.7:1/5.4:1, gutter 3.1:1/2.6:1). Choice: map literally (looks like
   Omarchy's other apps) or derive comment/gutter by mixing `foreground` toward
   `background` to hold the ladder (rubric C2 in `theme.c`).
2. **Caret colour:** `bright_foreground` (Omarchy's rule: no separate cursor key)
   or `accent` (sublimité today)?
3. **Light/dark wording in config:** keep `theme = "dark" | "light"` and add a
   third value (e.g. `"omarchy"`) meaning "follow the system", or make follow
   the default whenever `current/theme/colors.toml` exists?
4. **Monochrome themes** (`white`, `vantablack`, `matte-black` have near-grey hue
   keys): accept flat syntax colouring there, or add non-hue cues (italic)?
5. **Font default:** resolve `monospace` via fontconfig by default (inherits
   Omarchy's JetBrainsMono Nerd Font, 9 pt in terminals) while the embedded
   DejaVu stays as fallback? The size default is separate (terminals 9 pt,
   sublimité's own default set in `src/editor/runtime.h`).
6. **Upstream?** The contract above needs nothing from Omarchy. Do we want an
   Omarchy-side `sublimite.toml.tpl` and a `theme-set.d` hook (user-installed,
   section 3.4 C), or no Omarchy files at all?
7. **Unverified:** `omazed` (Zed's Omarchy bridge) is not installed here, so
   how Zed tracks the theme was not read; and no live `omarchy theme set` was
   run (it would change Tobias's desktop), so the inotify sequence in 3.4 is
   a faithful replay of the script's file operations, not a capture of a real
   switch. Worth one real switch to confirm before coding.

---

## Sources

Local: `/usr/share/omarchy/bin/omarchy-theme-set`, `omarchy-theme-set-templates`,
`omarchy-theme-color`, `omarchy-font-set`, `omarchy-font-current`,
`omarchy-hook`, `omarchy-hook-install`, `omarchy-restart-*`,
`omarchy-theme-set-{vscode,claude,t3code,pi,foot,tmux,gnome,browser,obsidian}`,
`omarchy-install-editor-{zed,helix}`; `/usr/share/omarchy/default/themed/*.tpl`;
`/usr/share/omarchy/themes/*/colors.toml`; `/usr/share/omarchy/shell/Commons/{Color,Style}.qml`;
`/usr/share/omarchy/default/hypr/{looknfeel,windows}.lua`;
`/usr/share/omarchy/config/{alacritty,ghostty,kitty}`;
`~/.config/omarchy/{themed,hooks}`, `~/.config/nvim/lua/plugins/omarchy-theme-hotreload.lua`,
`~/.local/state/omarchy/current/`; `~/.claude/skills/omarchy/{theming,hooks}.md`.

Web:
- https://learn.omacom.io/2/the-omarchy-manual/52/themes
- https://learn.omacom.io/2/the-omarchy-manual/92/making-your-own-theme
- https://raw.githubusercontent.com/basecamp/omarchy/master/docs/theming.md
  (the fetch tool returned condensed summaries; the local install was used to
  verify every claim that matters, and where the web is stale it is said so)
