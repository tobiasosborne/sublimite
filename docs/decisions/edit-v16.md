# edit-v16 — bounded GSUB ligature subset for emoji clusters

ZWJ sequences, skin-tone sequences and flags now draw the emoji font's
single ligature glyph instead of edit-r8e's first-emoji projection when the
resolved face carries a GSUB that ligates them. No shaping engine: one
grapheme cluster, glyph ids from the face's cmap, the face's enabled
ccmp/liga/rlig lookups in LookupList order, a bounded glyph array.

## What the fixture needs (M)

`tests/fixtures/fonts/NotoEmoji-wght.ttf` (variable monochrome Noto Emoji,
OFL; fontTools 4.62.1 dump, 2026-10-16):

| lookup | type | format | role |
|---|---|---|---|
| 0 | 4 ligature | 1 | 3768 ligatures: ZWJ, skin-tone, flag (RI pair) and tag sequences |
| 1 | 2 multiple | 1 | 62 regional indicators -> empty sequence (deletion); nested only |
| 2 | 1 single | 2 | 25 regional indicators -> `unknownFlag`; nested only |
| 3 | 6 chained | 3 | RI after `cancel.tag`/`unknownFlag` -> lookup 1 |
| 4 | 4 ligature | 1 | U+2691 + CANCEL TAG -> `unknownFlag` (2 ligatures) |
| 5 | 5 context | 3 | invalid RI pair -> `unknownFlag` + deletion (lookups 2, 1) |

Script DFLT only, default LangSys, one feature `ccmp` = lookups 0, 3, 4, 5.
Lookup flag 0 everywhere. No ligature contains U+FE0F (0 of 3768); the
longest ligature has 9 components. Largest ligature set: 397 entries;
U+1F469 WOMAN has 392.

NotoColorEmoji 2.047 (system, CBDT) has the same shape (ccmp = 0, 2, 3, 4;
its chain is format 2) but stb cannot rasterise CBDT, so it never reaches
this path.

## Decisions

1. **Supported subset.** Type 4 (format 1) and type 6 format 3, with type 7
   unwrapped. A type 6 record's nested lookup is applied when it is type 1
   (formats 1, 2), 2 (format 1, deletion included) or 4; deeper nesting is
   not followed. Type 6 formats 1 and 2, type 5 (lookup 5 above), types 3
   and 8 are skipped. ClassDef is therefore not implemented: only chain
   format 2 uses it, and the fixture uses format 3 (the brief listed ClassDef
   1/2 "as far as Noto needs"; it does not need them).
   Lookup 3 cannot fire inside one grapheme cluster (UAX #29 GB12/13: an RI
   pair starts its cluster; CANCEL TAG is Extend and cannot precede an RI in
   the same cluster). It is implemented anyway, as the brief asks, and
   tested directly on a glyph array: `[unknownFlag, ARegion] -> [unknownFlag]`.
   Skipping type 5 means an invalid RI pair keeps today's overlay of both
   indicators instead of `unknownFlag` (a fidelity gap, not a safety one).
2. **Script/feature selection.** DFLT, else latn, default LangSys; features
   ccmp, liga, rlig plus the required feature. Unique enabled lookups are
   sorted ascending (LookupList order). Lookups with any LookupFlag bit other
   than RightToLeft are skipped: ignore-marks/ligatures/bases and mark
   filtering need GDEF, which we do not read.
3. **Variation selectors.** As HarfBuzz skips default ignorables while
   matching, a glyph equal to the face's FE0E/FE0F glyph is skipped unless it
   matches the expected component; a matched span absorbs it. Needed because
   fully qualified sequences (e.g. `❤️` inside the kiss sequence) carry FE0F
   and Noto's ligatures never do. ZWJ is never skipped (it is a component).
4. **Routing.** The GSUB path applies when the cluster's first nonzero-width
   scalar is Extended_Pictographic **or Regional_Indicator**. The brief said
   Extended_Pictographic only; regional indicators are not
   Extended_Pictographic, and the flag acceptance case needs them.
5. **Plumbing: layout hands emoji clusters over whole.** The glyph callback
   has no face, so layout cannot decide. `layout_cluster_glyph` now passes an
   emoji cluster (`utf8_cluster_emoji`) unprojected; every other cluster is
   projected exactly as before (byte-identical keys). `font_cache_render`
   offers an emoji key to the face that resolves its base scalar; if
   `font_gsub_cluster` reports a substitution it paints the glyph ids
   (first at the pen origin, the rest overlaid, a zero-advance glyph at the
   first's advance); otherwise it composes through `utf8_artwork_step`, the
   r8e projection as a scalar step (`utf8_cluster_artwork` now uses the same
   step), so the image equals the projected key's image byte for byte
   (asserted in font_gsub_test). Consequence: `👩‍💻` has its own cache key and
   atlas slot even when the face has no ligature (one more tile per distinct
   emoji cluster, inside the existing cache capacity). The unicode_render and
   layout tests that asserted first-scalar slot identity now assert the whole
   key and, where no ligature exists, bitmap identity with the first emoji.
   The tab strip passes whole clusters already; it now also gets the
   projection/ligature instead of an overlay of every scalar.
6. **After substitution, r8e on the glyph stream.** Leftover skin-tone and
   selector glyphs are dropped; a leftover ZWJ ends a pictographic cluster
   (dropped in an RI cluster). `👩‍💻‍😀` (not RGI) therefore draws the
   technologist ligature alone. A remaining glyph 0 or no substitution at all
   means FONT_ERR_MISSING: the caller keeps the r8e projection.
7. **Where GSUB runs.** Only on the resolver path (worker: `font_async`
   glyph jobs). The family-scan path stays projection-only because it is the
   UI-admissible `font_raster_glyph_ui` path and the first-use validation of
   a face's GSUB is linear in the table.
8. **No arena.** The brief said "parsed once per face into an arena". The
   validator proves every structure reachable from the applied lookups once
   per face, lazily at the face's first emoji cluster on the worker, and
   records a fixed `font_gsub` descriptor inside `font_t` (table pointer,
   length, LookupList offset, up to 16 lookup indices, state). Apply then
   reads the immutable table bytes directly, re-proving each bound, with
   fixed stack arrays. Same property (no allocation at paint/layout time;
   nothing at all allocated), no arena sizing. A malformed table sets state
   NONE for that face: r8e from then on, never a crash.
9. **Raster by glyph id.** `font_raster_glyph_id` beside `font_raster_glyph`
   (worker only; the glyph id gets the same proof a cmap result gets).
   `font_glyph_id` exposes the existing bounded cmap. The variable font
   renders at its default instance: stb ignores fvar/gvar.

## Bounds (law 4)

- `FONT_GSUB_MAX_GLYPHS = 10` (P): the longest RGI emoji sequence in Unicode
  15.1 emoji-zwj-sequences.txt is kiss / couple with heart with two skin
  tones, 1F469 1F3FB 200D 2764 FE0F 200D 1F48B 200D 1F468 1F3FC = 10 scalars.
  Longer clusters keep the projection.
- `FONT_GSUB_MAX_LOOKUPS = 16` (E): both Noto emoji fonts enable 4 (M); 4x.
  A face over the cap has GSUB disabled, never truncated.
- Validator budget 4 x (len / 2 + 1) steps (E): an unshared table is walked
  about once per 16-bit word. Fixture measured 19461 steps for a 38076-byte
  table (19038 words): 1.02x (M). Bounds parse time on hostile aliasing.
- Apply budget 23960 ops per cluster (E): 4 x 5990, the worst work measured
  on the fixture's heaviest 10-glyph arrays (ten WOMAN glyphs, 392
  ligatures each) (M). Exhaustion = no substitution (projection drawn).

## Verification

`tests/font_gsub_test.c`: lookup census by type; `👩‍💻` -> 1455
(technologist), `🇦🇺` -> 1626 (flagAustralia), each one glyph and not the
first scalar's glyph (606, 207); `👍🏽` -> 578, one glyph, **equal** to
thumbsUp alone (M: monochrome Noto Emoji ligates 1F44D 1F3FD back to
thumbsUp; it has no skin-tone artwork, so the brief's "differs from the
first scalar" cannot hold for this case; the tone swatch is still not drawn);
10-scalar kiss, family, England tag flag, non-RGI ZWJ tail; no-substitution
cases; the nested type 6 chain; truncated prefixes and seven garbled fields
rejected; a whole face with a garbled GSUB falls back; Latin and CJK images
identical with GSUB on/off and to golden hashes from the red run (pre-change
renderer); allocation guard around the cluster and render calls.
`fuzz/font_gsub_fuzz.c` seeds with the fixture's GSUB table.
