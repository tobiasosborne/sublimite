# edit-b6ai.3: follow fontconfig's `monospace` on Omarchy

Research: [theme-system.md](../design/omarchy-2026-11-02/theme-system.md) section 4.

## Rule

The default family is `monospace` (Fontconfig's generic alias, which
`font_selection_resolve` already supported) when **all** hold: no CLI pair, no
`EDIT_FONT_FAMILY`/`EDIT_FONT_FILE`, no `font.family`/`font.file`, and
`config_runtime_system_font()` is true. That is: a fonts.conf path was given,
settings are not pinned (`theme = "system"`) and the system palette is in force
(colors.toml was readable; it stays true while `theme/` is briefly absent
during a switch, like the palette). So "Omarchy" means the same thing for
colours and font. A pinned `theme = "dark"` returns to the embedded face, and
that flip bumps the config generation (`palette_system` is now part of the
change test in `receive`).

Outside Omarchy nothing changes: `config_palette_path` has no file, the flag is
false, the embedded DejaVu stays. This is the only case touched, so a future
default elsewhere (edit-kf04, IBM Plex) composes: it sits in the "otherwise"
branch.

`editor_config.fontconf_path` is both the watch path and the switch. Native
startup (`load_preferences`) fills it from `config_fontconf_path()`; tests leave
it NULL and stay hermetic on this Omarchy box (a test with a palette but no
fonts path never follows).

## Live

fonts.conf is not read by the config worker. The font worker already calls
`InitLoadConfigAndFonts` on every resolve, so it re-reads the file itself; the
only missing piece is a notice. `config_runtime` keeps two `file_path_watch`
members on the shared inotify service (no new fd or thread):

- `fonts_watch[1]`: `.../fontconfig/fonts.conf`, i.e. the parent `fontconfig/`
  and the name. `omarchy font set` and editors replace files, which the parent
  watch sees as MOVED_TO.
- `fonts_watch[0]`: the entry `fontconfig` in its parent (`~/.config`), used
  only while `fontconfig/` does not exist (this box before the first
  `omarchy font set`) and dropped as soon as `fonts_watch[1]` starts. A busy
  parent like `~/.config` would otherwise wake the UI for unrelated writes;
  those events are filtered by name in dispatch but would still be read.
  If `fontconfig/` is deleted later, `fonts_watch[1]` gets IN_IGNORED and the
  entry watch takes over again.

Each notice, and each re-start of the file watch after the first one, bumps
`fonts_generation` (UI thread; not the settings generation). The first start
does not bump, so startup builds one atlas, not two. The editor, in
`font_selection_poll`, turns a bump into `preferences_font_refresh` when the
request is the alias `monospace` (default or explicit, no file) and re-requests
the font even though the text is identical; the existing retain/retry for an
in-flight request applies, and a burst of notices coalesces into one flag.
Zero idle wakeups: inotify only.

## Explicit choice must beat the Omarchy rule (fix found by the test)

`omarchy font set` writes a **strong `prepend_first`** on `monospace`. For
fontconfig, many concrete families are classed monospace, so
`fc-match "Go Mono"` with that rule returns the Omarchy font (M: temp
fonts.conf equal in shape to omarchy's, `FONTCONFIG_FILE=... fc-match "Go
Mono"` -> `DejaVuSansMono.ttf` against `Go-Mono.ttf` without the rule). The
edit-5ih exact-family check then rejected the match and an explicit
`font.family = "Go Mono"` silently fell back to the embedded face on a box that
had run `omarchy font set`. Red run: `family='Go Mono'` under the rule gave
`err=-1` (FONT_ERR_MISSING). `font_selection_resolve` now tries the configured
match first (aliases, `monospace`), and, if it does not name the requested
family, matches the concrete name once more without configuration rules.
Exact-family semantics are unchanged (a missing family is still MISSING).

## Nerd Font cell metrics (M, 2026-11-03, [AC, desktop monolith])

Probe linking `font_init_index` / `font_set_em_px` / `font_monospace` on the
installed faces (table values read with a small python struct parser of
`head`, `hhea`, `OS/2`):

| face | upm | hhea asc/desc/gap | line (em) | cell at 14 px | cell at 20 px |
|---|---|---|---|---|---|
| JetBrainsMono Nerd Font Regular | 1000 | 1020 / -300 / 0 | 1.320 = (1020+300)/1000 | 8x18 (asc 14, desc 4) | 12x26 |
| DejaVu Sans Mono (embedded shape) | 2048 | 1901 / -483 / 0 | 1.164 = 2384/2048 | 8x16 | 12x24 |
| Go Mono | - | - | - | 8x16 | 12x23 |

`font_monospace` accepts the patched face (all printable ASCII share one
advance, 600/1000 em). The cell is 12.5 percent taller than DejaVu at 14 px
(18/16), which is JetBrains Mono's own line spacing: the unpatched upstream
face has the same 1020/300 metrics, so the Nerd patch adds no height (the
patched glyphs fit inside the em box's ascent and descent). The installed
family is the non-`Mono` Nerd variant (`JetBrainsMono Nerd Font`, alias `NF`;
`JetBrainsMono Nerd Font Mono` is not installed here), whose icons are
wider than one cell; they matter only for private-use codepoints, which the
cell grid places by the font cache's width rule, not for ASCII. No
fontconfig request change was needed: the request stays the plain alias
`monospace`, which returns exactly what Omarchy's terminals use, and a face
that fails the fixed-pitch test is refused by `font_monospace` and replaced by
the embedded face. If a future Omarchy font has a spacing variant, the user
sets it through `omarchy font set`; we do not rewrite the request.

## Not done

- Only `$XDG_CONFIG_HOME/fontconfig/fonts.conf` is watched. `conf.d/*.conf`,
  `~/.fonts.conf` and `FONTCONFIG_FILE` are not (Omarchy writes the first).
- The face is re-selected on every notice even if the resolved file did not
  change (one atlas build; no compare of the resolved path).
- The palette-appears-later restart limit from edit-b6ai.2 applies to the font
  as well.
