# edit-457.10b: per-buffer large-file port

The single-buffer work on `wt/edit-457.10` is recreated on current main rather
than rebased. No dependency module or Makefile change is needed: the existing
source/test/bench globs discover the new files.

## Ownership and message routing

`editor_buffer` owns `editor_large`, its file/index/tree, and a borrowed pool
pointer used for teardown. The public large-file APIs operate on the active
buffer. Find/warm notifications match their work slot and epoch; find also
matches its application generation. Warm/find leases also bind the work mailbox receiver directly to their buffer,
so an IPC buffer prepared before tab installation can receive its own result.
Teardown removes bindings before cancelling/joining the current leases.
Save notifications match the buffer's file
pointer and save generation. An inactive buffer's job cannot publish into the
active buffer's state. Buffer destruction cancels and physically joins the
large jobs before releasing their arguments or the buffer arena. This also
covers failure rollback, retained-tab eviction and editor shutdown.

Workers receive immutable snapshots via work job arguments. Find results are
read only after mailbox publication. Full shared mailboxes cause cancellable
publication retries, rather than dropped completion notifications. Allocation
of jobs/snapshots happens during explicit open/find setup, outside typing.
No process globals are introduced.

## Open, layout and exact line numbers

Mapped buffers use the bounded prefix density for their initial line total;
copy/initial buffers retain exact counts. The eager mapped `piece_line_count`
call is removed from `editor_buffer_prepare`. Until the snapshot warm job has
finished, unwrapped layout uses its existing bounded sequential scanner instead
of reseeding every row with opaque exact piece queries. Once warm, main's
existing independently seeded clipped-row behavior is retained.

The index module owns its bound mailbox receiver. A per-step hook therefore
checks all buffers for exact publication. It updates each buffer's line total,
and corrects the active estimated viewport using that same buffer's index.
Inactive estimated views are corrected after activation. A busy view defers
viewport correction but retains the full-layout request immediately, so the
new gutter width is not lost. This performs no libc allocation.

The imported bounded byte jump retains its synthetic anchor for a line whose
start lies beyond the backward probe. Deep single-line goto is not expanded
in this port. Prefix estimates have an API status flag; no new gutter marker
or find/save key binding is added.

## Cooperative index ordering

The old branch's non-yielding index worker finished before the queued warm job
could start. Main's index continuations would instead let that warm job overtake
publication. A regression now requires the mapped buffer's warm snapshot to be
reserved at open with no warm lease yet. Index publication enqueues that already
prepared job through work_submit, without allocating on the typing path. This
restores the imported index-before-warm order. A full work queue retains the
reservation and retries on later steps; close releases an unsubmitted snapshot.
The warm job is not promoted onto the foreground lane.

## Benchmark endpoint corrections

Main's index builder now cooperatively queues continuations. Warm, find and
save may all finish before indexing does, so the combined row also waits for
`lines_exact`. “Three workers” means three job types queued on the existing
single bulk lane, as required by perf/01-perf-target.md section 2.15.

The imported bench obtains dimensions from the 30 px ASCII cell, while main's
editor defaults to 15 px. It now explicitly selects 30 px and asserts the
resulting 2880 x 1800 viewport. It checks the submitted prefix glyphs (including
inverse control-byte glyphs in the sparse fixture) before counting G5 success.
Test fixtures and the save bench's disposable copy live under build; the corpus
is read-only. Typing rows retain line 1000 because the separate edit-czn
far-line layout problem remains. All bench results are loaded (M)[AC] TRACK,
not acceptance verdicts. Evidence and missing acceptance items are in
../worker-reports/edit-457.10b-s9.md.

## Recorded measurements

Final source, loaded (M)[AC], BAT0 Charging, load1 31.18 at bench start;
null backend, TRACK. Every table entry is p50/p99 in ms; reference columns are
(G). No gate verdict is claimed. Historical s8 is not paired with this source.

| Row | s8 (M)[AC] | Final port (M)[AC] | Reference (G) |
|---|---:|---:|---:|
| log G5 | 2.52 / 5.93 | 12.899 / 13.629 | 6 / 9 |
| sparse G5 | 8.53 / 9.75 | 15.487 / 16.744 | 6 / 9 |
| log G7 | 238 / 267 | 918.685 / 1231.721 | 80 / 125 |
| sparse index, tracked | 2588 / 5437 | 3242.762 / 4096.970 | none |
| log G6, find alone | 197 / 253 | 712.848 / 1074.815 | 80 / 125 |
| log arrows, index active | 0.050 / 0.071 | 0.397 / 2.381 | 1 / 2 |
| sparse arrows, index active | 0.052 / 0.098 | 0.230 / 0.420 | 1 / 2 |
| arrows, find active | 0.066 / 0.090 | 0.383 / 2.736 | 1 / 2 |
| typing line 1000, find active | 0.288 / 0.575 | 0.331 / 6.904 | 1 / 2 |
| arrows, index/find/save queued | 0.051 / 0.134 | 0.179 / 0.255 | 1 / 2 |
| typing line 1000, find/save | 0.288 / 1.044 | 0.467 / 14.732 | 1 / 2 |

The scheduling variants ran back to back: eager warm G7 485.215/491.677 ms
(M)[AC], load1 6.91; deferred warm 275.097/304.165 ms (M)[AC], load1 9.49.
The ordering choice is also enforced by the pre-publication lease test; these
loaded timings do not certify G7. Final typing allocations: zero (M)[AC],
against zero (G). Full raw outputs and per-finding RED/GREEN are in the report.

Final forced make all/check exited zero; 60 sanitizer binaries and replay CLI
passed (M)[AC], leaks disabled. Final editor fuzz: 2870 executions in 61 s
(M)[AC], clean, against 60 s (G). No runtime source change after scope cutoff.
