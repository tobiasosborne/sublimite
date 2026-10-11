# edit-6di worker report: non-ASCII tab titles painted blank

Branch `wt/edit-6di` from main bdf4e0d. Power: BAT0 "Not charging" (on AC).

## Root cause

`src/editor/paint.c:122-125` (pre-fix `compose_chrome`): the tab strip renders
into `scratch`, a by-value copy of `e->grid` taken before any title lookup.
The chrome callback `glyph` (paint.c:94) calls `editor_font_glyph`
(src/editor/fonts.c:9), which for a cold non-ASCII cluster appends a pending
descriptor and rebinds `e->grid`/`e->text_grid` via `font_async_bind`
(src/font/async.c:287), never the copy. `glyph_cell` (src/tabs/tabs.c:206) then
tests `slot < g->glyph_count` against the stale count, rejects the slot and
writes a background-only cell. Because no tab cell holds the slot,
`editor_font_event` (fonts.c:38-60) finds nothing to repaint on adoption, so
the title stays blank permanently (unless a later lookup happened to be warm on
a full compose). ASCII uses baked slots below the stale count: hence "73j-"
then blanks. Not a byte/cell mismatch: `names_copy` (tabs.c:12) already counts
per cluster in cells with wide CJK = 2; the new test pins that too.

## Fix

paint.c only: the callback context becomes a stack `chrome_glyphs {editor,
scratch}` and the callback rebinds `scratch` after each lookup. The render uses
a local copy of `e->strip` carrying the callback; `e->strip` (kept for
`tabs_strip_hit`) stores no callback, so no dangling stack pointer. Repaint goes
through the existing slot/row path: the cell now holds the pending slot (`?`
artwork) and the callback's `editor_font_track(e, slot, 0)` row registration
makes adoption damage row 0. No allocation added (bind writes four struct
fields). Tabs module and contract unchanged. See docs/decisions/edit-6di.md.

## Red (fix reverted, test present), release gcc

```
unicode_render_test: tab title col 21 for 'å' is blank (slot 4294967295)
unicode_render_test:415: FAIL cell->atlas_slot != RENDER_NO_SLOT && cell->atlas_slot < g->glyph_count
unicode_render_test:461: FAIL tab_title_unicode() == 0
exit 1
```

Test `tab_title_unicode` (tests/unicode_render_test.c): opens a buffer through
`editor_add_buffer(e, "build/73j-å文", ...)`, settles, asserts title_cells 7 /
clusters 6 for 9 bytes (P: 4 narrow + å narrow + 文 wide), ASCII cells on baked
slots, å and 文 cells carry real ink different from the pending `?`
(`bitmap()`), 文 is WIDE_LEFT/WIDE_RIGHT with a 2-cell image. Then, with the
bulk lane held, renames to a never-requested "ø語": the first frame must
already hold pending slots on the tab row; after release the tab row is
damaged, slots are unchanged, and both cells carry real ink.

## Green (all exit 0)

| suite | config | result |
|---|---|---|
| unicode_render_test | gcc release | 0 |
| unicode_render_test | clang ASan/UBSan | 0 |
| tabs_test | san | 0 |
| font_size_test | san | 0 |
| editor_test | san (display lock :99) | 0; typing rows mallocs=0, guard=ASan-inert |
| editor_test | gcc release (display lock) | 0; edit-g11 null typing mallocs=0; review 21 / P1-1 s6 `mallocs=0 guard=active` |
| editor_p4_test | san (display lock) | 0 |
| x11_font_size_test | san (display lock) | 0 |
| cli_test | san (display lock) | 0 |

Suites run one at a time; editor_test started at load1 11.2 (above the 10
threshold; noted), later suites at load1 2.8-6.9.

## Screenshot

`build/sublimite --new-instance "build/73j-å文"` on :99 (raster backend, fresh
XDG dirs), file content ASCII-only so no glyph is shared with the text area,
`import -window root`. Title reads `73j-å文` with the separator after it.

- crop (committed): docs/worker-reports/edit-6di-tab.png
- full frame: /tmp/claude-1000/-home-tobias-Projects-sublimite/8c381701-a117-40e9-8557-13b8432c1386/scratchpad/edit-6di-tab.png

## Out of scope (report only)

- A second launch of the same file with the reused XDG data dir, after its
  content had changed, exited with `sublimite: error -103; journal: .../session-*`
  : the Astra drive 2 "launch -103 on changed base file" blocker, reproduced
  here incidentally.
- `editor_font_glyph` (fonts.c:15) also registers `e->lay.row + e->tab_rows`
  when called from chrome, where `lay.row` is whatever row layout last
  touched: a spurious damage-check bit on completion (costs one row scan, no
  wrong output). Left as is.
- Commit trailer: the brief asked for `Claude Fable 5.1`; this worker ran on
  Opus 5.5 and CLAUDE.md law 10 forbids Fable subagents, so the commit carries
  the harness attribution `Claude Opus 5.5` instead.
