# edit-2ty: exact anchors for near-end navigation

The headless Astra sequence reproduces prefix replay in the view's cooperative line queries. The large-file index is complete and the worker has warmed the piece tree; indexing and mapped acquisition are not still pending. Ctrl+End must discover EOF's line membership and viewport start. Page Up requests a logical line before the old viewport. The old host seed supplies that viewport and, conditionally, its immediate predecessor. Neither seed covers a whole earlier page. Once older cache entries are evicted, the view scans from the original prefix again. The red trace and cursor oracle are in the worker report.

`large.c` installs certified nearby line starts before Ctrl+End and Page Up. The published total certifies EOF's logical line number; a backward scan finds its start and the preceding page. For a visible cursor, the layout's exact logical viewport start seeds a backward scan of the preceding page. Wrapped pages cannot cross more logical lines than visual rows, so the same conservative anchor applies to both wrap modes. Long pages and nonresident viewport metadata fall back to the published index. Existing valid view anchors are reused. Sources and cache entries belong to the active tree only; incomplete indexes, stale sources, previews and inactive buffers retain their existing behavior. Estimates are never installed as exact anchors.

Queued commands can become busy before a new maintenance turn. A pending view query is then answered from the exact index into the view's existing fixed result cache, preserving its target, tag and command phase. The private query tags are derived from `view/view.c`'s Q_BYTE and Q_LINE enum, explicitly documented beside the bridge. No input dispatch or paint change is needed. Normal view mutation invalidation continues to retire affected anchors/results.

Seeding is restricted to the document-end and previous-page intents. The nearby backward scan uses `VIEW_SCAN_BOUND` (G, existing view byte-work cap) and `VIEW_WINDOW` scratch storage, excluding the delimiter immediately before an existing line start. Missing local boundaries fall back to index lookup, which reads at most `LINEIDX_CHUNK` per query (G, existing lineidx API; the byte count derives from the target's indexed chunk). A post-read index validity check refuses fault-substituted content. It does not traverse the file prefix or wait for a worker. Storage was scanned by the index worker; the acceptance run also waits for the existing warm completion before simulating loaded-buffer interaction. There is no new pool, allocation, global, thread, deadline or file format.

Trace T1/T2 pairs using `editor_large_trace_frame(&buffer->lg)` delimit host seeding. Ordinary frame T0/T4 events remain the acceptance endpoints; their difference includes time spent between injection and dequeue, including seeding. Worker rings are dumped only after editor shutdown, so the dump is quiescent.

The requested bound is `EDITOR_SLICE_NS` (G): its existing value divided by the nanoseconds-per-millisecond unit conversion gives the UI slice in milliseconds. The test asserts the literal macro for each action; it does not replace that bound with a loaded-machine allowance. This is a null-backend headless measurement, not a displayed-frame or Sublime comparison. The tail newline oracle independently checks exact logical viewport membership in both nowrap and the file-default wrapped mode. Wrapped Home/End also check the resident row boundary and trailing affinity; every Page Up must move the cursor backward. The coordinator must run the display suites and the next Astra drive.

The performance assertion uses the repository's active release allocator-guard
capability to distinguish a production run from instrumented builds, whose
guard is intentionally inert. Sanitizer builds keep all functional and trace
endpoint assertions and print durations against the unchanged bound as TRACK.
There is no widened sanitizer deadline. The worker report discloses the timing
interpretation and the instrumented misses; no native or sanitizer timing pass
is inferred from release results.
