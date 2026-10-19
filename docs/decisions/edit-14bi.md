# edit-14bi: index geometry follows lazy replay

Pending COPY acquisition sizes its unbuilt line index for the original file.
Lazy replay can change the attached tree's length before snapshot binding.
Adoption must recreate that index when its length differs from the replayed
tree, retaining the existing MMAP recreation path. COPY acquisition with an
unchanged length keeps its existing index and reservation.

Keep `lineidx_bind_snapshot`'s length check: binding an index with different
geometry would publish incorrect metadata. Recreate on the existing allocating
open/adoption path; no typing-path allocation or error handling changes.

Reuse the retained-BASE lazy recovery fixture from edit-4jhi. Preserve its
same-length COPY case and add COPY insertion growth through checkpoint
relaunch, checking the acquired mode and adopted index length as well as text,
modified state, independent TARGET binding, and journal retirement.

(P) The sparse BASE length is `JOURNAL_STARTUP_SCAN_BYTES + 1` byte: the
existing eager-scan bound plus the smallest positive byte increment. The
replayed insertion adds `strlen("x")` bytes. The existing same-length case
deletes that many trailing zero bytes. `UINT64_MAX` forces COPY for every
representable fixture size; the existing MMAP threshold is the smallest
positive byte length. No new workload size or wait bound is introduced.
