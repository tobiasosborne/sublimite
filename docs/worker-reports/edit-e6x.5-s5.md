Kept the sound backend, fixed upload rollback and T5 ordering, and added bounded SSE2 color caching.

- `make check`: **23 tests passed**
- **300-second fuzz clean**; full benchmark completed
- Paired kernel median: **0.747× [bat] / 0.835× [AC]** (M), loaded TRACK
- **Zero input-to-submit allocations**; seven exempt libxcb allocations/frame

G3/G3z still need the coordinator’s quiet [AC] run. Minimap remains excluded.

[Full report, RED/GREEN runs and benchmark output](/home/tobias/Projects/editor/.wt/edit-e6x.5/docs/decisions/P2.5.md) · [Handoff](/home/tobias/Projects/editor/.wt/edit-e6x.5/src/raster/STATUS.md)