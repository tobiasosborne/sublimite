# sublimité visual design workups, 2026-10-21

These are five workups for the main window, ordered from conservative (01) to experimental (05). Each one is drawn at 2880×1800 on its own cell grid, showing `src/piece/piece.c` lines 227–270 with syntax colours applied by hand. Only the parts the workup assigns to a pixel region (minimap, palette overlay) are drawn off the grid. They are proposals and do not describe the shipped editor. Brief: `build/briefs/design-workups-s10.md`. The research they build on is in `docs/design/state-of-design-2026.md` §7.

- **Today:** [`baseline.png`](baseline.png). This is the installed build (`~/.local/bin/sublimite`, 2026-10-20 03:43), opened on `src/piece/piece.c` on a private Xvfb `:96` at 2880×1800, 243 DPI, with a throwaway profile. The window was resized to fill the screen so it can be compared with the mocks. [`baseline-default-window.png`](baseline-default-window.png) shows the window at its default size of 1728×1260 (M: `xwininfo`). It shows lines 1–44 because that is where the file opens. The mocks show lines 227–270 because those lines contain real code.
- **All at once:** [`contact-sheet.png`](contact-sheet.png).
- **Per workup:** `NN-slug/dark.svg` and `dark.png`, plus `light.*` for 01 and 02. The SVGs and PNGs come from the same cairo drawing. Glyphs are embedded as outlines, so the SVGs render identically on any machine.

Evidence tags follow CLAUDE.md law 4: (P) physics or arithmetic, (M) measured on this box [AC], (E) estimate or design choice.

## Comparison

| # | Name | Thesis | What changes | Memory cost vs today (derivation) | Typing path | Rules out |
|---|---|---|---|---|---|---|
| 01 | Quiet tokens | §7 palette on today's exact layout | 29 token values in `theme.c` | **0 B** (P: same 29 tokens; new values only) | Zero: colours are resolved when cells are written, as they are today | Any hierarchy beyond colour; the `|` separator and block caret stay |
| 02 | Calm chrome | Chrome recedes, the page reads as one surface | Tokens (+2) and layout: 2-row tab bar, dot for unsaved, status row, wider gutter gap, minimap off, bar caret | **net −971,200 B** (P: −1,126,400 + 153,600 + 1,600), plus the backend's copy of the region also freed. The minimap region off saves 160×1760×4 = 1,126,400 B (P, size M). Chrome glyph halves add ≤ 153,600 B (P: 96 glyphs × 2 halves × 20×40 B). The caret slot pair adds 1,600 B (P: 2 × 800) | No allocation. +1 dirty row per keystroke for the status `Col` field (P: 2880×40 = 115,200 px), which can be coalesced to idle | Minimap by default; 2 text rows (44 → 42, P) |
| 03 | Type and air | Typography carries the design | 02 + Iosevka 34 px, line height 1.45 (49 px), hairlines baked into chrome glyphs | 02's figure + Iosevka Latin subset 33,260 B (M) + ASCII bake 95×17×49 = 79,135 B (P) + hairline variants ≤ 96×833 = 79,968 B (P); cell array **shrinks** 129,600 → 121,680 B (P) | Same as 02; slot size 833 B (P: 17×49) instead of 800 | 11 of 44 text rows (P: 33 vs 44, −25 %) unless line height drops to 1.30 |
| 04 | Rail | Files on a left rail, breadcrumb on top, no tab bar | 02 tokens + layout: 22-column rail, breadcrumb and status merged into row 0, 1-cell overview ruler in the pixel region | Region 20×1760×4 = 140,800 B instead of 1,126,400 B (P): **−985,600 B**; the rail reuses existing cells (P: the grid is full width already) | Zero allocation. The breadcrumb symbol comes from the hl worker through a mailbox, and its row is damaged only when the enclosing function changes | 17 text columns (P: 131 → 114); tab muscle memory |
| 05 | Floating | One new primitive buys a floating palette, plus UI type and a text minimap | 03 + Inter label tiles, rounded tab via coverage glyphs, syntax-texture minimap, **new `render_overlay` (RGBA8 premultiplied)** | Overlay 1456×812×4 = 4,729,088 B (P) reserved at init; minimap 110×1617×4 = 711,480 B (P), smaller than today's 1,126,400; Inter subset 57,348 B (M); label tiles ≈ 46 KB (E: about 860 px of labels ÷ 17 px ≈ 55 tiles × 833 B) | Editor typing: zero (overlay hidden, minimap refresh deferred to idle). Palette typing: opaque copy of changed rows only (body opaque, P) | Breaks the frozen `render.h` (new bead); a soft shadow bends §7 item 15 |

Implementability, in one line each:

- **01:** Haiku from this README alone. Touches `src/config/theme.c` only.
- **02:** Codex sol, or Haiku without the split-glyph tab labels. Touches `theme.c`, `config.h` (2 tokens), `src/tabs`, `src/editor/paint.c` (status row, gutter, active line, caret), `src/font` (chrome halves, caret slot) and the minimap default.
- **03:** Codex sol. Touches 02's modules plus `src/font` (new embedded subset, ASCII re-bake, line-height placement) and `src/config` (a `font.line_height` key; none exists today, M: `config.h:47-59`).
- **04:** Codex sol. Touches `src/editor` layout and paint, `src/tabs` (rail list instead of strip), `src/hl` (enclosing-symbol query), and `src/minimap` (replaced by an overview ruler) or a new small module.
- **05:** not from this README alone. It needs a `render.h` contract bead first (P2.0 is frozen), then `src/raster`, `src/gl`, `src/palette`, `src/minimap` and `src/font` (second face). Opus or Codex sol.

### The grid fact every workup lives with

Every row is the same height, `cell_h`. Margins exist only on the right and bottom (M: `src/raster/raster.c:336`, "paint the disjoint right/bottom margins"). So a chrome bar is a whole number of rows. The brief's "tab bar about 1.6× line height" therefore comes out as 1.0× or 2.0×. Workups 02–05 use 2.0×. Their labels are centred by splitting each chrome glyph image into an upper-row half and a lower-row half. That fits the contract: "glyph images are precomposed in layout". The cost is the atlas slots priced above, and it needs no new primitive. The same holds for the gutter: the "1.5-cell gap" becomes 2 cells.

### Default type size at 243 DPI (applies to 01, 02, 04)

Today's em is 10 pt × 243 px/in ÷ 72 pt/in = 33.75 → **34 px** (P). This is inferred, and it matches the measured 20×40 px cell (M, baseline.png). DejaVu Sans Mono's metrics give an advance of 1233/2048 × 34 = 20.5 → 20 px, an ascent of 1901/2048 × 34 = 31.6 → 32 and a descent of 483/2048 × 34 = 8.0 → 8, so the cell is 40 px (P). That is 4.0 mm per line on the glass (P: 40/243 × 25.4). The grid is 144×45 cells (P: 2880/20, 1800/40). §7 recommends a 30 px em. I kept 34 px so that the workups vary only one thing at a time; size is a separate setting.

---

## 01 Quiet tokens

**Idea.** This is what we get for free: the §7 palette applied to every one of the 29 tokens in `src/config/theme.c`, with no other change. It uses a near-black page, `#0D0E10` (not `#000`, to avoid OLED smear, §3). Text is warm-neutral, and there is one blue accent. The five syntax hues are low-chroma and at equal lightness, and comments are a neutral grey. The minimap keeps its current texture but drops to 1.7:1, so it stops being the loudest object on screen. Setting `tab.active.background` to the page colour makes the active tab join the page, at zero cost.

| Token (dark) | Hex | vs | Ratio (P) | Token (light) | Hex | vs | Ratio (P) |
|---|---|---|---|---|---|---|---|
| editor.background | `#0D0E10` | | | editor.background | `#FAFAF9` | | |
| editor.foreground | `#DCDEE2` | bg | 14.3:1 | editor.foreground | `#1F2227` | bg | 15.3:1 |
| chrome.background | `#151618` | bg | 1.1:1 | chrome.background | `#F2F2EF` | bg | 1.1:1 |
| chrome.foreground | `#9C9EA4` | chrome | 6.8:1 | chrome.foreground | `#5B5E63` | chrome | 5.8:1 |
| gutter.foreground | `#56585D` | bg | 2.7:1 | gutter.foreground | `#96989D` | bg | 2.8:1 |
| caret.background (accent) | `#6DB0F4` | bg | 8.4:1 | caret.background (accent) | `#1570D1` | bg | 4.7:1 |
| selection.background | `#273A4E` | text | 8.7:1 | selection.background | `#CCDEF1` | text | 11.6:1 |
| find match (bracket.background) | `#382F1F` | text | 9.8:1 | find match | `#F3E2C2` | text | 12.5:1 |
| syntax.comment | `#7D8086` | bg | 4.9:1 | syntax.comment | `#6B6E74` | bg | 4.9:1 |
| syntax.keyword / type | `#BAA4E2` | bg | 8.7:1 | syntax.keyword / type | `#7445B8` | bg | 6.1:1 |
| syntax.string | `#99CE9A` | bg | 10.7:1 | syntax.string | `#2F7339` | bg | 5.5:1 |
| syntax.number / constant | `#E9B082` | bg | 10.1:1 | syntax.number / constant | `#9C5414` | bg | 5.4:1 |
| syntax.function | `#95CDE9` | bg | 11.2:1 | syntax.function | `#1A6896` | bg | 5.8:1 |
| syntax.punctuation / operator | `#9C9EA4` | bg | 7.2:1 | syntax.punctuation / operator | `#5B5E63` | bg | 6.2:1 |
| minimap.foreground | `#3A3C41` | bg | 1.7:1 | minimap.foreground | `#C9CAC8` | bg | 1.6:1 |

Ratios use WCAG 2.2 relative luminance (P): linearise each sRGB channel, take L = 0.2126R + 0.7152G + 0.0722B, and compute (L1+0.05)/(L2+0.05). Selection is the accent at 27 % over the page (dark) or 20 % (light). The find fill is amber `#E6B55D` at 20 % (dark) or 35 % (light). Both mixes are computed in sRGB, as the coverage blend does. Every text pair is at least 4.5:1. The gutter (2.7–2.8:1) and the minimap are deliberately below that because they are not essential text. For comparison, today's pairs (P, from `theme.c`) are: text 11.5:1, comment 6.8:1 (comments are louder than keywords at 6.3:1), and minimap 4.8:1.

- **Typography:** unchanged (DejaVu Sans Mono, 34 px, 40 px line).
- **Chrome:** today's one-row tab bar. The `|` final separator (`src/tabs/tabs.h:111`) and the `*` modified marker stay, because tokens cannot remove them.
- **Minimap:** today's texture, quieter.
- **Caret:** today's block, now accent blue.
- **Selection and find:** as tabled.
- **Empty state:** the page colour with a gutter `1`.
- **Downside:** no hierarchy is added. The tab bar is still 40 px with its label near the top edge. There is still no status line, and the minimap viewport box is still wider than its ink. Tokens alone cannot fix any of that.

## 02 Calm chrome

**Idea.** Use 01's palette and spend layout code, but no new primitive, so the chrome recedes and the page reads as one surface.

- **Tab bar:** two rows (80 px = 2.0× line height, P). It sits in `#151618`. Labels are muted and centred with split glyph halves. The active tab is the page colour, with full-contrast text and no border, so it joins the code. An unsaved file shows a dot (●, 6 px radius, in the label colour) in place of `*`. There is no `|` separator, and close appears on hover only.
- **Gutter:** numbers right-aligned, dim `#56585D` (2.7:1). The caret line's number is `#BCBEC2` (10.0:1 on the active line). A 2-cell gap separates them from the code.
- **Active line:** the whole row filled `#121314` (text on it 13.8:1).
- **Caret:** a 2 px accent bar. It is precomposed into the glyph image under the caret, using two alternating reserved slots so a pending frame never samples a rewritten one (render.h T5 rule). Because a cell has one `fg`, the character under the caret also takes the accent colour. That is the honest price of a bar caret on this contract.
- **Status row:** one row in chrome grey, muted text, path on the left; `Ln, Col / Spaces / UTF-8 / LF / C` on the right.
- **Minimap:** off by default, which is the §7 item 13 option.

New tokens: `editor.line.background` `#121314` / `#F2F2EF` and `gutter.active.foreground` `#BCBEC2` / `#383B40`. In light, active number on line is 10.0:1 and muted on chrome is 5.8:1 (P).

- **Typography:** unchanged.
- **Find:** amber fills as in 01, with the current match in the selection colour.
- **Empty state:** the tab bar with one `untitled` tab, a status row naming the open-file and switch shortcuts, and a blank page.
- **Downside:** two text rows are lost (44 → 42, P), and the minimap is gone unless toggled. The status `Col` field changes on every keystroke, adding one dirty row (115,200 px, P) to the keystroke frame. If G1 shows it, it moves to the next idle frame.

## 03 Type and air

**Idea.** Let type and spacing carry the design. None of the fonts in the brief's list are installed: `fc-list` shows no JetBrains Mono, Geist Mono, IBM Plex Mono or Commit Mono (M). Iosevka *is* installed, by hand under `/usr/local/share/fonts/iosevka` (M), which suggests Tobias already likes it. So this workup proposes **Iosevka Regular** and renders with it faithfully. JetBrains Mono, §7's fallback, would need to be vendored, and it would be about 20 % wider (E: its 600/1000 advance vs Iosevka's 500/1000 (P)).

- **Metrics:** em 34 px (same as today, for a fair comparison). Advance 500/1000 × 34 = 17 px (P). Natural height round(0.965×34) + round(0.215×34) = 33 + 7 = 40 px (P). **Line height 1.45 × 34 = 49.3 → 49 px** (E: §7 item 2).
- **Grid:** 169×36 cells (P: ⌊2880/17⌋, ⌊1800/49⌋), with a 36 px bottom margin in the page colour. That gives 33 text rows and about 160 code columns (P).
- **Hairlines:** `#252629` (1.3:1) under the inactive tabs and above the status row. They are baked into the chrome glyph images as variants, because a 2 px row does not exist on a uniform grid.
- **Status row:** on the page colour, in comment grey (4.9:1), so the bottom edge disappears.
- **Gutter and caret:** as 02.
- **Minimap:** off.

**Glyph cache.** `FONT_ATLAS_PAGE_DIM` is 1024 and `FONT_ATLAS_MAX_PAGES` is 8 (M: `src/font/font.h:16-17`), so pages are 1 MiB each with an 8 MiB cap (P). At 17×49 a page holds at most 60 × 20 = 1,200 cell-sized slots, against 51 × 25 = 1,275 at today's 20×40 (P: ⌊1024/w⌋ × ⌊1024/h⌋, worst case before shelf packing of ink rectangles). The ASCII working set of 95 glyphs plus the chrome halves fits one page either way (P: 95 + 2×96 = 287 < 1,200), so the page count is unchanged.

**Binary.** The single Iosevka Regular face pulled out of its TTC is 9,933,996 B (M), which would nearly double the 10,663,904 B static binary (M, P: 93 %). Subset it to U+0020–007E, 00A0–017F, 2000–206F and five symbols, which gives **33,260 B** (M, fontTools). Keep DejaVu (343,140 B vendored, M) as the coverage fallback through `font.fallbacks`.

- **Empty state:** as 02.
- **Downside:** 1.45 costs a quarter of the visible lines (33 vs 44, P). At 1.30 (44 px) you get 37 rows. The face change also needs a new ASCII bake and a line-height placement path in `src/font`.

## 04 Rail

**Idea.** This is a structural change: no tab bar. Open files sit in a 22-column left rail on the chrome grey (`#151618`):

- an `OPEN` list first; the active file sits on the page colour with a 4 px accent bar, drawn as one coverage glyph (7.9:1 against chrome, P), and an unsaved file gets a dot;
- below that, the folders of open files, as a quiet tree.

Row 0 above the code merges the breadcrumb (`src › piece › piece.c › pool_grow() ●`, current file in full text, path in muted) with the status fields on the right in comment grey. So the window has exactly one chrome row and keeps today's **44 text rows** (P). The minimap becomes a 1-cell **overview ruler** in the existing pixel region: the viewport band, amber ticks for find matches, and a blue tick for the caret. That costs 140,800 B against today's 1,126,400 B (P). `Ctrl+P` (the existing palette) stays the fast switcher, and the rail can be hidden with a key, which gives the 22 rail columns back to code.

- **Palette:** 01 + 02's tokens.
- **Typography:** unchanged.
- **Gutter, caret, selection, find:** as 02.
- **Breadcrumb symbol:** comes from the tree-sitter hl worker as the enclosing function, through a mailbox, and damages row 0 only when it changes.
- **Empty state:** the rail shows `OPEN` empty, plus `Recent` with the last five files. The page shows nothing.
- **Downside:** 17 fewer code columns while the rail is shown (P: 131 → 114). The enclosing-symbol query is new work in `src/hl`. People used to tabs lose their spatial strip, although the palette covers switching. A "paper" light variant (warm greys, `#F7F5F0` page) fits this layout well but is not drawn.

## 05 Floating

**Idea.** Find out what one new primitive buys. 05 builds on 03's grid (Iosevka, 49 px lines) and adds four things.

1. **A proportional UI face for chrome.** Inter is installed (M); its subset is 57,348 B (M). It is drawn *inside the contract*: each label is rasterised once into cell-sized tiles, and the tiles are ordinary glyph images. That is about 55 tiles × 833 B ≈ 46 KB for the labels shown (E: label widths from the mock). Status digits use tabular figures that are pre-tiled once, so a keystroke never rasterises.
2. **A rounded active tab.** This is also inside the contract: a quarter-disc coverage glyph in the first and last cell of the tab, with fg the tab colour and bg the chrome colour, so 2 × 833 B (P).
3. **A syntax-texture minimap** in the existing opaque region: 1 px per character, 3 px per line, syntax colours at 35 % over the page, and an 8 % white viewport band with no border. Ink sits at about 2.0–2.3:1 (P: keyword `#4A425A` on the page is 2.0:1), so it never outshines the text. The region is 110 × (33 rows × 49) × 4 = 110×1617×4 = 711,480 B (P), smaller than today's.
4. **The new primitive:** `render_overlay`, a premultiplied RGBA8 region composited after cells and the opaque region. It is used for the palette: an opaque body `#18191C` (text 13.0:1, muted 6.6:1, selected row `#22252B`) with 18 px rounded corners and a soft 28 px shadow. The buffer is (1400+56) × (742+56+14) × 4 = **4,729,088 B** (P), reserved at init. A full blend touches 1,182,272 px (P). Only the shadow ring, 1,182,272 − 1400×742 = **143,472 px** (P), needs alpha; the body is an opaque copy. At an assumed 1–2 ns per blended pixel on the CPU raster path (E), the ring costs 0.15–0.3 ms per overlay frame (E). On GL it is one blended quad. The overlay is drawn only on its own damage: opening the palette, or a filter keystroke that changes rows. It is absent from editor typing.

Typography is as 03, with UI labels in Inter at 27 px (E: 8 pt × 243/72 = 27, P). Caret, gutter, selection and find are as 02. Empty state: as 02, with the palette opening centred by default.

**Downside:** `render.h` is frozen (P2.0). This needs a contract bead and changes to both backends, validation and tests. The shadow bends §7 item 15, which forbids drop shadows on the editor surface; the palette floats above it, but this is the first step down that road. Proportional chrome labels also add a second font to every chrome change.
