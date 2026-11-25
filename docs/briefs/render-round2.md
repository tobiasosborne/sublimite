Render ROUND 2 (bead v2-jj1; the advisor's rulings on Astra's review, 2026-11-25). Sol, on its render thread, after
viewport round 4. Input: docs/reviews/wave2-render-astra.md (nine MUST-FIX). The errata are RV-1 to RV-5 at the end of
docs/decisions/wave2-render-spec.md.

1. The nine MUST-FIX, each red first from Astra's input:
   - backend ops on the UI thread: fixed by RV-2 (no PUMP; consume and poll are called by the driver's owner);
   - the binding lost before the first accepted full frame: fixed by RV-3 (bind stages a candidate);
   - device loss between frames ignored;
   - a NULL wake accepted at setup;
   - the native type: plat_native_binding *, the signed plat type;
   - the packed cell offsets described in the driver header;
   - reject reasons reportable through render_stats;
   - frame ids monotone for the service's life (RV-4);
   - the full-frame cost (RV-1).
   Also kill the two surviving mutants (bypassed sheet validation, removed wide-right coverage extent check), and
   delete RELEASED (RV-5). Apply Q4 and Q5 as ruled.
2. RV-1: release-path submit validates STRUCTURE only, fused with the copy, and checks the paint serial and ancestry
   once. The full per-cell directory check runs only in the sanitizer lane and under ASSERT builds.
3. SIZE (1,355 against 710 is not accepted). Three structural changes, then a recount by function group:
   (a) ONE retained-scene implementation in the common service (install, patch, the R-A scan, row damage), which every
       driver consumes; null becomes a thin consumer;
   (b) test observers (the per-cell sample mirror, borrowed sample and region pointers, the per-scan stopwatch) move
       out of the production headers into a test header, as file_test.h does;
   (c) one evidence path (not render_evidence_event plus render_publish_evidence), and one description of the five
       backing arrays.
4. BENCH, QUIET BOX: load1 below 2 and no lane running, or the figure is labelled non-probative. The coordinator
   gives a quiet window and serialises lanes around it. Measure full submit at the largest admission (sheet and
   runtime) and one changed row, per step and per operation, stamped with power and load.
Rules: no git, no bd; red first; decision file and report BEFORE the final lanes; nice make -j2; nothing outside the
checkout. The three Wave 2 brief lines apply.
