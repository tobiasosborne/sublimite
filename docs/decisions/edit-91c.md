# edit-91c: large-file follows outside laid-out rows vs the bulk lane

## Finding

Two waits, not one. (1) The resident window bridge (`src/scroll/resident.c`)
requested every window on WORK_BULK, where a lazy large file's index build,
warm and find jobs are continuations that re-queue behind each other, so a
window waited a round of their slices, or forever when a test holds the lane.
(2) Worse and earlier in the chain: Ctrl+End on a file whose line count is not
exact runs view's VIEW_DOC_END, whose finish needs the exact line number of
EOF (`view.c` line_query). With no index anchor near EOF that is a UI-thread
scan of the whole file in 0.5 ms slices (about 45 MB/s under ASan (M)[AC]),
answered only when the index completes (`large.c seed_navigation`,
`seed_pending_query`). The resident bridge was never even asked: the trace in
the report shows `windows=0/0 view_busy=1 phase=P_FINISH` for the whole wait,
and the first frame at the end arrived 1.39 s (M)[AC, ASan, load 16] after the
key, when the index finished.

## Decision 1: in-core windows take WORK_FOREGROUND

Foreground lane contract (`src/work/work.h`, `work_pool_init_foreground`):
bounded CPU work or continuations, cancellation polled at least every 5 ms CPU,
no blocking I/O. `acquire_window` copies at most SCROLL_SCAN_BUDGET = 64 KiB in
4 KiB pieces, polling `work_should_stop` between pieces and yielding with
`work_continue` at a 0.5 ms wall deadline. Bound relied on: 65536 B at the
naive scalar scan rate 6.3 GB/s (M)[bat] (`perf/00-hardware-profile.md`) is
about 10 us (E) per window; a cached page not yet in this process's page table
costs a minor fault, about 1 us (E, `perf/01-perf-target.md` section 2.4,
fault-around estimate), 65536 / 4096 = 16 pages (G, existing constants) x 1 us
= 16 us (E). Both are far inside the 5 ms poll bound. The only thing that could
block is a major fault on a file-backed snapshot, so the copy goes foreground
only when every page of the window is in core at submit time: `mincore` over
the window's spans, addresses only (`piece_iter_next` derives pointers without
touching bytes; nothing is dereferenced on the UI thread). The probe is capped
at 16 spans, the worker's own 4 KiB piece count for one window; a window split
into more spans, a page not in core, a failed probe, a page size below 4 KiB,
a pool without the lane, or an exhausted foreground reserve keep WORK_BULK.
The probe-to-copy race (eviction under memory pressure) is covered by the
contract's own softness clause ("OS scheduling and memory-bandwidth contention
are not hard wall-time guarantees"). No new lane: the work module's three
classes stay; `work_prioritize` was not used because it carries the same
"never prioritize I/O" rule and the class is known at submit time anyway.

## Decision 2: Ctrl+End on an unindexed lazy file bypasses view's exact query

`src/editor/input.c large_doc_end`: when unwrapped, `lg.lazy` and
`!lg.lines_exact`, Ctrl+End (and Shift+Ctrl+End) places the cursor at EOF the
way the byte jump does (`large.c editor_large_goto_byte`, edit-agh) and lets
scrollctl resolve the EOF viewport through resident windows with an estimated
label (`scroll_key_motion` already seeks by byte when the extent is inexact);
`index_progress` relabels it exactly on completion through the existing
scrollctl path. Page motions keep view's exact query (see Remains).

## Measurements (ASan/UBSan build, Xvfb :99, box load 16-24, BAT0 "Not charging" = [AC])

| case | before | after |
|---|---|---|
| Ctrl+End during the real 1 GiB index build, key -> first frame at the end | 774 ms, 1 387 ms (two runs) (M) | 7.9 ms, 5.8 ms (M) |
| Ctrl+End with the bulk lane held for the whole build (never completes) | never (5 s watchdog) | 2.7 ms, 2.1 ms (M) |
| Ctrl+End, index complete, bulk lane held (warm/find family) | n/a (not measured before) | 0.96 ms, 0.61 ms (M) |
| 79 PageUp + 79 PageDown across the EOF window boundary, index complete, bulk held, worst | would wait on the held lane | 6.7 ms, 11.7 ms (M) |

Budget (tracked, not enforced, law 1): G5 warm, column A: open request ->
correct first viewport = 64 KiB read + full frame, PB 1.025 ms (E), gate
6 / 9 ms p50/p99 (G). A resident window is the same 64 KiB read followed by
the same full frame, so the row transfers to "Ctrl+End -> first frame at the
end". The sanitizer numbers above are not comparable to that release budget.

## Remains (not done here)

PageUp/PageDown while the index is still building and the cursor is outside
the built prefix: view's setup needs `line_at(cursor)` exactly, so the first
PageUp after the new Ctrl+End waits for the index (1.08 s, 1.63 s (M) on the
two no-hold runs). A relative-motion path with estimated ordinals (seed view
with scroll's certified byte / estimated label pairs and invalidate on
relabel, or let layout rows carry the cursor) is a separate bead; view's cache
invariant "certified line starts, never approximate counts" is the settled
point to weigh.

Landing note (Opus lander, 2026-10-13): the probe calls the bridge source's
`span` callback on the UI thread, so a resident source's `span` must not block.
Production sources (`snapshot_span`, piece iterator) only derive pointers.
`tests/editor_scroll_test.c` `cancellation` instrumented `span` to hold the
worker and spun the UI thread forever under this change; its `held_span` now
holds only off the UI thread (the worker copy), which is what that test means
to hold.
