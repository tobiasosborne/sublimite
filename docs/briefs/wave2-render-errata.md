Errata pass (advisor, 2026-11-25 21:55 CET): Wave 2 render frame contract (branch t3/wave2-render-spec, fb7776c).
SIGNED IN SUBSTANCE, including the PLAN §4.6 amendment text. The advisor reads only the diff. These errata ride with
the font revision, in the same Astra job, because R-C is font's.

ACCEPTED as written:
- the F1 record;
- a cell samples only its expected slot AND version, and a mismatch draws background at the same footprint (the ABA
  counter-example is right, and the check is needed even at depth one);
- the delivery FIFO with its watermark;
- the UI copies tile bytes once into the packet;
- the two independent axes (device, presentation), and LOST by quiescence with no synthetic T5 or T6;
- Q3–Q7, including the integer Q8 expression and the GL two-scanline resolve that gives parity with raster by
  construction;
- one off-UI owner of the raster X connection;
- the capacity table;
- the witness table;
- the retired donor structure.

R-A. Backend incidence lists. The text keeps, per runtime slot, a linked list of the retained cells that reference it,
     maintained on every cell patch, to find what to redraw when a tile arrives. But a tile arrives only when a glyph is
     seen for the first time or reassigned.
     The simplest alternative, to adopt or refute with (M):
     - no lists;
     - a packet that carries N > 0 tiles makes the backend scan its retained cells once, O(V), against the set of slots
       that packet installed, and mark the rows that name one;
     - a packet with N = 0 does no scan.
     That removes O(V + S) index storage, list maintenance on the typing path, and the stale-list defect class.
R-B. Resumable preparation. begin/step/accept, with a cursor that stops mid-row, re-acquires sources and must be
     cancelled before any mutation, is machinery for one case: a full frame at the largest geometry. By the document's
     own figures, one changed row is 7,200 bytes, and the full 360 x 300 grid is 2,167,200 bytes, about 238
     microseconds of copy (E), inside one 0.5 ms slice before validation.
     The simplest alternative, to adopt or refute with the validation cost added:
     - preparation is ONE bounded call in the accept step (render_submit: the offer plus the source in, the frame id
       out; a refusal has no side effects);
     - the tile grant is sized so the call fits the slice at ordinary geometry;
     - the cost at the largest admitted geometry is stated and measured.
     That deletes the preparation cursor and its invariant, render_step, render_abort, the frozen-prefix rule and the
     mid-row witnesses. Slicing returns only if a measured full frame on the target box misses the goal; the goal sheet
     is a goal, not a blocker (CLAUDE.md Law 1).
R-C (for the font revision; replaces F2 in docs/briefs/wave2-font-sendback.md). The version check changes what font
     retention is for. Reassigning a slot that a visible cell still names is now SAFE: that cell draws background, never
     another glyph. So holds are no longer a memory-safety mechanism, only a way to avoid a blank cell.
     The proposal, to adopt or refute:
     - font_async has no hold API at all: no hold_begin/keep/end, and no builder hold across MORE;
     - lookup stamps the entry with the paint serial;
     - when no free slot exists, one sweep over the cells of the live publication restamps what is on screen, and the
       oldest unstamped settled entry is evicted;
     - if every entry is on screen, the result is PRESSURE and the marker;
     - evicting a slot bumps the paint serial, so that any user is redecorated;
     - a candidate built across MORE that names an evicted version is caught by submit validation and redecorated.
RECORD: with R-A, R-B and R-C, restate the line budgets of render and font in the same passes.
