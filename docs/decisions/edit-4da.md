# edit-4da: charge startup for the BASE bytes eager recovery touches

The f3i op log preserved ORIGINAL spans at install, but eager recovery still
materialised the complete BASE for its gap buffer, forced COPY acquisition,
compared complete texts for cleanliness, and counted complete-file newlines.
Lowering the cost estimate alone would therefore violate fm4.

For a discovered differences-only session whose configured acquisition maps
the BASE, recovery now retains only the validated op log and replayed length.
It attaches the mapped original before applying those ops, without submitting
an unreplayed preview. It uses the existing large-file prefix line estimate
and full VIEW state. Logged edits conservatively mark the tab dirty, as lazy
recovery already does; it does not compare the entire BASE to detect a net
no-op. An install/log failure returns an error and cannot fall back to copying
the entire BASE. The source is retired only after the replacement checkpoint
is durable, as before.

The read-only cost scan first checks the journal's own size. For each BASE it
charges the entire size when acquisition is COPY, the program deletes the
whole text, TARGET names another generation/path, or a legacy VIEW needs a
line scan from a nonzero scroll offset. Otherwise it charges FILE_PREFIX_MAX
plus the DELETE lengths. (P) FILE_PREFIX_MAX is the file module's existing
bounded acquisition header, not merely the shorter journal identity CRC
prefix; DELETE lengths bound ORIGINAL bytes copied by piece_delete. All sums
saturate rather than wrap. Relative TARGET paths are canonicalised for the
comparison with the named BASE.

Verified paths: file.c's mapped open reads its bounded prefix and maps the
remainder; file_attach installs that mapping through piece_init_mapped.
The op-only replay avoids recovery.c's bulk read/gap buffer; the mapped install
avoids same_bytes, piece_line_count and the full-VIEW byte-to-line fallback.
Index construction and warm scans remain ordinary asynchronous large-file
maintenance. COPY and whole-text recovery retain the existing flat path and
count the complete BASE, so oversized sources remain deferred.

JOURNAL_STARTUP_SCAN_BYTES and its fm4 derivation are unchanged. Launch timing
uses the same tracked G4a comparison as large_orphans; no new latency gate.
The sparse BASE regression covers whole-text deferral, configured COPY
deferral, eager dirty-tab recovery, and relaunch of the installed checkpoint.
The inherited whole-text GB journal and its set-aside copy also preserve holes.
Explicitly named journals keep their existing strict recovery path.

restore.c and the edit-601/edit-aq9 work are outside this bead.
