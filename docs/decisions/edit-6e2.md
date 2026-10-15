# edit-6e2: configurable font fallback chain (`font.fallbacks`)

Status: implemented (S7 worker, 2026-10-16). Coordinator decisions 1-4 from
the brief are recorded here, along with the choices made while coding.

## Key and precedence

- Config: `font.fallbacks = ["Noto Emoji", "Noto Sans CJK SC"]` (dotted key or
  `[font] fallbacks`). The value must be an array of strings. Each entry must
  be non-empty and free of `\n\r\t`, the same rule as `font.family`. Anything
  else is `CONFIG_ERR_VALUE`, the usual reload warning (`config_runtime_error`),
  and the previous settings stay current. `[]` means an empty chain.
- Storage: `config_settings.font_fallbacks[CONFIG_TEXT_CAP]` holds the entries
  joined by `'\n'`. (P) The parser decodes every string of one array into a
  single `CONFIG_TEXT_CAP` buffer with one NUL per entry, so the joined form
  (one separator in place of each NUL) always fits. No entry count is invented.
  `CONFIG_ENTRY_CAP` grows from actions + 7 to actions + 8 named scalars.
- Precedence uses the edit-5ih admission rules, applied to this key on its own.
  Family/file selection and the chain are independent keys, so a CLI family
  does not suppress a config chain:

| Winning source, highest first | Admission | Config reload effect |
| --- | --- | --- |
| CLI `--font-fallbacks A,B` / `--font-fallbacks=A,B` | Flag supplied, including empty (= explicit empty chain) | Chain stays the CLI one |
| `EDIT_FONT_FALLBACKS=A,B` | Non-empty, no CLI flag | Chain stays the startup environment one |
| `font.fallbacks` | Neither of the above | A changed chain requests a new font version; an unchanged one does nothing |
| (none) | - | Empty chain = today's per-scalar fontconfig discovery |

  For the CLI and environment forms, entries are split on commas, surrounding
  spaces are trimmed and empty entries are dropped. A control character makes
  the list invalid: the editor logs once (existing `editor_font_bad_config`)
  and uses an empty chain.

## Resolution (worker only)

- `font_chain_resolve(entry)` (src/font/fallback.c):
  - `file:PATH[#INDEX]` uses the `font.file` grammar (`font_selection_parse`).
    It needs `stat` to show a non-empty regular file, otherwise `FONT_ERR_INIT`.
    Relative paths resolve against the cwd, which is what the tests use.
  - Any other entry goes to the existing resolver as `font_selection_resolve(entry + ":color=false")`.
    That resolver uses in-process dlopen'd fontconfig and requires an exact
    family match, so an absent family returns MISSING rather than a substitute.
  - Why `:color=false`: fontconfig ranks `color` above `family`. On this box
    plain `fc-match "Noto Emoji"` returns `NotoColorEmoji.ttf` (M), which is
    CBDT and gives empty boxes under stb, and the exact-family check would then
    reject the entry. With `:color=false`, `"Noto Emoji"` resolves to the
    monochrome `~/.fonts/NotoEmoji-wght.ttf#0`. A family that exists only in
    colour (`"Noto Color Emoji"`) resolves to MISSING and is skipped. This
    makes the colour-vs-monochrome choice explicit: the chain is monochrome
    by construction. A `file:` entry is taken as given; pointing it at a CBDT
    file is the user's explicit choice and draws nothing.
- Where it runs: `font_async_set_fallbacks` is called from the editor's
  `font_worker` (WORK_FOREGROUND, the job that already resolves the primary
  family). It runs after `font_async_reset_atlas` / `set_primary`, never on
  the UI thread (the test asserts `e->font.worker != pthread_self()`).
- Unresolvable entries are skipped and counted in
  `editor_stats.font_fallback_skipped`, which is set on every adoption next to
  `font_error`. The worker logs one stderr line per generation. `font_error`
  stays `FONT_OK`: a skipped entry is a notice, not an error.
- Cost (M)[AC, BAT0=Full, load 3.3]: in-process resolution took 14.9-18.7 ms
  per family entry (`Noto Emoji`, `Noto Sans CJK SC`, `DejaVu Sans`,
  `absent-family`; scratch probe linking build/libedit.a) and 0.0 ms per
  `file:` entry. The cost is paid once per chain text per owner, never per
  zoom (see below).

## Lookup order and cache

Per scalar, inside `resolve_scalar` (src/font/async.c, glyph worker):

1. The primary face.
2. The per-codepoint scalar cache (`owner->scalars[]`). Every outcome below
   is cached, so the chain is consulted once per scalar.
3. The configured chain, in order: `owner->faces[0..chain_count)`. A face is
   loaded lazily on its first miss (`load_face`, factored out of the existing
   discovery load), so an unused CJK face never reads its file.
4. Discovery faces already loaded (`faces[chain_count..face_count)`).
5. Per-scalar fontconfig discovery (`font_fallback_codepoint`, isolated
   fc-match child), as before.

The cache stays per codepoint, as the brief directs (the bead title says
ranges; the code map shows per-codepoint, and that is fine).

Faces: chain entries occupy slots in the async owner's existing face table
(capacity `ASYNC_ENTRIES`, (P) one slot per entry). Entries past that
capacity are counted as skipped. `FONT_FAMILY_MAX_FACES` is unchanged: the
editor's Unicode path never uses `font_family` beyond its primary, and the
fixed-probe `font_family` path is the legacy `font_runtime` only (see
"Found, not fixed" in the report).

Zoom: `font_async_reset_atlas` keeps the resolved chain prefix. It drops the
bytes and re-arms each entry as `FONT_MORE` so it reloads at the new size. The
owner also keeps the text the chain was resolved from, so
`font_async_set_fallbacks` with unchanged text does no fontconfig work. Each
of the two version owners resolves a new chain at most once.

Memory (P): `FONT_CHAIN_TEXT_MAX` (4096 B) in each of the two async owners,
the editor's UI request and two version snapshots (3 x 4096 B), and the
effective preference chain (4096 B). The total is 24 KiB, reserved at INIT.
A loaded chain face costs its file size in an mmap arena, the same as a
discovery face.

## What an empty list picks on this box (M)

Measured 2026-10-16 with `fc-match`, the same program and patterns the
editor's per-scalar discovery spawns:

| Query | File |
| --- | --- |
| `:charset=1f469:color=false` (U+1F469 WOMAN) | `/home/tobias/.fonts/NotoEmoji-wght.ttf` #0 (monochrome) |
| `:charset=1f600:color=false` (U+1F600) | `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` (it covers U+1F600-1F64x emoticons) |
| `Noto Emoji` without `:color=false` | `/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf` (CBDT) |

So with no chain, the editor's per-scalar discovery already avoids the colour
font, because it queries `:color=false` and rejects `color=True`. The emoji
it picks then depend on fontconfig's preference: DejaVu Sans for the
emoticon block, Noto Emoji elsewhere. `font.fallbacks = ["Noto Emoji"]` makes
the monochrome Noto Emoji win for every scalar it covers.

## API added (src/font/font.h, async section)

`FONT_CHAIN_TEXT_MAX` (= `CONFIG_TEXT_CAP`, static-asserted in
src/editor/preferences.c), `font_chain_resolve`, `font_async_set_fallbacks`,
and `font_async_scalar_source`. The last is a UI diagnostic for use only
while `font_async_idle`; tests use it to see which face file served a scalar.

Editor: `editor_config.font_fallbacks` (CLI), `editor_stats.font_fallback_skipped`,
and the internal `editor_font_request_chain` (selection and chain in one
generation, so a startup family plus chain prepares one version, not two).
