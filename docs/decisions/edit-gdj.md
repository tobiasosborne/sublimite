# edit-gdj: selection across separate Shift chords

The checked-in Astra comparison review withdrew the selection-anchor finding after pixel checks and a minimal repeat. The worker brief and HANDOFF summary still describe the earlier finding.

Keep the existing command semantics: input translates each movement binding's `KEYS_ARG_EXTEND` into the view's Shift argument; `set_cursor` updates the anchor only for movement without Shift. Modifier releases do not change the selection. No persistent physical-Shift latch is needed across separately pressed chords.

Add editor-level exact-range regressions through injected modifier presses, movement, and releases. Run the wrapped and unwrapped configurations. Derive expected byte positions from literal ASCII fixture prefixes and viewport page motion from the configured text-row pair. Exercise a nonempty backward selection, reversal across its anchor, and resets after plain movement and text mutation. The existing counting hooks guard movement and typing through frame submission.

The worker report distinguishes the unchanged baseline from a deliberately injected anchor-reset fault used to validate the regression. No runtime change is justified by a baseline that already passes.

Evidence: [worker report](../worker-reports/edit-gdj.md).
