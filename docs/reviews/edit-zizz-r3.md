**BLOCKER: none. The round-2 blocker is closed.** [self_contained](/home/tobias/Projects/sublimite/.wt/edit-zizz-s27/src/editor/recovery.c:513) requires the **first** edit to delete exactly the entire recorded BASE at offset zero, or an empty BASE. Partial deletes and full-length deletes after earlier inserts cannot qualify. Groups preserve edit order; each validated BASE resets the log. Retained BASEs use their recorded size, and mapped buffers with edits remain unsaved. Dropped self-contained text survives in the preserved source independently of later target changes.

**MAJOR: none introduced by round 3.**

**MINOR — PAINT priority is only partly fixed.** [src/editor/recovery.c:821](/home/tobias/Projects/sublimite/.wt/edit-zizz-s27/src/editor/recovery.c:821)

The set-aside suffix now preserves PAINT. However, an over-capacity recovery containing both an unsaved PAINT-marked buffer and an installed retained-copy buffer whose target was rewritten still loses the warning: the rewritten-buffer loop replaces it with “unsaved edits were recovered.” The new guard then preserves that replacement, hiding the closed-tab explanation and reopening shortcut.

Fix: preserve the install-set PAINT warning when emitting rewritten-target notices too; cover this combination in `zizz_paint`.

Read-only syntax checks passed. Runtime fixtures were not run because they write files. No files modified.

LAND AFTER FIXES