# edit-xzw: index completion and viewport relabeling

The reported failure is an asynchronous completion assertion race. The
headless fixture reproduced it with `lines_exact` published while scroll
relabeling was still awaiting a foreground resident window. No production
change is needed to make an idle viewport recover without user scrolling.

## Ordering derivation (P: source control flow)

`editor_step` pumps work, calls `editor_large_index_progress`, and calls
`editor_scroll_slice`. It later calls the scroll slice again before `submit`.
`submit` calls index progress before its readiness fence and again after
composition. Consequently publication can happen after the final scroll
slice in a turn. Publication and resident adoption are different milestones.

`large.c:index_progress` publishes the exact total and requests a full
layout. Its unwrapped path delegates the viewport ordinal to scrollctl.
The test's `all_done` predicate checks index/find/warm completion; it does
not check viewport relabeling, pending scroll work, or frame completion.

The unwrapped scroll fast path requires no pending scroll and agreement
between the scroll extent's exactness and the current index count. Before
index completion the placed byte jump can bypass resident acquisition.
When the count becomes exact, that exactness mismatch forces resolution of
the retained byte anchor. `scroll_resolve_resident` may return `SCROLL_MORE`
to acquire its window; scrollctl then sets `scroll_pending` and leaves the
displayed state alone. It can therefore hold an exact extent and an
estimated label simultaneously. This is the reproduced state, rather than
an idle fast path that forgot to relabel.

While pending, the no-pending fast path cannot run. Worker completion wakes
the pump, `editor_scroll_slice` resumes, and resident adoption clears
`top_estimated` and requests layout if the ordinal changed. The resolver's
exact lookup supplies the ordinal from the index and the retained byte
anchor. `editor_scroll_waiting` participates in readiness, and `submit`
refuses a frame while that scroll is pending. Thus the last estimated frame
can remain visible during acquisition, but no intervening estimated frame
is submitted after this publication fence, and recovery needs no scroll
input.

The foreground resident lane changes how soon that acquisition finishes;
it does not change this state machine. Neither a changed index exactness
nor a pending continuation can become an idle estimated label through
this ordering.

## Test choice

The deterministic regression resets the replacement index's publication
flag so an unusually fast original open cannot affect its starting state.
It holds the bulk index build, explicitly seeks
the byte jump through a foreground resident window, and completes the
index by polling between editor steps. It checks that the next turn
relabels, retains the independently verified byte anchor and ordinal,
submits an exact gutter, and performs no guarded allocations. This uses
the existing test-only index hold and public/private editor APIs; no
production hook or allocation is added.

The original fixture now waits for its existing `settled` milestone if
`all_done` returns with an estimated top. A fixed extra turn was tried
and failed under load: the foreground completion may still be pending
on that turn. The deterministic ready-window case verifies next-turn
adoption, but the loaded cold-window case requires worker completion.
Using the existing settle watchdog avoids inventing a scheduling count
or duration. This is the evidence-based extension of the brief's suggested
extra-step tolerance; the exact-label assertion remains strict after
settlement. A failure prints the pending state and dumps the trace after
closing the editor to quiesce worker writers.

The test-only `EDIT_XZW_REPRO` selector runs the existing generated
null-backend fixture and the deterministic regression without unrelated
IPC/display prerequisites. The default suite includes the regression too.

## Numeric evidence

The ready-window fixture inherits geometry, mmap threshold, estimated
colour and watchdog from `index_arrives_during_frame` (E: existing fixture
configuration, not a new performance bound). Its target is the last B row:
`A_LINES * 8 + (B_LINES - 1) * B_LEN` (P: each A row is one letter, six
digits and LF; each preceding B row occupies `B_LEN` bytes). EOF clipping
may move the displayed anchor, so the ordinal oracle counts actual tree
newlines at that retained anchor. Empty state and API-success values are
P: zero initialization and return-code definitions.

The loaded green reproduction had load1 **23.04 (M)[AC]**, read from
`/proc/loadavg`; BAT0 reported `Not charging`, interpreted as AC by the
repository convention. The observed Boolean pending/exactness flags in
the worker report are M[AC] snapshots. No latency budget or repetition
count was introduced.
