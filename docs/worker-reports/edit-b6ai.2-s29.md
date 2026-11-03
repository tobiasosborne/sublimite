# edit-b6ai.2 (session 29): follow the Omarchy palette live

Box: monolith, [AC, desktop monolith] (no battery). Worktree
`.wt/edit-b6ai.2-s29`.

## Outcome

`theme = "system"` is now the default. When
`$XDG_STATE_HOME/omarchy/current/theme/colors.toml` exists (default
`~/.local/state/...`), the editor maps it onto the 31 tokens and follows it
live. Without the file, the built-in dark palette is used with no publication,
so behaviour is byte-identical to before. `"dark"` and `"light"` pin the
built-in palettes. A malformed file gives the built-in palette of the detected
mode and one stderr notice. Nothing runs on the typing path, and no global,
thread or fd was added. The mapping, the contrast rule and the measured ratios
are in [docs/decisions/edit-b6ai.2.md](../decisions/edit-b6ai.2.md).

## Files

- `src/config/config.h`: adds `config_palette`, `CONFIG_THEME_TOKENS`,
  `config_palette_builtin/color/parse/path`, `config_runtime_open_palette`,
  `config_runtime_palette`, `config_runtime_palette_error` and the
  `theme_pinned` setting.
- `src/config/theme.c`: resolver aliases, mode detection, the section 8
  mapping and the ladder rule.
- `src/config/settings.c`: `theme = "system"`.
- `src/config/config_posix.c` / `config_win.c`: `config_palette_path`. On
  Windows it returns `CONFIG_ERR_ARG`, so there is no system palette.
- `src/config/runtime.c`: the worker reads `colors.toml` after
  `config.toml`. Two shared parent-watch members (`theme`, `theme.name`).
  The receive decides the effective palette and bumps the one generation.
- `src/editor/editor.h` (`palette_path`), `open.c`, `preferences.c`
  (`editor_theme_palette_cache`), `private.h`, `session.c`: colours come from
  the runtime palette, and `plat_set_dark` follows its mode.
- `tests/config_palette_test.c` (new), `tests/config_editor_test.c`
  (`palette_preferences_test`), `fuzz/config_palette_fuzz.c` (new).
- `docs/config.md`, `docs/decisions/edit-b6ai.2.md`, `src/config/STATUS.md`,
  `tools/design_shots.sh` (optional `EDIT_SHOTS_THEMES`, see below).

## Red

`tests/config_palette_test.c` was written before the API existed:

```
tests/config_palette_test.c:67:25: error: unknown type name 'config_palette'; did you mean 'config_value'?
tests/config_palette_test.c:67:71: error: implicit declaration of function 'config_palette_color'; did you mean 'config_theme_color'?
```

`config_editor_test` was run with the `palette_path` field added but before
any editor or runtime wiring:

```
config_editor_test: FAIL e->layout_cfg.bg == 0x1a1b26 && e->layout_cfg.fg == 0xa9b1d6
config_editor_test: FAIL palette_preferences_test(root, path) == 0
```

## Green

```
config_palette_test: tokyo-night comment 5.80:1 gutter 3.17:1 minimap 1.91:1 chrome 5.54:1 (built-in dark 5.73 3.13 1.64 5.52)
config_palette_test: flexoki-light comment 5.50:1 gutter 2.61:1 minimap 2.00:1 chrome 5.57:1 (built-in light 5.37 2.59 1.53 5.45)
config_palette_test: built-in tables, tokyo-night and flexoki-light mapping and ladder, resolver fallbacks, malformed files, theme pin, live delete/rename/write switch, absent state passed
config_editor_test: system palette startup, live delete/rename/write switch in one frame generation, pin passed
```

The live test replays the operation order of `omarchy-theme-set` L266-296 in
a `/tmp` directory: mkdir next-theme, write, rm, rmdir, rename, then write
theme.name. It covers:

- one publication per switch, and none on re-apply;
- the previous palette kept while theme/ is absent;
- a malformed file giving the built-in light palette with exactly one notice
  over two malformed applies (stderr captured and counted);
- pin and unpin;
- no readable fd once settled;
- a missing parent and a NULL palette giving the built-in palette and
  generation 0.

The editor test checks that the swap lands in one frame generation (existing
probe) and that the typing key after the swap makes mallocs = 0.

Build and suites:

- `make all`: exit 0.
- `make TARGET=win all`: exit 0.
- `make check`: `check: 3 of 118 test binaries failed in 121.8 s`.
  - The 3 failures are the box's known ones (worklog 2026-11-02, edit-b6ai.1):
    undo_test and savectl_test (transparent hugepages = always) and keys_test:303
    (libxkbcommon 1.13.2). All three also failed when rerun alone.
  - Under PR_SET_THP_DISABLE (python prctl 41 + exec), undo_test and
    savectl_test exit 0 with this tree.
  - scroll_test was flagged FLAKY by the runner: it failed in parallel and
    passed alone.
  - editor_test passed, which includes the G1 typing-latency regression and
    the mallocs=0 guards.
- Before this bead the box was at 114 of 117. The extra binary is
  config_palette_test, which passes.

Fuzz: `build/fuzz/config_palette_fuzz -max_total_time=60` was seeded with
the 22 stock colors.toml files. Result: `Done 309459 runs in 61 second(s)`,
no crash (M) [AC].

## Measurements

Contrast ratios (P: WCAG 2.2 arithmetic on (M) stock files) for all 22 stock
themes are in the decision note. All 22 parse with rc = 0.

## Screenshots

`tools/design_shots.sh` needs `xdotool` and `xwd`, which are not installed on
this box. So I made only a small, untested addition there:
`EDIT_SHOTS_THEMES="tokyo-night flexoki-light"` runs the two lanes with stock
Omarchy themes through `theme = "system"`.

Instead, I took a manual capture. It used a private Xvfb on :93 and :94,
1800x1100 at 144 dpi, with `build/sublimite --new-instance
tools/design_fixtures/hello.c` under private XDG dirs containing a copy of the
stock colors.toml, and `import -window root` after the 10 s idle blink stop.
The script is in `/tmp/b6ai2-tool/shot.sh` and is not committed. Images:

- `/home/tobias/sublimite/.wt/edit-b6ai.2-s29/build/shots-b6ai2/tokyo-night.png`
- `/home/tobias/sublimite/.wt/edit-b6ai.2-s29/build/shots-b6ai2/flexoki-light.png`

Copies are in `/tmp/b6ai2-shots/`.

## Found (not fixed)

- Several light themes put syntax hues below 4.5:1, mapped literally:
  catppuccin-latte keyword 2.34:1 and warning 2.31:1, flexoki-light warning
  2.31:1, rose-pine warning 2.05:1. Whether hues get a contrast floor is
  Tobias's call; the bead limited the rule to muted-derived tokens.
- On a box with no `current/` at startup, a palette that appears later is
  seen only after a restart (watch start is not retried, by design: zero idle
  work).
- design_shots.sh cannot run on monolith until `xdotool` and `xwd` are
  installed (user space).
