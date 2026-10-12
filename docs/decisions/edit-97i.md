# edit-97i — reuse the tab for an open path

Resolve paths in editor open maintenance, before reserving a buffer or tab.
Compare canonical paths and current target device/inode identity, including
symlinks, relative spellings and hard links. Compare the tab's current target
rather than its original file snapshot: Save As can retain the original source
while the tab points elsewhere. Failed resolution falls back to the supplied
pathname, retaining absent-path buffer support. Untitled and stdin buffers
have no pathname and remain independent.

Use the existing tab selection and activation flow. Keep its ID, piece tree,
undo history, modified state and retained view. Matching examines live tabs;
retained closed tabs keep their existing reopen behavior. Ordinary CLI origin
coordinates preserve the view; non-origin coordinates request repositioning.
The IPC representation cannot distinguish an omitted coordinate from an
explicit origin, so both preserve the existing view.

Batch open requests reuse both live tabs and matching targets already prepared
within that request. Capacity and journal IDs count only distinct new buffers.
Prepare new buffers before publishing tabs, keeping failed acquisition from
switching an existing tab. Process accepted paths in request order so the last
path becomes active. An argument-free recovered launch keeps its session.
Fallible positioning of reused tabs is staged before publication too. Retained
view and scroll state are restored during preparation and published only after
every path succeeds. Source indexing may gain knowledge while preparing a
position, without committing a requested view. A regression rejects an
oversized column after an earlier new path and verifies that no tab or journal
ID was published.

Wait membership belongs to each request, with distinct tab IDs stored in the
editor's prepaid state. Its bound derives from IPC_MAX_PATHS plus the optional
stdin buffer (P: the public request representation). Closing a tab completes
every independent client waiting for that tab once its other members close;
repeated aliases do not add extra close obligations. Existing buffer wait_token
is retained as the primary association for compatibility with diagnostic
fixtures; close accounting uses request membership. Tab IDs survive buffer
replacement. Disconnected clients retain the existing bounded sweep behavior.

No typing-path allocation or new capacity, duration or performance budget is
introduced. Resolution and file acquisition remain explicit open maintenance.

The existing session save suite deliberately exercises legacy shared-target
tabs. Its fixture temporarily masks the original tab association while creating
its peer, then restores that association before editing. The original buffers,
controllers and save assertions remain intact; ordinary production opening has
no bypass for duplicate paths.

The existing CLI hand-off benchmark repeatedly invokes the same file. Its
structural assertion now expects the distinct pathname count (P: the sole
fixture path) and retains the explicit coordinate assertion. Sample counts,
timing aspirations and tracking policy remain unchanged.
