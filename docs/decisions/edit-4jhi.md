# edit-4jhi: lazy recovery separates BASE from TARGET

Scope: recovery-3 F6. Lazy replay acquired TARGET, then rejected its identity
because the operation offsets belonged to a different retained BASE inode.

Lazy preparation now acquires the recorded named BASE through the configured
file acquisition. The existing origin/BASE identity check still authorises
replay immediately after attachment. TARGET never supplies that authority.
After replay, a distinct TARGET becomes the buffer pathname and save
destination through the existing independent source/target binding.

The acquired retained generation stays ORIGINAL and the buffer's checkpoint
BASE. Its differences and the separate TARGET survive the replacement
checkpoint and another hot exit without copying the complete text on the UI
thread. The retained artifact stays referenced; this bead adds no deletion or
retirement policy. Source-journal retirement keeps its existing checkpoint
ordering.

Pending acquisition runs adoption with recovery publication suppressed, so
it cannot create a save controller for the retained pathname or an unpublished
buffer id. After installation/checkpoint, save setup captures TARGET's
identity separately while retaining the mapped BASE source guard. Because
savectl creation adopts the source identity, setup explicitly rebinds its
destination identity. A path watch tracks TARGET through the already
registered shared watch-service descriptor.

The sparse regression BASE is (P) `JOURNAL_STARTUP_SCAN_BYTES + 1` byte:
the existing eager bound plus the smallest positive byte increment. The
result is (P) 105,000,001 bytes, derived from the existing 105,000,000-byte
constant. A distinct TARGET forces full-BASE charging irrespective of mapping.
Marker lengths use `strlen`; the mapped insertion adds `strlen("x")`.
The COPY variant rewrites TARGET and pairs that insertion with deletion of
the last zero byte, so the total length stays unchanged. Both variants check
text, modified state, absence of set-aside, retained source identity,
destination identity, source retirement, and checkpoint relaunch.

Outside scope: COPY adoption with length-changing replay retains an index
sized for the unedited file. `lineidx_bind_snapshot` then rejects the edited
snapshot. This was encountered while extending the regression; it is reported
without a fix. The mapped regression retains the requested length-changing
case. No new size bound, duration, typing-path allocation, or error-to-fail
transition is introduced.
