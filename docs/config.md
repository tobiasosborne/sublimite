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
10 pt. File sizes feed the display DPI font path at startup and request its
existing atlas worker on reload; reset uses the configured size. Removing the
file setting restores the environment override or default. Family selection
remains pending; the requested family is exposed through `editor_preferences`.
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
