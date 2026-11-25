Errata pass (advisor, 2026-11-25 21:25 CET): Wave 2 viewport + layout r2 (branch t3/wave2-viewport-layout-spec-r2,
25172c4). SIGNED IN SUBSTANCE; the advisor reads only the diff of this pass. Astra, document only, after the render spec.

ACCEPTED as written:
- the seam rule, with its CRLF and scalar handling;
- the determinism invariant;
- S2's ownership table;
- no suffix splice (S6);
- style runs (S7);
- the successor-table planning, the 3R - 1 row bound, and 6 decodes per unit;
- S9;
- the 2,050-line budget;
- S8's note, as is: undo and redo use BIND and follow the committed caret, with no Wave 1 API change. A peek is
  considered only if a measured undo frame misses the goal.
Astra's corrections to the advisor's claims (a)–(d) stand, recorded as the advisor's errors.

E1. S = 4,096 is NOT accepted; its premise is wrong. Nothing requires one display line to be scanned within one UI
    slice:
    - the design is resumable by its own text;
    - typing repairs from retained row certificates and does not rescan the display line;
    - at S = 4,096 the document's own follow bound is (2R - 1) x D = 159 x 8,196 = 1,303,164 bytes, which is 40.2 ms
      at the measured tail rate (1,303,164 x 30.87 ns), about 80 slices.
    So the slice does not bound S, and the price of the small S is paid in ordinary files: any line over 4 to 8 KiB
    folds, with wrap on or off (long prose paragraphs, log lines, JSON-lines records, minified code).
    The basis for S is the cold placement the user waits for, against the goal sheet's G5: open to a correct first
    viewport, at any line length, in 6 ms p50 / 9 ms p99 (/home/tobias/Projects/sublimite-v1-oracle/perf/01-perf-target.md
    §0.2, read-only; a goal, not an enforced limit).
    The advisor's candidate, to verify or correct:
    - an ordinary wrapped cold placement stages the target display line and at most one predecessor, so 2 x (2S + 4)
      bytes at 30.87 ns per byte, within 6 ms less the sheet's 1.0 ms frame;
    - so S <= 5,000,000 / (4 x 30.87) = 40,492, whose largest power of two is 32,768, giving D = 65,540;
    - check: 2 x 65,540 x 30.87 ns = 4.05 ms.
    Confirm how many display lines that placement really stages, and restate S, D and A29 accordingly. Scratch grows
    with D, so restate the storage row too.
E2. Clip mode stages whole display lines to get exact end columns, which is the Theta(R x S) term. The alternative, to
    adopt or refute:
    - a clip row is staged only up to its right clip edge;
    - `next` comes from the raw LF scan and grid arithmetic;
    - the full-line column is computed on demand for the one row that asks (End, the hscroll clamp).
    Name the consumers of `end_column` on a clip row. If there are none beyond those, the clip-mode follow bound drops to
    R rows of clip width plus raw scans.
E3. viewport_port.query is one function with eight parameters whose meaning depends on an enum. Replace it with one
    tagged request struct, or separate typed pointers.
E4. SELECT_LINE and physical-line Home use piece's line answers when they are exact (piece_lines_before,
    piece_lines_start), and scan only as the fallback. As written, a 10 GiB line is scanned raw even when the index
    already knows its bounds.
A29: signed in substance. It enters the amendments, with the S that E1 yields, after this pass.

THEN (advisor's order): Sol implements viewport from the signed text, on a wip branch: viewport.h, the motion walkers,
and their tests against a fake port. It depends only on piece and utf8. Astra reviews it; it lands after the advisor's
owner read. Layout waits for the render and font signatures, because of render_types and the paint callbacks.
