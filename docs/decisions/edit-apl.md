# edit-apl — production raster memory policy

## Bound and evidence

P4-3 section 9 and P2-1 section 10 describe the same default-capacity defect. The new census opens production defaults at target A (2880×1800 pixels, G), including the failed-EGL-to-raster path. It counts the real backend state, embedded worker palettes, snapshots/strips, SHM allocation and native pixmap payload once each. The editor arena contains backend state and is not added to state a second time in the combined startup total. Native payload is a lower bound (E), not a driver allocator census. ASan startup heap and the arena reservation provide additional whole-startup lower-bound evidence; thread stacks and native/driver metadata/transient peaks remain unmeasured.

The raster-share test uses 50,036,000 bytes (G, derived): A's 87,000,000-byte hard G10 cap (G), less the planned non-surface baseline of 16,228,000 bytes (E; 57,700,000 − 2×20,736,000), and one target-A handoff buffer of 20,736,000 bytes (E). This reserves the perf plan's other ownership rather than spending the entire G10 allowance on raster. The review does not prescribe an explicit numeric raster sub-cap; this derivation is a conservative necessary subsystem check, not a new binding gate or full G10 certification.

## Scope decision

The production defaults are in src/editor/open.c, not src/editor/backend.c. The bead's named source scope excludes open.c. No default change is made without explicit authorization. No raster-only silent capacity clamp is used: the frozen render interface reserves its advertised maximum extents at init and requires allocation-free resizing within them.

## Concrete options for Tobias

- Defaults-only change in open.c: use target-A geometry for the default maximum cell counts (360×120 at the production 8×15 font, G geometry), while retaining explicitly requested capacities. Two native surfaces then require 41,472,000 bytes (E) rather than 103,680,000 bytes (E). The explicit-capacity control measures snapshots/state at 873,520/266,064 bytes (M)[AC], with 20,736,000-byte SHM (M)[AC] and 20,736,000-byte native pixmap payload (E). The combined raster payload is 42,611,584 bytes (E), below the derived 50,036,000-byte allowance (G). Combined startup lower bounds are 47,753,343 bytes for direct raster and 47,760,256 bytes for failed-EGL fallback (E; measured heap/arena/SHM plus estimated native payload). These are lower bounds, not whole-editor G10 certification. Define whether a larger font also derives default cell maxima from the same pixel geometry. Existing target-A G1/G11 benches provide explicit maxima and preserve their viewport/endpoints.
- Preserve generous cell metadata but derive separate production surface limits from display/work-area before allocating the editor/backend, and ensure resize clamps against the same advertised limits. This needs the open/resize configuration boundary, outside the bead's named files.
- Shared pixmaps remove one separately owned native surface where the server supports them, but the current generous default still owns 51.84 MB of framebuffer (E) before snapshots/state, exceeding this derived share (G). Unsupported-server fallback, CPU/server clear ordering, resize margins and ownership must be designed and tested. Sharing alone is insufficient under the chosen handoff reserve.
- Worker-owned growth/retirement can preserve large-window support while limiting startup capacity, but changes the frozen init-only allocation/resize contract and must explicitly account for old/new surfaces at peak. This is a separate design bead, not a hidden exception.

The minimal reviewable defaults patch for the production 15-pixel font is:

```diff
-    e->max_rows = config->max_rows ? config->max_rows : 300;
+    e->max_rows = config->max_rows ? config->max_rows : 120;
```

The existing visible-row admission raises this maximum for larger explicit initial windows, and explicit max_rows remains unchanged. A larger-font policy would preferably derive both default cell maxima from the target pixel dimensions after font selection; that is a larger open.c edit. Neither patch is applied: the scope extension request received no answer before the report was completed. The coordinator/Tobias must authorize this file or assign a design change.

No gate exemption or whole-editor G10 PASS is granted. Red/green and verification evidence are in docs/worker-reports/edit-apl-s9.md.
