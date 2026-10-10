# edit-dnc: large-file editor overhead

The lineidx kernel is owned by edit-wvf and is unchanged here. Open/find
diagnostics use the existing trace ring with owner-derived IDs in its high
half. Those IDs describe operation phases rather than keystroke frames; the
bench emits phase deltas and saves dumps only after workers have joined.

Index completion is checked at submit, including after composition's bounded
minimap poll. This publishes exact state in the frame that adopts the last
range and defers an estimated grid if its gutter must be repainted. There is
no second content scan and no allocation on the typing path.

## File API gap for find

The file API exposes snapshot backing lifetime/fault state but cannot expose
an immutable, contiguous original when a snapshot still represents it exactly.
`file_snapshot_bytes` will certify every snapshot span against that retained
original before borrowing its bytes. Edited/private non-file snapshots use the
existing snapshot search. The snapshot retains the mapping throughout use.
Optional worker control is polled between metadata spans, before certification
and before returning it, so even a large original cannot postpone cancellation
until the whole geometry walk has finished. Faulted originals are refused;
fault checks continue between find windows and before accepting the aggregate.

The editor searches certified originals in bounded windows through the public
find API. Windows overlap by needle length minus one; starts belong to only
one window and the next window respects the previous accepted match's end.
Faults/cancellation discard the aggregate. Saturated window offsets fall back
to the existing whole-snapshot kernel. This retains exact counts, nonoverlap
semantics and fault invalidation without changing src/find.

## Mapped first viewport scope gap

Trace attribution places the sparse-file delay in eager piece attachment and
index setup in src/editor/buffers.c. That file is outside the supplied file
list. A narrow routing-scope clarification was requested; unless authorized,
the prefix-preview/adoption change is left to the coordinator. The file
mapping/prefetch interval is small in the measured trace and is unchanged.

## Loaded comparisons

Final source, BAT0 Not charging, load1 1.32 before log/find/sparse pairs and
1.50 after sparse (M)[AC]. Each preserved pre-fix invocation is followed
immediately by the changed binary; these are TRACK comparisons, not gate
certification. Values are p50/p99 in ms (M)[AC]; reference column is (G).

| Row | Before | After | Reference |
|---|---:|---:|---:|
| log G7 exact | 124.837 / 158.986 | 130.303 / 133.976 | 80 / 125 |
| sparse index | 1187.812 / 1215.241 | 1191.286 / 1196.859 | uncalibrated |
| log G6 find | 129.341 / 129.609 | 117.344 / 124.900 | 80 / 125 |
| sparse G5 viewport | 8.168 / 9.353 | 6.605 / 9.925 | 6 / 9 |

The same-source raw/snapshot find-kernel pair measured 124.437/124.568 and
142.903/150.881 ms (M)[AC], load1 5.35. The existing unchanged lineidx bench's
fresh mapping row measured 136.840/141.869 ms (M)[AC], load1 3.06. Prefaulted
flat/snapshot indexing paired on the editor pool measured 90.197/93.596 and
91.812/95.771 ms (M)[AC], load1 5.08. Different loads/page-fault states prevent
subtracting those percentiles as a precise editor overhead. The timing gates
remain unmet; the behavioural publication fix is supported by its red/green
regression and the mapped-source choice by both paired comparisons.
