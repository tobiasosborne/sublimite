# Configuration

The native editor loads `$XDG_CONFIG_HOME/sublimite/config.toml`, falling back
to `$HOME/.config/sublimite/config.toml` when the XDG value is unset or relative.
The parent directory is created on a worker. Save a valid file to reload it;
invalid edits preserve the previous settings. Removing settings, or deleting
the file, restores their defaults.

```toml
theme = "light" # "system" (follow Omarchy, else dark) is the default
wrap = true    # otherwise use the existing per-file default

[font]
size = 12.5    # points: fractional fixture value from config_test
family = "DejaVu Sans Mono"
fallbacks = ["Noto Emoji", "Noto Sans CJK SC"] # tried in order after the primary

[tabs]
width = 8      # tab-stop fixture from config_test; bounded by INDENT_MAX_WIDTH
spaces = true

[keymap]
redo = "Ctrl+Y"
undo = ["Ctrl+Z", "Alt+BackSpace"]
```

Native font-size precedence is `EDIT_FONT_SIZE_PT`, then `font.size`, then
the existing default (E) `EDITOR_FONT_DEFAULT_PT`, chosen for the Sublime
reference in `src/editor/runtime.h`. File sizes feed the display DPI font path
at startup and request its existing atlas worker on reload; reset uses the
configured size. Removing the file setting restores the environment override
or default.

Display density (X11 and XWayland, edit-b6ai.15): an explicit `Xft.dpi` always
wins. Otherwise, on an XWayland server (detected by its `XWAYLAND` X extension)
the density is 96 times `EDIT_DISPLAY_SCALE` (default 1), because XWayland
reports the monitor's physical size, which is not a UI density: a 4K monitor
reporting 1860 x 1040 mm gave 52.5 dpi and 7 px text. On a real X server the
physical RandR size is used as before. Set `EDIT_DISPLAY_SCALE=1.5` (finite,
positive) if your compositor scales the monitor and does not scale XWayland
clients; `EDIT_FONT_SIZE_PT` or `font.size` also remain available.

Font selection uses the highest supplied source as a whole family/file pair:
`--font-family` / `--font-file`, then nonempty `EDIT_FONT_FAMILY` /
`EDIT_FONT_FILE`, then `[font] family` / `file`, then the default: Fontconfig's `monospace` while the
editor follows Omarchy (see below), else embedded DejaVu Sans Mono.
Within a source, `file` wins. For example, a CLI family overrides an environment
file. Flags accept a following value or `=VALUE`; an explicitly empty CLI pair
selects the embedded default. Flags apply when launching an editor; use
`--new-instance` to select a font independently of an already running instance.

`family` is a Fontconfig pattern such as `Iosevka Fixed:semibold`. `file` is a
font path, optionally followed by `#decimal` for a TTC face. Resolution, loading,
monospace validation and atlas preparation run on a worker. A changed config
selection swaps at a frame boundary; an unchanged selection does nothing.
Removing both keys restores embedded DejaVu unless a startup source overrides
config. A failed selection uses embedded DejaVu and logs once per editor.
Use Iosevka Fixed or Term: the cell grid has no HarfBuzz, ligatures or contextual
alternates, and proportional advances are rejected. The exact precedence and
ownership decisions are in [edit-5ih](decisions/edit-5ih.md).

Following the system font (edit-b6ai.3): when none of the sources above names a
family or file, and the Omarchy palette is in force (`theme = "system"` and
`$XDG_STATE_HOME/omarchy/current/theme/colors.toml` exists), the default family is
Fontconfig's `monospace` match. `omarchy font set` stores its choice there
(`~/.config/fontconfig/fonts.conf`; this box: JetBrainsMono Nerd Font). The editor
watches `$XDG_CONFIG_HOME/fontconfig/fonts.conf` (`$HOME/.config/...` when unset or
relative) on its parent directory, so atomic replacement is seen, and also when
`fontconfig/` does not exist yet. A change makes the font worker resolve
`monospace` again and the new face swaps at a frame boundary, like any font
change. No polling is involved: an unchanged box costs nothing. gsettings is not
read. The size stays sublimité's own (`font.size`, `EDIT_FONT_SIZE_PT`, zoom).
An explicit `--font-family` / `--font-file`, `EDIT_FONT_FAMILY` / `EDIT_FONT_FILE`,
`font.family` or `font.file` always wins, even when the Omarchy rule would
capture it (an exact family name is matched without configuration rules if the
configured match names another family). Without Omarchy, or with `theme =
"dark"` / `"light"`, the default stays the embedded DejaVu Sans Mono. The proportional-face
rule still applies: a `monospace` that is not fixed-pitch falls back to the
embedded face with one log line. Details: [edit-b6ai.3](decisions/edit-b6ai.3.md).

`fallbacks` is an ordered array of font families (Fontconfig patterns) or
`file:PATH[#INDEX]` entries. For a character that the primary font does not
have, each listed font is tried in turn, and the first one that has the
character is used. If none of them has it, the editor asks Fontconfig, as it
did before. An empty or missing list uses Fontconfig alone. Families are
resolved without colour fonts (`:color=false`): the rasteriser cannot draw
colour bitmap emoji, so `"Noto Emoji"` selects the monochrome Noto Emoji, never
Noto Color Emoji. If an entry cannot be found, or names a missing file, it is
skipped. The editor logs one line about it, and it is not an error. The
precedence works like font selection: `--font-fallbacks A,B` (an empty value
also counts), then a nonempty `EDIT_FONT_FALLBACKS=A,B`, then
`[font] fallbacks`. Spaces around commas are ignored. Families are resolved on
a worker; see [edit-6e2](decisions/edit-6e2.md).

`theme` takes `"system"` (the default), `"dark"` or `"light"`. `"system"`
follows the Omarchy palette at
`$XDG_STATE_HOME/omarchy/current/theme/colors.toml` (`$HOME/.local/state/...`
when the XDG value is unset or relative). On a box without that file the
editor uses the built-in dark palette, exactly as before. `"dark"` and
`"light"` always use the built-in palettes, even on Omarchy. Windows has no
system palette, so `"system"` means the built-in dark palette there.

When you switch theme in Omarchy (`omarchy-theme-set`), the editor picks it
up: it watches the `current/` directory, reads the new file on a worker and
swaps the colours at a frame boundary. No polling is involved. A malformed
file gives the built-in palette for the mode it names (dark if it names
none) and logs one line to stderr. While the theme directory is briefly
missing during a switch, the previous palette stays. Only the file is read; no
Omarchy command is run and nothing is installed. Comment, line-number,
minimap and inactive-tab colours are lifted toward the foreground until they
match the built-in themes' contrast, and the caret uses `accent`. The token
mapping and the contrast rule are in [edit-b6ai.2](decisions/edit-b6ai.2.md).

Theme token names and RGB values are provisional until the visual spec is
present and approved.

Keymap action names are the lowercase `KEYS_ACTION_*` suffixes from
`src/keys/keys.h`. Values are standalone strokes; arrays supply alternatives.
Supported modifier names are `Ctrl`, `Shift`, `Alt`, and `Super`. Keys can be
single ASCII characters or XKB keysym names such as `BackSpace` or `Page_Down`.
Duplicate strokes and unknown actions fail the whole reload. An override
replaces that action's default bindings. Existing parameterized defaults
supply the arguments; custom multi-stroke prefixes are outside the subset.

The supported TOML forms and explicit parser bounds are documented in
[the config decision](decisions/edit-v0o.7.md).

## Omarchy default editor

This is not a setting in `config.toml`. `make install-omarchy` (opt-in, see the
README) writes Omarchy's own `defaults/editor` file and a menu row. The only
knob it adds is an environment variable read by the installed launcher script:
`SUBLIMITE_OMARCHY_WAIT=1` makes it wait until the tab is closed even when
Omarchy opened it, `SUBLIMITE_OMARCHY_WAIT=0` makes it return at once even for
`$EDITOR`. Unset, it waits exactly when the caller is not an Omarchy-launched
terminal window. See [edit-b6ai.11](decisions/edit-b6ai.11.md).
