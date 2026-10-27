# edit-cxho.24 — design hill-climb round 2: quiet palette, tabs without characters

2026-10-27, Opus 5.5 coordinator in the main session, unattended (Tobias away, with permission to proceed). The work sits on branch `wt/design-s25`. **Not landed: it waits on Tobias's eye** (`docs/design/council.md` acceptance).

## Why this candidate

Round 1 graded today's build, in absolute mode, against Tobias's standard (`docs/design/rubric.md`). The overall mean was 1.73 out of 5 (M, three members). All three members ranked the same gaps:

1. characters doing the work of UI (`|`, `*`, `[aa]`, block caret, prompt labels);
2. no margin system;
3. default type and an unedited programmer palette.

The type gap (C1, mean 2.67) was the smallest. Of the agreed gaps, the 0 B ones were taken first: the palette, the tab glyphs, the minimap's colour sources, and the notice row.

## What changed

- **`src/config/theme.c`:** all 28 tokens in both themes now come from `tools/mkpalette.py` (OKLCH ladder; every line carries its WCAG ratio).
  - One neutral tint for both themes, taken from the window manager's title bar `#222226` (M, edit-29y2). The dark page stays exactly `#222226`, so title bar, tab strip and page are one surface; the active tab is marked by label tone.
  - The ladder: text 12.3:1 / 16.3:1, comment 5.7:1 / 5.4:1 (was 2.8:1 in light), gutter 3.1:1 / 2.6:1, minimap 1.6:1 / 1.5:1 (dark / light, P).
  - Five syntax hues at one lightness and low chroma; operators and punctuation are neutral.
  - One accent (`caret.background`); the selection is a tint of it.
- **`src/tabs/tabs.c`:** no `|` separator (the cell stays blank); unsaved is shown by U+2022 instead of `*`.
- **`src/editor/paint.c`:**
  - The minimap's ink is now `minimap.foreground` (it was `gutter.foreground`, which left the map louder than its own ladder rung).
  - The minimap viewport is a neutral wash of `editor.foreground` (it was the accent).
  - Find-match cells use `e->find_bg`, resolved from `bracket.background` when the theme applies. It was a hard-coded `0x53667e`, which the light theme also got.
- **`src/editor/session.c`:** the notice row uses text on `chrome.background` instead of a hard-coded blue band.

## Cost line (perf never suffers)

- **Memory:** +4 B, one `uint32_t find_bg` per editor (P). Everything else is constant values.
- **Paint and typing path:**
  - Find-match cells read a field instead of a constant. The colour is resolved once per theme application, never per cell, so there are no string lookups in the paint loop.
  - The minimap makes the same three theme lookups per map paint as before.
  - The notice row adds two lookups per notice write, not per keystroke.
  - No allocation and no new dirty pixels per keystroke (P: the same cells are written).

## Evidence

**Red, then green:**

- `tests/config_theme_test.c` (new): 25 failures on today's palette, green after.
- `tests/tabs_test.c`: red at :227, then green.
- `tests/editor_test.c` minimap helpers: red at :428, :645 and :7788, then green on the new sources.

**Updated, not red-first:**

- `tests/editor_p4_test.c` pinned `0x493038`. It now reads `error.background` from the theme (the first full check found it).
- `tests/unicode_render_test.c:261` counted the tab row's U+2022 unsaved mark as expected text damage. It already masked row 0 out of the observed damage, and now masks it on both sides (the second full check found it). The allocation guard in the same loop held: the `•` costs no heap call on the typing path.

**Round 2 council (blind A/B, seed 25; report: `docs/reviews/design-council-2026-10-27.md`):**

- Mean 1.83 → 2.43 (M).
- No criterion regressed for any member.
- Gains: C5 tabs +1.67, C3 colour +1.33, C2 ladder +1, C7 minimap +1, C9 states +0.67, C4 chrome +0.33.
- Unchanged: C1 type, C6 gutter, C8 panels, C10 empty.
- Every member preferred B in every scene they named.
- Control noise was 1 of 3. The control pair shared its scene with the crops; the runner now uses `03-find` as the control.

## Not done (next candidates, in the council's order)

1. **Panels** (C8): drop the `Find:` / `Command:` labels, show toggles by tone instead of brackets, and inset the line on the page grid (`src/findui/paint.c`; hit columns at :124).
2. **Margin system** (C4, C6): one inset unit for the gutter, tab labels, panels and minimap; current line number lifted one step.
3. **Bar caret** (C9): needs a render contract primitive in raster, GL and D3D11. That is its own bead, priced, never on the typing path's budget.
4. **Type** (C1): an OFL face of real quality with deliberate leading. Iosevka is installed in `~/.local/share/fonts`. The candidate face needs Tobias's word, then an embed plus a re-bake of the ASCII atlases.

The old palette is still in git (9dcdf6f `src/config/theme.c`). The bead asked to keep it selectable by name; `config_theme` has only dark and light, so that would need a parser change, which was not done.
