# edit-4w1.56 — foreground work isolation (session 8)

Assigned review: `docs/reviews/P1-1.md` §4. The inherited session-6/session-7
implementation already introduced the optional lane and continuation API. This
session reproduced the old FIFO failure before completing that work.

## Execution and ownership

Keep `work_pool_init` source-compatible and preserve its existing capacity.
`work_pool_init_foreground` adds a dedicated execution thread, queue, condition
and mailbox in caller-owned pool storage. Bulk I/O, discovery and raster never
execute on that thread. Foreground jobs explicitly use `WORK_FOREGROUND`.
The editor and unindexed-jump benchmark now enable the lane.

The foreground reserve is independent of the raster reserve. Each class prefers
its own reserve and may borrow the shared bulk slots; bulk cannot exhaust either
reserve. The optional worker has a separate mailbox identity without changing
legacy `n_workers` or raster-worker array indexing. Existing bounded drain,
selective receive, binding, cancellation, epoch exhaustion and shutdown contracts
also cover its mailbox. No production global or typing-path allocation is added.

`work_continue` preserves a job's argument, generation, identity and physical
lease, and appends another invocation to its class queue. A cancellation or
shutdown between invocations prevents resurrection. Arguments remain caller-owned
until physical completion, including a blocked bulk syscall.

`work_prioritize` upgrades an existing bounded, nonblocking bulk continuation.
A queued invocation moves immediately. A running invocation stays physically
owned by its original worker until return, then continues on foreground. The
mutex protects the scheduling-class member; fn/arg/generation remain immutable.
It needs no replacement snapshot, allocation, or second job slot.

Migration initially allowed the destination mailbox to overtake old-worker
results. A fuzzer found this, and a deterministic destination-first receive test
reproduced it. A pool-mutex-protected last-producer identity now defers an invocation on a
different worker until the old pending count reaches zero. That count includes callback
ownership. Deferred jobs go to the queue tail, so other jobs still run. The same guard
also covers continuation migration between raster workers: a two-raster-worker
fuzz campaign reproduced that ordering failure before the general guard. Normal foreground continuation publication does not wait for each message
to drain. UI pumping remains required, as with the existing mailbox contract.

## Index integration and limits

Both background and foreground indexing retain progress across bounded batches:
at most a (G) 1 MiB of scanned bytes, (G) 64 source spans, or (G) 16 chunk entries
per invocation. Result ranges remain immutable after publication. A full mailbox
requests a continuation instead of sleeping on the foreground resource.

`lineidx_build_start_foreground` creates an interactive resident-source build;
`lineidx_build_prioritize` upgrades the live build without replacing its lease.
A fragmented-source test proves another foreground request runs after at most
(G) 64 spans, and source release occurs after completion. The editor prioritizes
an incomplete copied-file index on a jump request, returns `EDITOR_MORE`, and
adopts/polls results when retried. Dirty-index maintenance is P1-1 §6 and remains
outside this bead.

Mapped sources can fault or block in the span/scan path. The foreground API
requires a resident/nonblocking source, so the editor keeps mapping builds on
bulk. A general mapped/cold jump still needs nonblocking I/O completion ownership
before it can use this service. This bead does not claim those end-to-end gates,
production find continuations, or a real-display keystroke-to-pixel verdict.
The new work service provides the execution resource for such consumers; it does
not make an arbitrary whole-file callback bounded or nonblocking.

## Benchmark decision

The new work row compares FIFO and foreground dispatch back to back, alternating
order for each pair, with one blocked bulk job and with all three queued. The FIFO
control releases I/O after a (G) 5 ms fixture wait; the foreground variant must
make progress before release under a (G) 2 s watchdog. Percentiles are TRACK only.
The benchmark exits nonzero for missing progress, not for a loaded-box latency
miss. The (G) warm G7j reference is 30/50 ms.

Final paired run: (M)[AC], power `Not charging`, load1 (M)[AC] 13.25. Values below
are nanoseconds, all (M)[AC]. The injected FIFO delay is fixture-controlled and
must not be read as a prediction of real save latency.

| Bulk configuration | FIFO p50 / p99 | Foreground p50 / p99 | Delivered before bulk release |
|---|---:|---:|---|
| One active | 5,170,960 / 5,865,450 | 115,309 / 2,049,007 | FIFO 0/32; lane 32/32 |
| All three queued | 5,165,339 / 5,218,695 | 115,072 / 127,944 | FIFO 0/32; lane 32/32 |

The evidence supports selecting the independent lane. It supplies no wall-time
gate verdict on the loaded box. Earlier paired measurements preceded the priority
and handoff changes and are superseded by the final row above.

Red/green transcripts, build/check/fuzz outcomes and the unrelated raster teardown
failure observed during validation are in `docs/worker-reports/edit-4w1.56-s8.md`.

## Session s8b integration with P1.6c and P4.I

The rebase keeps P1.6c's chunk rope, sliced UI seek/refresh/adoption, logical
cancellation and completion-gated CPU diagnostics. Worker builds and seeks share
persistent bounded continuation state, including partial chunk counts and a
pending terminal seek answer. Publication backpressure yields for both message
kinds. CPU checkpoints reset on each invocation because the worker may change;
a 4 ms thread CPU deadline (G) supplements the existing byte/span/entry bounds.
The editor jump prioritizes the active buffer's clean resident build using
P4.I's `e->buffer` fields. Resolution and fresh verification are recorded in
`docs/worker-reports/edit-4w1.56-s8b.md`.
