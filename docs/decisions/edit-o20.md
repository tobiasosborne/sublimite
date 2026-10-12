# edit-o20 — daily file, cancel and viewport commands

The editor-owned go-to-line widget becomes a prompt with an explicit kind.
Path storage derives from the existing IPC path capacity (P: the same path
must fit the buffer and tabs APIs, including its terminating NUL). Decimal
input keeps its preceding integer-width bound and preview/accept/cancel path.
Path input edits prepaid storage and never previews document mutations.

File commands queue editor-owned maintenance work. Subsequent input waits until
the containing frame has been submitted and retired. Setup then uses existing
buffer/tab APIs. Open rejects absent and unreadable/nonregular files instead
of adopting the buffer API's intentional absent-path untitled behavior.
Failures use the existing status notice without incrementing unsupported counts.

The named commands bypass find-field chord consumption using the resolved
binding action. Escape retains prompt-first, panel-second, selection-last
priority, with a single target per press (P: ordered state ownership).
Exit injects the existing close event; editor_step performs flush/hot exit.

Scroll uses the scroll policy's pixel displacement for an exact row (P:
row height multiplied by SCROLL_PIXEL_UNIT), reversing Sublime's amount sign.
Wrapped scrolling uses the existing prepared visual-row continuation with a
row displacement rather than quantizing through wheel notches. Neither path
invokes cursor follow.

Save As runs the existing atomic durable file writer after retirement, with a
snapshot of the current tree. It preserves the tree and undo log. A replacement
savectl is prepared with the new target identity before writing; successful
writing rebinds the tab title/path, controller, path watch and journal BASE,
then rotates a complete session checkpoint. The original file stays leased
for piece/undo snapshots. Mapped originals keep an independent source guard;
the target identity cannot be replaced by the old original's identity. COPY
originals no longer determine target-change notices. Future Ctrl+S uses the
replacement controller and the ordinary session save path.

This explicit file operation is synchronous maintenance, unlike Ctrl+S's
asynchronous acknowledgement. No new duration, capacity or performance budget
is introduced. The file is durable before checkpoint rotation. The preceding journal remains
on a crash, but a peer whose BASE named the overwritten target may conflict
and cause recovery to set it aside. Pre-write retention/PREPARE for the new
target and transactional association publication remain deeper journal work;
this bead does not introduce that protocol. Same-path Save As uses the ordinary
Ctrl+S transaction.

Path input follows UTF-8 grapheme boundaries and uses the prepaid font runtime
for rendering. Save As seeds the current path as selected text. An empty tab
set still admits the Open prompt. Decimal prompt layout remains byte-identical.

Saving over a target held by another tab uses the normal save-completion
policy: peer text stays retained and conservatively dirty, while peer BASE
records and controller identities follow the new inode before rotation. The
null-backend regression verifies that every live base remains valid. Reload
also accepts a path-rebound buffer without an original file handle; untitled
Save As owns a target watch rather than an acquired original.

Prompt palette values are reused from the preceding go-to-line widget. Zero
initializers and empty lengths, terminating-NUL offsets, boolean sentinels,
grapheme continuation offsets and byte-to-cell comparisons are structural (P),
not new capacities or counts. Test geometry reuses the preceding prompt fixture
and adds room for the existing panel/chrome. Allocation expectations derive
from the binding no-allocation rule (P).
