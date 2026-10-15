# edit-6e2 (S7 worker): configurable font fallback chain

Status: done. The bead's scope is complete; the tree builds and every
requested suite passes. Design and derivations are in
docs/decisions/edit-6e2.md.

## Files touched

- src/config/config.h: `font_fallbacks[CONFIG_TEXT_CAP]` field (derivation in comment); `CONFIG_ENTRY_CAP` changed from +7 to +8 named scalars.
- src/config/settings.c: `font.fallbacks` branch (array only, entries non-empty, no `\n\r\t`).
- src/ipc/ipc.h, src/ipc/args.c: `--font-fallbacks A,B` / `=A,B` (repeat keeps last, empty is explicit).
- src/main.c: maps `args.font_fallbacks` into `editor_config`; adds it to the usage line.
- src/editor/editor.h: `editor_config.font_fallbacks`, `editor_stats.font_fallback_skipped`.
- src/editor/private.h: chain request/snapshot/effective-preference fields; `editor_font_request_chain`.
- src/editor/font.c: `editor_font_request_chain` (selection + chain in one generation); `editor_font_request` wraps it.
- src/editor/font_size.c: worker calls `font_async_set_fallbacks`, records the skip count, logs once per generation; the count is adopted into stats; the chain is copied into each version snapshot.
- src/editor/preferences.c: CLI > env > config precedence for the chain, comma-list normalisation, reload handling, `_Static_assert(FONT_CHAIN_TEXT_MAX == CONFIG_TEXT_CAP)`.
- **src/font files touched: src/font/font.h (one new block after `font_async_event`), src/font/async.c, src/font/fallback.c.** Not touched: unicode.c, gsub.c, discovery.c, selection.c/.h.
- tests/config_test.c, tests/font_cli_test.c, tests/font_selection_test.c: new cases. fuzz/config_fuzz.c: chain invariants.
- docs/config.md (user docs for the key), docs/decisions/edit-6e2.md (new).
- tests/fixtures/fonts/ (NotoEmoji-wght.ttf, PROVENANCE.md, licence): already copied in by the coordinator and used only by font_selection_test through `file:` entries; nothing is embedded.

## Red run (tests written first)

Step 1, the tests alone: compile red.
```
tests/config_test.c:138:40: error: 'config_settings' has no member named 'font_fallbacks'
make: *** [Makefile:149: build/rel/tests/config_test.o] Error 1      make exit=2
```
Step 2, declarations plus stub bodies (stubs return MISSING / skip 0): runtime red. Stamp: BAT0=Full, loadavg 4.13 4.80 4.17.
```
config_test exit=1
config_test: FAIL !strcmp(chain_settings.font_fallbacks, "Noto Emoji\nfile:/abs/x.ttf#1")
font_cli_test exit=1
font_cli_test: FAIL PARSE(chain, &args) == IPC_OK && !args.request.count
font_selection_test exit=1
font_selection_test:397: FAIL font_chain_resolve("file:tests/fixtures/fonts/NotoEmoji-wght.ttf#0", resolved, sizeof resolved, &index) == FONT_OK
font_selection_test:479: FAIL fallback_chain() == 0
```

## Green run

Release build. Stamp: BAT0=Full, loadavg 3.11 4.13 4.07.
```
config_test exit=0          config_test: parser, settings and keymap passed           wall 0.01 s (M)[AC]
font_cli_test exit=0        font_cli_test: family/file/fallbacks options, required values and option terminator passed
font_selection_test exit=0  font_selection_test: font.fallbacks chain order, skipped entries, CLI/env/config precedence, typing mallocs=0
                            ... all later existing lines pass; wall 1.05 s (M)[AC] (tracked print, not a gate)
```
What the new tests cover:
- (a) `['absent-family', 'file:<NotoEmoji fixture>']`. U+1F469 is served by the fixture path, checked through `font_async_scalar_source`. fontconfig would return an absolute path, never this relative fixture path. The glyph inks as a 2-cell image. `font_fallback_skipped == 1`, `font_error == FONT_OK`, and resolution ran on a worker thread.
- (b) `['file:/missing/edit-6e2.ttf']`: skipped (count 1) without error. The scalar falls through to fontconfig.
- Order, when fontconfig knows DejaVu Sans (it does on this box, so the case ran). `['DejaVu Sans', fixture]` serves U+1F600 from DejaVuSans.ttf; the reverse order serves it from the fixture. The order survives Ctrl+= zoom and Ctrl+0 reset.
- Removing the key restores fontconfig, with one new generation.
- (c) Precedence. Env beats config, and a config reload under an env override does not rebuild. CLI beats env. An empty CLI value beats env and config and starts no generation (`font_changes == 0`). With no CLI or env, the config chain applies.
- (d) config_test: the array parses into the `\n`-joined form, and `[]` gives an empty chain. A string, an integer, a boolean, an empty entry or an entry with a tab is `CONFIG_ERR_VALUE`, and the previous settings are kept. At runtime a non-array reload gives `config_runtime_error == CONFIG_ERR_VALUE` with the generation and chain unchanged.
- `font_chain_resolve` unit cases: a `file:` entry with `#0`, a missing file, an empty `file:`, a bad `#index`, and an absent family. `"Noto Emoji"` (when installed) never resolves to the Color file.

## Verification commands (all exit 0)

Stamp: BAT0=Full; loadavg 3.29-5.62 during these runs.
- `make -j4 all`: exit 0.
- `DISPLAY=:99 sh tools/with_display_lock.sh build/tests/editor_test` (release, counting-allocator guard): exit 0. It prints
  `edit-g11 null typing: keys=95 frames=97 ... mallocs=0`, `editor_test: null 10000 keys mallocs=0 guard=active`,
  `editor_test: raster 10000 keys mallocs=0 guard=active`, `editor_test: all passed`.
- `make -j4 build/san/tests/{font_selection_test,font_test,font_cli_test,config_test,config_editor_test,editor_test}`: exit 0.
- With `ASAN_OPTIONS=detect_leaks=1`, each run on its own from build/san/tests:
  font_selection_test 0, font_test 0, font_cli_test 0, config_test 0, config_editor_test 0, and
  `DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test` 0.
- `build/fuzz/config_fuzz -runs=200000 <seed dir with font.fallbacks inputs>`: exit 0, "Done 200000 runs in 2 second(s)". The run count is an arbitrary smoke-run choice, not a budget.

## Measurements (M)[AC, BAT0=Full, load 3.26]

- In-process chain resolution per family entry: 14.9-18.7 ms (Noto Emoji, Noto Sans CJK SC, DejaVu Sans, absent-family). A `file:` entry takes 0.0 ms. This is worker time, once per chain text per owner.
- What fontconfig picks with an empty list, with no chain configured (fc-match, the same program and patterns the editor's discovery uses):
  - U+1F469 → ~/.fonts/NotoEmoji-wght.ttf (monochrome).
  - U+1F600 → DejaVuSans.ttf.
  - Plain `fc-match "Noto Emoji"` → NotoColorEmoji.ttf (CBDT). That is why chain families resolve with `:color=false`. Table in the decision doc.

## What remains

Nothing in scope. Possible follow-ups, not done:
- A user-visible status line for `font_fallback_skipped`. Today the notice is the stats field plus one stderr line per generation.
- The two version owners each resolve a new chain once, so the first zoom after a chain change pays the fontconfig cost again on the other owner (about 15-19 ms per family entry, measured above). This happens on the worker while the old atlas stays on screen.

## Found, not fixed

1. The fixed CJK/emoji probes (`font_fallback_discover` / `font_fallback_isolated` → `font_family_load`) feed only the legacy `font_runtime` owner. The editor's Unicode path (`font_async`) never uses them, so that path ignores `font.fallbacks`. If `font_runtime` is dead in production it could be retired. Not touched.
2. `editor_font_bad_config`'s message says "invalid font selection; using embedded DejaVu Sans Mono". That message is also used for an invalid `--font-fallbacks` / `EDIT_FONT_FALLBACKS` list (a control character), where the wording is inaccurate. It is cosmetic and rare.
3. The chain lookup, like the existing per-scalar path, calls `font_load_file` (stdio `fopen`, which allocates) on a glyph worker. If that happens during a typing window, the process-wide malloc guard would count it. The tests settle before typing. This is pre-existing behaviour for discovery faces and is not new with the chain.
