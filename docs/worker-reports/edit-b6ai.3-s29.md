# edit-b6ai.3 worker report (s29): follow fontconfig `monospace` on Omarchy

Box: Omarchy monolith, [AC, desktop monolith], 36 CPUs. Decision note: `docs/decisions/edit-b6ai.3.md`.

## Changed
- `src/config/config.h`, `config_posix.c`, `config_win.c`: `config_fontconf_path`, `config_runtime_open_follow`, `config_runtime_system_font`, `config_runtime_fonts_generation`.
- `src/config/runtime.c`: two `file_path_watch` members for fonts.conf (file parent; directory entry only while `fontconfig/` is missing); `palette_system` flips now bump the config generation.
- `src/editor/editor.h`, `open.c`, `private.h`, `preferences.c`: `editor_config.fontconf_path`; default family `monospace` when nothing explicit is set and the system palette is in force; a fonts.conf notice re-requests an alias request.
- `src/font/discovery_posix.c`: `font_selection_resolve` explicit-family fix (below).
- `tests/font_system_test.c` (new), `docs/config.md`, `docs/decisions/edit-b6ai.3.md`.

## Red
1. Stubbed runtime: `font_system_test:84: FAIL config_runtime_system_font(r)`.
2. Explicit family under an Omarchy-style rule (before the discovery fix): `font.family='Go Mono'` with fonts.conf mapping monospace to DejaVu gave `fam=[Go Mono] file=[] err=-1` (FONT_ERR_MISSING, embedded fallback).

## Go Mono explicit-family fix (discovery_posix.c)
`omarchy font set` writes a strong `prepend_first` on `monospace`. Fontconfig classes many concrete families as monospace, so `fc-match "Go Mono"` with that rule returns the Omarchy font (M: `FONTCONFIG_FILE=<such a file> fc-match "Go Mono"` -> `DejaVuSansMono.ttf`; without it `Go-Mono.ttf`). The edit-5ih exact-family check then rejected the match, so any explicit `font.family`/env/CLI family naming a monospace-class face silently became the embedded face once a user had run `omarchy font set`. The bead requires explicit choices to win, so the resolver now tries the configured match first and, if it does not name the requested family, matches the concrete name once more without config rules. A missing family is still MISSING.

## Nerd Font metrics (M, probe linking font_init_index/font_set_em_px/font_monospace)
| face | hhea asc/desc/gap (upm) | line | cell 14 px | cell 20 px |
|---|---|---|---|---|
| JetBrainsMono Nerd Font Regular | 1020/-300/0 (1000) | 1.320 em | 8x18 | 12x26 |
| DejaVu Sans Mono | 1901/-483/0 (2048) | 1.164 em | 8x16 | 12x24 |
| Go Mono | | | 8x16 | 12x23 |
`font_monospace` accepts it. Extra height is JetBrains Mono's own line spacing (unpatched upstream has the same 1020/300), not the Nerd patch. Request stays the plain alias `monospace`. Installed variant is the non-"Mono" Nerd family; only private-use icons are wider than a cell.

## The :203 failure (sanitizer build)
Cause: test fixture, not product. `colors.toml` sat directly in the per-run temp dir, so the palette's `<dir>.name` sibling watch lived on `/tmp` itself; unrelated `/tmp` entries (`claude-*-cwd`, seen by printing the pending inotify events: wd of `/tmp`, masks 0x100/0x2/0x8) made the shared descriptor readable under load. With Omarchy's layout the watched parent is the private `current/`. Fix: the test now uses `root/current/theme/colors.toml`. The idle assertion is unchanged. Product leaves nothing unconsumed. (Found on the way: a colors.toml directly in a shared directory makes the palette watch cover that directory; irrelevant on a real Omarchy layout.)

## Green
- Sanitizer `font_system_test`: 10 sequential runs all exit 0; 40 concurrent runs all exit 0 (this reproduced the failure before the fixture fix).
- Release `font_system_test`: green 5/5 earlier.
- `make all`: ok. `make TARGET=win all`: exit 0.
- `make check`: `check: 119 test binaries passed in 85.2 s (parallel, 36 jobs, 6 displays)`; one FLAKY line: `scroll_test` failed in parallel, passed alone (3/3 alone), unrelated mapped-truncation timing under load.

## Found / not done
- Only `$XDG_CONFIG_HOME/fontconfig/fonts.conf` is watched: `conf.d/*.conf`, `~/.fonts.conf` and `FONTCONFIG_FILE` are not.
- One atlas rebuild per notice even when the resolved face is unchanged.
- Palette appearing after startup is not picked up (edit-b6ai.2 limit); the font follows it.
- Screenshot skipped per coordinator.
