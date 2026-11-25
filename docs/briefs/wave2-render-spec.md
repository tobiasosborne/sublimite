Brief: the Wave 2 render + render_submit_prep FRAME CONTRACT (advisor, 2026-11-25). Astra, document only. It runs after
the viewport/layout revision and before the font revision. Output: docs/decisions/wave2-render-spec.md, on a wip branch
for the advisor's signature.

It covers the harvest's render and render_submit_prep sections (docs/decisions/wave2-L3-L4-harvest.md: Q3–Q9 as they
concern the frame) and the frame contract seen by layout (the signed-pending viewport/layout spec), font and the two
backends (raster XShm, GL 3.3). It uses the same required form as the earlier specs:
- one invariant per structure;
- the simplest alternative, adopted or refuted;
- the witness table;
- bounds in visible cells and the slice budget, with derivations (Law 4);
- the headers as text;
- the v1 structure not carried;
- a line budget.
The G1 budget is 1.0 ms p50 / 2 ms p99, with UI slices of at most 0.5 ms (perf/01-perf-target.md §0.2).

F1 (from the font send-back, decided HERE): the simplest alternative for Q4/Q9 was not evaluated: a backend-owned atlas
with tile deltas carried by the frame. Adopt it, or refute it with a counter-example.
- The proposal: the backend (the GL texture, or the raster worker's private RAM) keeps its own copy of every tile it
  has been sent.
- A frame carries, in its prepaid storage, the bytes of tiles that are new or reassigned since the last accepted frame.
- The UI copies them once at submit, up to a per-frame byte cap; cells whose tile did not fit stay pending for the next
  frame.
- The backend applies the deltas before drawing that frame.
The advisor's claims, unverified:
(a) No thread but the UI ever reads font_async's tiles. There is then no device pin and no T5 coupling in font, and
    pin_begin/image/abort/commit/retire and the (owner, token, epoch, frame) ledger are deleted.
(b) Per-frame UI work for glyphs is proportional to newly sent tiles, not to V cells and D images on every keystroke.
(c) After LOST or a backend restart, the UI resends from its own tiles, which stay the source of truth.
(d) The cost is a second copy of S tiles in the backend.
The font proposal as written walks all V cells, pins D images and repacks their bytes on every frame, including for a
one-character edit.
- If F1 is refuted, that per-frame walk needs its (M) cost at the largest admitted grid, against G1.
- If F1 is adopted: PLAN §4.6 names "page repack" in render_submit_prep, so draft the amendment for the advisor's
  signature.
