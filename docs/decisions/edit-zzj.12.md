# edit-zzj.12 — raster caret idle path

The caret frame uses the normal render adapter and genuine X fence, matching
PresentComplete and pixmap Idle acknowledgements. It does not fabricate T5/T6
or release the retained pixmap early. The general full-frame strip/fence worker
and all shutdown/fence ownership code remain unchanged for edit-2vs.

A prepared visibility-only editor frame keeps its existing paint/decorations,
relayouts just the cursor row and composes only that row. It does not refill the
minimap or redraw the tab strip. Selection and wide cursor pairs are supported.
A visibility change with no changed viewport pixels submits no frame.

The editor explicitly hints caret-only damage to raster. This is necessary:
ordinary row damage can include atlas pixel changes in place even when page and
glyph descriptors compare equal. Without the hint, every marked row still uses
the original raster pipeline. With the hint, dimensions/descriptors and retained
cell identities must match, and at most two cells may change. Larger cell areas
keep the worker path to bound UI work. The existing SSE2 kernel draws the one-
or two-cell scene inline; XShm uploads just those pixels. A transient XFixes
update region limits the Present copy to those cells and is destroyed in server
request order after Present snapshots it. XFixes version support is checked
at initialization. No process globals or typing-path heap storage were added.

Inline caret frames submit no strip or completion jobs. The UI drains their
private X connection without blocking or a timeout. If new damage is waiting
for the previous pixmap, the completion fd is armed in the editor's existing
epoll set, which the platform already polls. A clean frame does not need an
additional completion-only wake: its acknowledgements are consumed on the next
blink/input turn. At idle/unfocus there is no completion timer. Pending genuine
acknowledgements remain retained until a subsequent turn or shutdown; observation
of blink T6 may therefore be delayed, while the actual scheduled presentation
is unaffected. Input that needs the pixmap arms fd-driven completion handling.

The editor benchmark's normal settle endpoint is pending damage submitted
(T4), not physical Present completion. A pure regression explicitly keeps a
submitted backend active with delayed Present and requires settling. Rows
whose endpoint is T5 retain an explicit ready-settle helper; G11 setup and unfocus use submitted-damage settling; initial setup completion
is excluded from the blink window as the loop naturally observes it. The G11 fixture now
selects the font used by its geometry calculation and asserts its actual
2880 by 1800 viewport. The unrelated inherited G1 geometry claim is unchanged.

G11 structural misses are reported even in TRACK mode; timing verdicts remain
visible. Measurements, pasted red/green and remaining gate acceptance are in
[the worker report](../worker-reports/edit-zzj.12-s9.md). No real-display test
was performed. No single loaded-box timing is an acceptance verdict.
