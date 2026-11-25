Send-back (advisor, 2026-11-25 20:05 CET): Wave 2 viewport + layout spec (branch t3/wave2-viewport-layout-spec,
09d8345). NOT SIGNED. One revision pass by Astra, serial after the font spec. Same file, same branch. Answer S1 first,
because S2–S5 depend on it. Keep the document within its present length by deleting what S1 removes.

ACCEPTED, do not reopen:
- the row contract: byte/end/next, seam affinity, the terminal row, a lone CR;
- Q10's key (buffer, text serial, geometry serial, publication) and the dependency-interval invariant with inspected
  gaps;
- the omission of `last`;
- complete-frame publication by directory swap (Q13), and the stale-hit rule;
- one emitter for clip and wrap;
- the witness table;
- no Wave 1 API change.

DECISION 1, the independent cold reader: ACCEPTED.
- One process-wide, single-thread pool, created by L5 at startup with the existing work API and shared across tabs.
- Reason: a mapped read can fault for unbounded time, WORK_FOREGROUND is CPU-only by its landed contract, and one bulk
  worker is not preemptible.
- Witness: a held bulk lane, a delayed read, cancel, and a late stale delivery.

DECISION 2, LIMIT: NOT ACCEPTED as proposed. A result that keeps the old picture and never resolves is a dead state
the user can reach with ordinary settings. It covers "no LF" and "an unending clipped tail", which is any long line with
wrap off, the default for code files. See S1; if S1 is adopted, LIMIT is deleted.

S1. THE SIMPLEST ALTERNATIVE WAS NOT EVALUATED: synthetic line starts on a byte grid. Adopt it or refute it with a
    counter-example.
    The rule:
    - Fix a constant S. A grid point g = k x S is a synthetic line start iff no LF lies in [g - S, g).
    - At a synthetic start, the decoder, segmentation, column, indent and wrap state reset exactly as at a hard start,
      at the first non-continuation byte within 3 of g. State the exact byte rule.
    - In both modes, a display line is the text between consecutive starts, hard or synthetic.
    The advisor's claims, to verify or break (they are the advisor's, and nobody has checked them yet):
    (a) Every display line is shorter than 2S bytes, and the nearest start before any byte t lies within 2S of t. It is
        found by one backward LF scan of at most 2S bytes.
    (b) The forward emitter decides the next synthetic start in O(1) from its own display-line start s: reset at g iff
        s <= g - S.
    (c) Every row is then an exact, deterministic function of (text, geometry), computable from at most 3S bytes around
        the target: 2S back to a start, and S more when rows above a start are needed.
    (d) Cost parity: the proposal already reads R x B bytes before the target and emits through them, so the grid costs
        no more than the proposal for S of that order.
    What it deletes, if it holds:
    - the approximate wrap phase and local-seed provenance identity;
    - LIMIT, its parked state and its L5 cue;
    - the long-line checkpoint ring, its worker and its stale-delivery rule;
    - the unbounded column and the unbounded `next` in clip mode;
    - the history dependence of RESTORE (the same anchor giving a different picture after a tab switch);
    - re-phasing while scrolling up through a long line.
    What it costs:
    - one mid-word row break per S bytes in lines longer than S;
    - a long line with wrap off is shown folded at the grid;
    - tab stops and a straddling cluster reset at a seam;
    - rows after a synthetic start depend on the absolute byte offset, so suffix reuse needs "no synthetic start in the
      reused suffix, or delta = 0 mod S".
    S is DERIVED (Law 4) from the measured break-scan rate and the slice budget, not chosen.
    If adopted: draft plan amendment A29 for the advisor's signature. PLAN §4.5 names long-line checkpoints, and the
    meaning of `approximate` and `line_start` changes.
    If refuted: LIMIT is still not accepted for ordinary text. A window with no provable grapheme boundary takes a
    scalar boundary as an approximate seed. To keep LIMIT even there, show text on screen that is wrong, or data damage.
S2. MOTION OWNERSHIP CONTRADICTS THE HEADERS.
    - The text says viewport "owns byte placement, affinity, relative scrolling and the decision to move the caret or
      extend a selection". Yet viewport.h has six functions, none of which can read text or rows, and layout_step
      returns the selection. So layout computes word motion, select-word, select-line, home/end and page: text logic in
      a row emitter.
    - Required: a table with one row per viewport_action, giving which module computes the target and the selection,
      from which inputs, and under which bound.
    - The requirement is only that viewport does not include layout. Viewport may include piece.h and utf8.h.
    - Delete viewport_request (eight arguments copied into a struct, with no failure path), or state what it validates.
S3. THE LINE BUDGET IS NOT DERIVED FROM THE DESIGN. 2,215 lines are allocated to a module whose header declares six
    small pure functions; the figure is v1's count minus two transfers. Redo the table from the revised headers, per
    function group.
S4. BOUNDS.
    (a) Strike O((RB + 1)^2). Derive the number of times a source byte can be decoded per intent. The advisor's
        derivation, to check: a byte past the last legal break is examined as lookahead, again as content of the next
        row, and at most once more when that row's width is smaller by the continuation indent (indent <= half the
        width, so the remainder then fits). That is at most 3, which makes CPU linear in the bytes admitted. Choose
        staged summaries or replay, one of them, and say which.
    (b) The general row bound 2RB + 1 would let R7/R8-class regressions pass in fuzz: for 80 rows and a 4,096-byte
        quantum it permits 2 x 80 x 4,096 + 1 = 655,361 completed rows for one follow. State it in R with derived
        constants.
    (c) B is used both as the slice quantum (a latency quantity) and as bytes of source per row (a content quantity;
        one cluster of an ordinary script can be 9 to 25 bytes for two cells). That is two parameters with two
        derivations. Under S1 the second one is S.
S5. CLIP MODE ON A LONG LINE is not specified. State what `hscroll_cols` means when the column at the target is
    unproved, what `next` is when the following LF is beyond the window, and what End and Home do. Give bounds, with
    v1's behaviour as evidence. S1 answers this; without S1 it needs its own section.
S6. Q12's simplest alternative is missing: no suffix splice at all, always emitting from the first touched dependency
    through the visible bottom. Give its cost as (M) from v1's layout_bench at the largest admitted geometry, against
    the G1 budget. Keep the hard-start suffix splice and the translation directory only if that number requires it.
S7. `style(ctx, byte, cell)` is one indirect call per cell on the typing path. Replace it with runs: the callback returns
    where the style ends. State that printable ASCII does not go through the per-cluster glyph callback. Both callback
    shapes stay provisional until the font and render specs are signed.
S8. Tie `layout_input.change` to the landed type: it is the bounding replace that edit_take_change yields, one per
    command, also for a multi-splice plan. Say so, and say what happens when more than one command commits between two
    layout_begin calls.
S9. Add the invariant "the same (text, geometry, anchor) gives the same rows, whatever came before", with its test:
    every incremental result is compared with a fresh emission, not with one "from the same provenance". Under S1 this
    is unconditional.
