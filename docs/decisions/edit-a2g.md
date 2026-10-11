# edit-a2g: large-file/find review fixes

Cancelled find arguments have separate active and retiring owner slots. Restart unbinds and cancels the old receiver, retains both argument and snapshot until physical acknowledgement, and refuses another retirement when that slot is occupied. Maintenance reaps completed retirement. Explicit find admission allocates; typing does not.

Keep marks incomplete indexes dirty. Warm restart retains its existing job reservation, waits for physical retirement, obtains a fresh snapshot from the buffer recycling allocator, resets progress, and retries submission after exact index publication. Completed warming requests a correcting layout for the active buffer; inactive correction remains pending until activation.

Explicit nondefault file coordinates retain their requested line/column through COPY/mapped acquisition. The existing request setup waits for an owned worker line seek rather than clamping to density estimates or invoking global piece line queries. EOF alone clamps a requested line. A bounded resident target sample raises the estimated total to the proven target/following-line lower bound, so a low prefix density cannot hide the positioned viewport. The target seek can stop inside an otherwise uncounted chunk; its certified offset is sufficient. Partial indexes remain dirty for maintenance. The public editor_jump_line path and fully asynchronous request setup are not changed because editor.c is outside the supplied files.

Byte jumps never publish the backward probe edge as a logical start. A certified nearby start permits bounded cluster repair; otherwise an owned, cooperative snapshot worker finds the actual logical start and normalizes the cursor to a grapheme boundary. CRLF is a single cluster. The explicit helper waits for worker publication and physical completion before installing the result. This preserves source/argument lifetime but does not provide asynchronous deep navigation.

Unwrapped bracket visibility derives from prepared row byte ranges and bounded horizontal-cell decoding. Wrapped edit follow still needs an input.c/layout seam outside the supplied files.

Warm and find retain progress in their explicit-admission job objects. Each invocation has a byte bound and checks a wall deadline between bounded operations; individual file faults may still stall bulk I/O. Warm advances one line-index chunk at a time. Find uses the existing literal kernel in bounded overlapping windows, with nonoverlap ownership by match start and progress advanced past an accepted boundary-crossing match. Multi-byte window size guarantees every match fits the kernel offset capacity, eliminating the old full-source dense fallback. Adjacent immutable snapshot spans are borrowed directly; fragmented windows copy into reserved job scratch. Terminal phases seal results, release snapshots once, and work_continue on mailbox backpressure rather than sleeping in the lane. No new globals.

Resident bridges can bind a file-backed validation snapshot before admission. The bridge retains its sticky backing token through physical close, checks it around span acquisition/copy/sealing, and masks failed adoption/use while preserving the viewport. The generic unbound initialization contract is restricted to nonfaultable sources. Source callback/context lifetime remains caller-owned. This is detected backing-fault validation, not new worker identity polling; hosts still own source-change detection and must bind faultable snapshots.

No input.c, editor.c, layout, save/journal/startup, tab lifecycle or eviction hunks were changed. Exact estimated-total mutation accounting, lazy first-frame row repair, wrapped follow, fully deferred coordinates and findui match-plan paging remain open. Evidence and verification are in ../worker-reports/edit-a2g-s9.md.

## Continuation: default remaining probes

This continuation supersedes the earlier supplied-file exclusion for the
mutation/layout seams required by the assigned probes. The existing near-end
index/navigation imports in large.c remain intact.

Editor mutations retain the large buffer's exactness flag. Exact totals receive
exact newline deltas; density totals use checked subtraction/addition and the
empty-file sentinel / newline-per-byte ceiling. Layout carries an explicit
estimated-total flag in both wrap modes. Exact index adoption still requests
full layout, which imports the replacement total and clears its estimate flag.

Lazy unwrapped layout now runs the existing bounded following-row repair before
piece warming. Only certified index results become logical row starts. Wrapped
queries near the viewport can reuse its rebased logical anchor within the
existing layout byte budget; deletion across that anchor invalidates it. Edit
completion keeps the followed view's metadata or takes line/start metadata from
the returned visual row, avoiding another global piece-prefix query. This does
not redesign nonresident navigation or giant-line wrap queries.

Findui reserves unlinked, owner-readable/writable O_TMPFILE descriptors in /tmp
at panel setup. A worker builds a compressed ordinal/byte-ordered match plan;
equal-length, fixed-stride matches share a record, and irregular runs spill
through a prepaid buffer sized from FIND_POLL_UNITS and the record type. Window
requests binary-search byte endpoints; uncached navigation and replacement
pages binary-search ordinals. All retrieval is on workers and reads the plan,
without another source traversal. Generation changes invalidate the active plan;
a cancelled borrowing job prevents its producer slot from being reused until
physical completion. Disposal closes descriptors after all borrows retire.
Descriptor admission failure returns the existing memory-admission error.

Literal work preserves SIMD masks for single bytes, the core SIMD/Two-Way
matcher for case-sensitive non-word windows, and persistent folding/word KMP
otherwise. Bulk masks enter the compressed plan without enumerating discarded
dense matches. Regex work retains NFA lists, candidate/best endpoint, prefix KMP
and the shared retry meter in prepaid scratch. It yields at byte boundaries
while preserving the synchronous closure/tag ordering and whole-word/empty-match
policy. Search stages and sealed publication use work_continue; no new globals
or typing-path heap allocation were introduced.

The default tests include the former estimated-delete, lazy-row and sparse-window
probes, plus history replay, protected immutable-source replacement paging,
irregular folded literal/regex plans, and a bulk marker before matcher completion.
The protected source is unreadable after counting, providing a read-work oracle
independent of the scan hook. Evidence and remaining navigation/verification
limits: ../worker-reports/edit-a2g.md.
