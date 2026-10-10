# edit-4w1.55: detected backing faults and cancelled file delivery

Scope: docs/reviews/P1-1.md §§2–3 only.

A retained file backing is the validity token. Its existing SIGBUS-service fault
counter changes before anonymous replacement pages become visible; healthy is
counter zero, and every detected recovery permanently invalidates that backing.
There is no revalidation/rebase operation: reload constructs fresh backing and
metadata. This is deliberately conservative, even if an edited snapshot no
longer uses every original byte.

`file_snapshot_backing` recognizes the file module's existing lifetime hooks.
The tiny `piece_snapshot_mapping` accessor exposes those immutable hooks while
the snapshot is retained, without exposing layout, changing piece algorithms,
adding a hook/global, or allocating. This accessor and the editor's index bind
are the necessary seams for snapshots to carry file validity into consumers.

Find's existing bounded polling checks the snapshot backing independently of
explicit cancellation inputs. Count and next APIs return FIND_CANCELLED and
clear results for a detected fault. The panel checks backing before worker
publication and UI adoption, cancels its leases, and clears previously adopted
count/cache/selection/highlights on service, state access, rendering and result
use. Replacement refuses further use of invalidated matches.

A line index binds its file backing before scanning. The editor binds both
small synchronous indexes and worker builds. The index retains only the backing
token, not the snapshot/tree/file, through metadata lifetime. Workers borrow
that token until physical completion. Fault detection cancels/unbinds the work
lease and masks all exact query/prefix/completion/seek results, including
already adopted metadata. Query wrappers check before and after indivisible
source scans. Invalidation is constant work rather than a chunk-table sweep;
physical cleanup follows the existing work completion acknowledgement.

The supplied main already removed readiness installation from getters after
the review cutoff. Preserve that behavior. Strengthen `file_msg_decode` at the
actual adoption boundary: validate its recorded file job handle, slot epoch,
and application generation against the live work lease before installing any
immutable record. Deferred decode after cancellation is rejected, as are slot
reuse and wrong generation. Decode still requires a live file; copies retained
past close are not supported. Getter polling does not consume messages.

No new mutable globals or per-file signal handlers. The existing single
`file_bus` registry owns signal recovery; retained backing references prevent
its slot/mapping from being retired while derived consumers still need it.
All added typing-path checks are nonallocating. Existing private find meter
test initializers merely gain a NULL snapshot field; their oracles are unchanged.

Back-to-back comparisons used the unmodified module benches, HEAD sources read
through `git show` into ignored build scratch, then the final implementation.
The box remained loaded and on AC. These are TRACK measurements, not gate
verdicts or proof of a speed improvement. Units below are ns; every number in
the table is (M)[AC]. Three timed samples per variant (M)[AC].

| Row | Before p50 | Before p99 | After p50 | After p99 |
|---|---:|---:|---:|---:|
| Find G6_ERROR, quick workload | 2051912 | 2441222 | 1968764 | 2177937 |
| Line index G7 warm fixture | 145999794 | 254933258 | 134668273 | 152503094 |
| Line index G7j partial worker seek + null viewport | 71045561 | 84340119 | 60703429 | 64402637 |

Find's quick row measures flat mapping input, so it does not isolate the cost
of checking a healthy retained snapshot token. Line-index benches use generic
sources. The comparisons guard general hot-loop regressions; functional file
snapshot and allocator fixtures establish the new token behavior. Line-index
runs stamped load1 between 4.48 and 4.68 (M)[AC]. No cold or display latency
claim is made.
