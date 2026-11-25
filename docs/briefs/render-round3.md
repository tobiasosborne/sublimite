Render ROUND 3 (bead v2-jj1; advisor, 2026-11-26). Sol, on its render thread, after the viewport mutant rerun and
BEFORE layout. Small and limited: Astra's round-2 re-check, docs/reviews/wave2-render-astra-r2.md, items F1–F3 and
the four mutants that survived the committed suite (her probes caught them; the suite must too). The size and item 9's
quiet-box figure are for the advisor's header read, not this round.
F1: the public null factory gets a working owner. It takes delivered packets, schedules its worker, and consumes
    accepted frames, so the null driver is a real consumer of the signed driver contract.
F2: the null owner's packet lifetime is joined to STOP_EPOCH. close must not return OK or release the arena while an
    owner still holds a packet; it returns WAIT with a wake, per RV-5.
F3: finish the backend evidence and scheduling contract in the headers, so a backend author never opens render.c.
Each item red first, from Astra's probe; each surviving mutant killed by a named test.
Rules: no git, no bd; decision file and report BEFORE the final lanes; nice make -j2; nothing outside the checkout. The
three Wave 2 brief lines apply.
