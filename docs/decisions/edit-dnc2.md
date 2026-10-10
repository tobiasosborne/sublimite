# edit-dnc2: mapped prefix viewport before attachment

Mapped files larger than the bounded file prefix reuse the copy-preview path:
the buffer owns a bounded preview tree and a distinct stable full tree, with
tabs and undo borrowing the full tree. Rendering binds the preview; input stays
queued until identity-checked adoption. Both full piece attachment and the
full-size index setup occur after the preview frame has submitted and retired.
The preview has a bounded unbuilt index; its gutter uses the full-file estimate
from prefix density. Small mapped files retain their prepared-open behavior.

The mapped `editor_add_buffer`/`editor_open` return contract stays intact. Open
drives the preview frame to retirement, waits for mapping acquisition if needed,
then adopts into the same full tree before returning. Immediate byte jumps,
full-source snapshots and the prepared warm snapshot therefore remain usable.
Full attachment can still take foreground maintenance time after the G5 endpoint;
this change does not claim sliced attachment or a shorter whole-call duration.
Explicit IPC positions beyond the prefix require acquisition and adoption before
their first correct positioned viewport. Ordinary copy opens remain deferred.
The existing `opening` owner remains set throughout the mapped preview-driving
phase. IPC admission is deferred until that phase returns, so a queued request
cannot replace the active buffer before the caller receives its full tree.
A socket-backed red/green regression checks subsequent IPC admission too.

Index submission precedes the remaining adoption reservations and warm-snapshot
preparation. Warm work still waits for exact index publication. All new
allocation remains on open/maintenance; queued typing uses existing reservations.
No globals, dependency-module changes or private dependency representations are
introduced.
The initial mapped build owns its geometry buffers separately from the later
prepaid reservation until that build retires, so this ordering temporarily
retains both sets of build storage. It adds no typing allocation.

## Remaining launch-order gap

Launching the full index before full piece metadata attachment is not implemented.
The public file API exposes a bounded prefix before attachment. It exposes the
complete original and its fault lifetime through a piece snapshot only after
`file_attach`, which itself constructs the full piece metadata. `file_source`
also requires attachment and exposes identity/validation, not mapped byte spans.
An independently opened mapping would evade the file module's acquisition
identity and SIGBUS lifetime contract, so it is not used.

The next slice needs an authorized file API for an immutable mapped-byte source
with retained backing/fault lifetime before attachment, and a corresponding
lineidx binding contract if a snapshot is unavailable. Those dependency files
are outside this slice's supplied list. The opt-in
`DNC2_REQUIRE_EARLY_INDEX=1 build/tests/editor_large_test` regression remains red
once mapping is ready but before attachment. The bead must remain open for this
requirement.

Loaded, back-to-back G5 observations and all red/green evidence are in
[the worker report](../worker-reports/edit-dnc2-s9.md). These measurements are
TRACK diagnostics, not timing-gate certification.

The preserved baseline and final sparse G5 row ran back to back on the loaded
box, BAT0 Not charging, load1 2.16/2.59 (M)[AC]. First-viewport p50/p99 is
8.520194/9.330951 ms before and 4.208069/4.725098 ms after (M)[AC], against
6/9 ms (G). Final trace attribution observes submission before attachment in
each sample. The behavioral regression, rather than a printed timing pass bit,
supports the routing choice.
