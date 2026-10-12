# Configuration

The native editor loads `$XDG_CONFIG_HOME/sublimite/config.toml`, falling back
to `$HOME/.config/sublimite/config.toml` when the XDG value is unset or relative.
The parent directory is created on a worker. Save a valid file to reload it;
invalid edits preserve the previous settings. Removing settings, or deleting
the file, restores their defaults.

```toml
theme = "light" # "dark" is the default
wrap = true    # otherwise use the existing per-file default

[font]
size = 12.5    # points: fractional fixture value from config_test
family = "DejaVu Sans Mono"

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

Font selection uses the highest supplied source as a whole family/file pair:
`--font-family` / `--font-file`, then nonempty `EDIT_FONT_FAMILY` /
`EDIT_FONT_FILE`, then `[font] family` / `file`, then embedded DejaVu Sans Mono.
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
