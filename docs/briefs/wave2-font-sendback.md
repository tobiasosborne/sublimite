Send-back (advisor, 2026-11-25 20:55 CET): Wave 2 font spec (branch t3/wave2-font-spec, 08975d5). NOT SIGNED as a whole.
The font revision runs AFTER the render frame-contract spec, because Q4 and Q9 are the frame contract seen from the
font side. Codex order: the viewport/layout revision (running), then the render frame contract including F1
(docs/briefs/wave2-render-spec.md), then this font revision (F2–F8, plus whatever F1 decides).

ACCEPTED, do not reopen:
- validation before stb, and rollback;
- bootstrap over the two bakes, with byte-digest identity;
- file-over-family selection and the ordered fallback chain;
- the glyph key (generation, exact bytes, span, presentation), with the A16 rule taken from the unprojected cluster;
- EMPTY has no slot and keeps its width and hits (R5);
- one fixed double-cell tile per entry;
- the entry state table;
- the two-bank scale transaction;
- discovery through the fc-match helper, as in v1;
- the witness table.

F1 is decided in the render spec; see docs/briefs/wave2-render-spec.md.

F2. (SUPERSEDED by R-C in docs/briefs/wave2-render-errata.md: no hold API at all.) View hold. The proposal calls hold_keep once per reusable cell on every publication. The alternative:
    - lookup stamps the entry with the builder's serial, in O(1);
    - only when no free or evictable slot exists does one sweep over the cells of the live publications mark what is in
      use, after which unmarked settled entries are evictable.
    That means no per-cell call on a frame without pressure, and O(V) once under pressure. Adopt or refute. If adopted,
    hold_begin/keep/end shrink accordingly.
F3. Lookup index. A balanced tree costs K x (1 + log(S + 1)) byte comparisons, plus its own rebalancing code. A table
    with a fixed probe limit P, comparing bytes only where the stored length and full hash match, has a deterministic
    worst case of P x K with no probability argument; exhausting P on insert is PRESSURE. For any P below log(S + 1)
    that is the smaller worst case, and one probe in the ordinary case. Adopt or refute.
F4. K = 16,384 bytes of key storage per entry is STRUCK. It has no derivation, and at S entries it is many times the
    tile storage it indexes (a double cell of 10 x 20 pixels is 2 x 10 x 20 = 400 bytes). v1 does the same at
    async.c:113, so this is v1 structure carried in.
    - Derive K from what composition can draw. A candidate to verify: the stream-safe bound of 30 non-starters
      (UAX #15) gives 4 + 30 x 4 = 124 bytes at 4 bytes per scalar. Check it against the longest emoji ZWJ sequence in
      the pinned tables.
    - Longer clusters settle LIMITED without being stored.
F5. Composition lane. Glyph composition on WORK_BULK means a glyph waits behind blocking I/O. That is v1 parity
    (async.c:425), and it is still wrong text on screen: every non-ASCII letter of an ordinary file shows the marker
    while a cold open of a large file holds the bulk worker. Composition is CPU-only over loaded bytes, which is
    WORK_FOREGROUND's landed contract; discovery and reads stay on BULK. Adopt, or state what forbids it.
F6. Resumable composition. Credits inside outline expansion and scan conversion mean rewriting the donor rasteriser to
    be resumable. Instead, the unit of work is one key, atomic, with its bound taken from the validator's caps and the
    tile area, and cancellation is checked between keys. font_compose_begin/step/end and font_cursor collapse to one
    call, unless an (M) run shows that one admitted key can exceed work.h's 5 ms polling rule.
F7. Pending art. A pending cell is background-only at its settled width. The marker is for settled states only
    (MISSING, LIMITED, PRESSURE). Reason: "?" is a real character, and the first paint of non-Latin text would be a
    screen of them. This is the advisor's ruling on taste, and Tobias may overrule it. State what v1 draws for pending.
F8. Restate the lookup, storage and submit rows of the bounds table after F1–F4. The line budget follows the revised
    headers.
