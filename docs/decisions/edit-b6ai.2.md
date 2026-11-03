# edit-b6ai.2: follow the Omarchy palette live

Research: [theme-system.md](../design/omarchy-2026-11-02/theme-system.md)
sections 2, 3.4 A, 7 and 8. Scope set by Tobias in the bead brief.

## Config value

`theme = "system"` is the default and follows the system palette. `"dark"` and
`"light"` pin the built-in palettes. In `config_settings` this is
`theme_pinned` (false by default) plus the existing `theme`. With `"system"`,
`theme` keeps its zero default `CONFIG_DARK`, which is the fallback mode. So
`config_defaults` is still a plain memset, and every existing test that
expects `CONFIG_DARK` after defaults still holds. `"omarchy"` was not used as
the value name. The value describes what the editor does, and a future
non-Omarchy source (the portal colour scheme, for example) can sit behind the
same value.

## Where the palette comes from

`config_palette_path`: `$XDG_STATE_HOME/omarchy/current/theme/colors.toml`,
with `$HOME/.local/state/...` when the XDG value is unset or relative. This
follows the same rule as `config_path`. On Windows it returns
`CONFIG_ERR_ARG`, so there is no system palette.

Only native startup (`load_preferences`) uses the lookup. Every other
`editor_config` gets a system palette only through the explicit
`palette_path` field. So the in-process tests stay hermetic on this Omarchy
box, where the real file exists.

## Mapping (report section 8, resolver fallbacks of section 2.2)

`config_palette_parse` reuses `config_toml_parse`. It reads `"#rrggbb"`
values only. All 22 stock themes use that form for every palette key (M:
`grep` over `/usr/share/omarchy/themes/*/colors.toml`; the only other forms
are `rgba(...)` gradients in `hyprland_*_border`). A palette key in any other
spelling counts as absent. Unknown keys and table keys are ignored. A TOML
syntax error, or a missing `background` or `foreground`, fails the parse.
Every other missing source keeps that token's built-in value for the mode.

The aliases follow the order of `omarchy-theme-color resolve_theme_colors`:

- short names `bg fg dark_bg lighter_bg dark_fg light_fg bright_fg`
- `color0` and `color7` for background and foreground
- `color1-6` and `color11-13` for the hues
- `purple` and `bright_purple`
- `light_foreground <- foreground`
- `bright_foreground <- color15 <- foreground`
- `lighter_background <- background`
- `dark_foreground <- color8 <- foreground`
- `muted <- color8 <- dark_foreground`
- `selection <- selection_background <- color8 <- background`
- `selection_foreground <- bright_foreground`
- `orange <- yellow`
- `dark_background = mix(background, black, 25%)`
- `bright_{yellow,blue,magenta} = mix(hue, white, 20%)`

Mixes use the resolver's arithmetic, `int(a*(1-t) + b*t + 0.5)` in doubles
like its awk, so ties round the same way.

Mode is resolved in this order: the `mode` key, the `theme_type` key, a
`light.mode` file beside `colors.toml`, then background R+G+B > 382 (P: the
resolver's own constant). If none of these applies, the mode is dark.

| Token | Source |
|---|---|
| editor/gutter/chrome/tab.active background, caret.foreground | `background` |
| editor.foreground, tab.active.foreground, tab.hover.foreground, syntax.variable | `foreground` |
| caret.background | `accent` (Tobias: caret on accent) |
| selection.background / .foreground | `selection` / `selection_foreground` |
| warning.foreground, syntax.type | `yellow` |
| error.foreground | `red`; error.background = mix(background, red, 15%) |
| bracket.background (find match) | mix(background, accent, 20%) |
| panel.background | `lighter_background` (dark) / `dark_background` (light) |
| hover.background | mix(background, foreground, 6%) |
| syntax.keyword / string / number / constant | `bright_magenta` / `green` / `orange` / `bright_yellow` |
| syntax.function / operator / punctuation / attribute | `blue` / `bright_blue` / `light_foreground` / `cyan` |
| syntax.comment, gutter.foreground, minimap.foreground | `muted`, then the ladder rule |
| chrome.foreground | `dark_foreground`, then the ladder rule |

`tab.hover.foreground` is `foreground` and not `light_foreground`. The
reason: `light_foreground` is brighter than `foreground` in dark themes but
lighter (fainter) in flexoki-light (M: `#403E3C` against `#100F0F`). That
would make hover lose contrast in one mode and gain it in the other. VS
Code's Omarchy template also uses `foreground` (report section 8).

## Ladder rule

Omarchy's `muted` and `dark_foreground` are too faint for comments, line
numbers and inactive tabs in many themes. The rule: mix the source toward
`foreground` in whole percent steps, `t = 0..100`, and take the first `t` at
which the WCAG 2.2 contrast against the token's own surface reaches the
built-in theme's ratio for the same token in the same mode. If no step
reaches it, the token is `foreground`. A token that is already readable
enough stays literal (`t = 0`).

The targets are computed at runtime from `theme.c`. They are not hard-coded,
so a retuned built-in palette moves them too. Their values (P: WCAG 2.2 from
the `theme.c` RGB; they agree with the comments in that file):

| Token (surface) | built-in dark | built-in light |
|---|---|---|
| syntax.comment (editor.background) | 5.73:1 | 5.37:1 |
| gutter.foreground (gutter.background) | 3.13:1 | 2.59:1 |
| minimap.foreground (editor.background) | 1.64:1 | 1.53:1 |
| chrome.foreground (chrome.background) | 5.52:1 | 5.45:1 |

Results (M: `tests/config_palette_test` and the same function run over the
stock files, 2026-11-03):

| Theme | muted literal | comment | gutter | minimap | chrome (from dark_foreground) |
|---|---|---|---|---|---|
| tokyo-night (dark) | #414868 1.91:1 | 73% -> #8d95b8 5.80:1 | 31% -> #61698a 3.17:1 | 0% -> #414868 1.91:1 | #565f89 2.76:1, 61% -> #8991b8 5.54:1 |
| flexoki-light (light) | #B7B5AC 2.00:1 | 47% -> #696762 5.50:1 | 14% -> #a09e96 2.61:1 | 0% -> #b7b5ac 2.00:1 | #878580 3.59:1, 26% -> #686663 5.57:1 |
| catppuccin-latte (light) | #acb0be 1.91:1 | 82% -> #5d6078 5.45:1 | 27% -> #9296a7 2.60:1 | 0% -> #acb0be 1.91:1 | #9ca0b0 2.30:1, 80% -> #5c5f77 5.53:1 |

In tokyo-night and flexoki-light the ladder text > comment > gutter >=
minimap holds. All 22 stock themes parse without error (M).

## Live switch

The watch reuses the config runtime: its worker, its mailbox and the file
module's shared parent-watch service. No new thread, fd or global is added.
There are two `file_path_watch` members on the stable parent `current/`. One
is named by the theme directory (`theme`: DELETE and MOVED_TO when the
directory is swapped). The other is the sibling `theme.name` (MODIFY and
CLOSE_WRITE, the last events of a switch). Any notice re-runs the existing
reload job. The job reads `config.toml` and then `colors.toml` through one
descriptor. Omarchy replaces the directory and never rewrites a file in
place, so an open file is complete.

The UI thread decides the effective palette in the mailbox receive. A change
to the settings or to the palette bumps the one config generation, and
`editor_preferences_apply` swaps all colours in that frame generation. The
test checks this with the existing probe.

Debounce happens without a timer. Notices coalesce into one pending reload,
and a reload whose palette equals the current one publishes nothing. So a
re-apply (`omarchy-theme-refresh`) costs one read and no frame. An absent
file keeps the last system palette, which covers the window between `rm -rf
theme` and `mv`. At startup an absent file means the built-in palette. A
malformed file gives the built-in palette of the detected mode. The worker
logs one stderr line per episode, and the episode ends when a valid palette
loads.

Watches start once, after the first read. If `current/` does not exist (no
Omarchy), the start fails and is never retried. If `current/` is removed
later, the IN_IGNORED notice triggers one restart attempt. There is no polling
timer, and the test asserts that neither fd is readable once a switch has
settled. Linux watching lives in `src/file/file_posix.c` (already the
platform file). Windows compiles the same code with an empty palette path.

## Not done

- A palette that appears later on a box that had no `current/` at startup is
  picked up only after a restart.
- The hue tokens are mapped literally. Several light themes put hues below
  4.5:1 against their background (M, same run): catppuccin-latte keyword
  2.34:1, warning 2.31:1; flexoki-light string 3.04:1, warning 2.31:1;
  rose-pine warning 2.05:1. The bead limited the contrast rule to
  muted-derived tokens. Whether hues get a floor is an open question for
  Tobias.
