# edit-6di: tab titles reach the async glyph runtime

**Root cause.** `compose_chrome` (src/editor/paint.c) renders the tab strip into
`scratch`, a by-value copy of `e->grid`, taken before any title lookup. A cold
non-ASCII lookup (`editor_font_glyph`) appends a pending descriptor and rebinds
`e->grid` and `e->text_grid` only, so `scratch.glyph_count` stayed at its old
value. `tabs` `glyph_cell` then rejected the new slot as non-resident
(`slot < g->glyph_count` fails) and wrote a background-only cell. With no cell
holding the slot, completion (`editor_font_event`) found nothing to repaint on
the tab row, so the title stayed blank after adoption too. ASCII uses baked
slots that always lie below the stale count, hence "73j-" then blanks. Width
counting was already per cluster in cells (`names_copy`, src/tabs/tabs.c), not
per byte.

**Choice.** The chrome glyph callback also rebinds the scratch grid after each
lookup, so the strip sees exactly what the editor grids see. Its context is a
stack `chrome_glyphs {editor, scratch}` passed through a per-render copy of
`e->strip`; the stored `e->strip` (kept for hit testing) carries no callback, so
it never holds a pointer to the dead stack frame. Alternatives rejected: a
scratch pointer field in `editor` (wider diff in private.h, state that outlives
the call), or raising `scratch.glyph_count` to the descriptor capacity (would
admit unwritten descriptors). The tabs contract and module are unchanged.

Repaint uses the existing path unchanged: the title cell now holds the pending
slot (the `?` artwork), the callback already registers the tab row
(`editor_font_track(e, slot, 0)`), and adoption damages that row. No allocation
is added: `font_async_bind` writes four fields of a stack struct.
