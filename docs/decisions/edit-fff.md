# edit-fff — retain undo safety reserves; report allocation guard miss

The M0 tabs-plus-caller-undo allocation guard is a real, load-independent miss. Its **1,000,000 B (G)** limit remains unchanged. Current census is **1,832,480 B (M)[AC]**; this decision grants no waiver and claims no memory-gate green.

The original [P4.4](P4.4.md) record measured **983,584 B (M)[AC]**. Replaying its tabs layout with original P1.5 undo reproduces that number. The committed P4.4 tree already includes P1.5d undo and measures **1,398,784 B (M)[AC]** under the same census, so the record described an older undo dependency than its final commit.

The principal growth is settled undo safety work: [P1.5d](P1.5d.md) reserves worst-case capture expansion of every admitted unknown insertion, adding **409,600 B (M)[AC]** for the small-tab fixture; [P1.5e](P1.5e.md) adds atomic-replay backups, another **409,600 B (M)[AC]**. Tab page rounding adds **4,096 B (M)[AC]**, and undo control blocks add **25,600 B (M)[AC]** through current main. Per-buffer editor slab recycling at 687492f changes none of the counted components. Slice-3 undo-history fields account for the **3,200 B (M)[AC]** increase since M0.

Decision: preserve the expansion and atomic rollback guarantees. Do not reduce virtual reserves without a separately reviewed reservation design. Do not change the bench to committed-page accounting merely to print PASS. The bead explicitly allows a gate-decision report instead of fixing this growth; the coordinator must adjudicate the allocation budget or assign a reserve redesign. This is a report of an unresolved gate, not a gate amendment.

Current empty undo mappings report **0 B (M)[AC]** committed; tabs plus undo control plus undo committed pages are **603,680 B (M)[AC]**. These figures explain the difference between address-space reserve and current owned-page charge, but do not replace the stronger named allocation census.

The permanent `tabs_test memory-gate` diagnostic returns nonzero on the unchanged reserve gate. Its default mode verifies census accounting and empty-history stats; the ordinary functional suite remains green. The original `tabs_bench` also remains nonzero for memory. No production code, new globals, typing allocations or benchmark limits were changed.

The adjacent timing reruns pass indent, UTF-8, trace and completed view replacement rows on unchanged production code. These M0 timing misses are treated as load artefacts, as required by the bead. Log document/selection rows are faster but remain incomplete: no full-command pass is inferred and no view code is changed. Historical view decisions do not establish a passing completeness baseline for those rows.

Full census breakdown, historical source reconstruction, pasted red/functional-green evidence, all adjacent bench transcripts, and build/sanitizer/fuzz results are in [the worker report](../worker-reports/edit-fff-s9.md).
