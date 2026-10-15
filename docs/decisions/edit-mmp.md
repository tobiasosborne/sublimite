# edit-mmp: typing before the line index completes

## Root cause

`src/editor/large.c` `editor_large_init`, lazy branch (was lines 52-53): it set
`lg.lines_exact = true` only when the prefix covered the file and never cleared
it otherwise. A COPY file larger than FILE_PREFIX_MAX opens pending; prepare
(`src/editor/buffers.c:215`) marks the *preview* exact (`lines_exact = true`,
`lines = piece_line_count(preview)`). Adoption (`editor_adopt_open`) re-enters
`editor_large_init` for the whole source: the buffer becomes lazy and `b->lines`
becomes a density estimate, but the stale `lines_exact = true` survives.

While the index build runs, the first key's `lineidx_edit` cancels it, and the
scroll resolver labels the top with an index estimate (with no built prefix,
lineidx's DEFAULT_BPL of 40 bytes per line: byte 2,097,088 -> line 52,427 where
the real line is 32,767). `editor_scroll_slice` computes
`top_estimated = approximate && !lines_exact` = false, so the estimated label is
adopted as exact. `layout_slice` (`src/editor/editor.c`) clips every row with
`lines_exact && line >= lines` (52,427 >= 32,769), leaving all rows void and
`lay.pos` short of `first_byte`. `editor_paint_prepare` (`src/editor/paint.c`)
then computes `hi < lo`; `hi - lo` underflows, `hi = lo + LAYOUT_BYTE_BUDGET`
exceeds the length, `indent_bracket_match` returns INDENT_ERR_RANGE, and paint
returns EDITOR_ERR_ARG, which `editor_step` latches through `fail()`.

The race: if the index finished before the first key, maintenance replaced the
estimated count with the exact one and nothing visible went wrong; otherwise the
first frame after the first key failed. (M) edit-28u saw 3 of 40; this bead saw
1 to 10 of 40 unheld runs of the same 2 MiB fixture [AC].

## Decision

`editor_large_init` assigns `lines_exact` on every path: exact iff the bounded
prefix covers the source (`plen >= size`), else false with the density estimate.
Before publication the editor then uses its designed estimate path (dim gutter,
estimated labels, byte-offset placement, edit-jf9 relative motion), and
`index_progress` publishes the exact count when the index completes, as for
mapped files. Keys are applied, never rejected.

No change to the typing path: the assignment runs once per open/adoption. Release
editor_test before/after: every `mallocs=0` line unchanged, G1 guard passes.

The `hi - lo` underflow in `editor_paint_prepare` is not changed: with a correct
`lines_exact`, a top label is never both estimated and trusted by the clip. It is
reported as a hardening note, not fixed here (bead scope).

## The settle watchdog (EDITOR_IDLE_NS)

`EDITOR_IDLE_NS` (`src/editor/editor.h:17`, 10 s) is the caret-blink idle
cut-off that tests reuse as a settle watchdog; its value carries no derivation
in the header. It is not raised.

Measured cause of long settles on a 1 GiB mapped file (sparse fixture with one LF
per MiB, bulk lane held so the index job does not run at all): the second key at
EOF and the first Delete after a jump took 0.8 to 2.1 s each (M)[AC, Full,
load1 3 to 6]. A stack sample during the slow key shows the UI thread in
`src/view/view.c` `line_query` called from `advance` P_NORMAL_SETUP
(`view_continue` <- `editor_continue_action`): the edit repair counts LFs from
the start of the file to label `first_byte`, because `editor_seed_view_lines`
(`src/editor/editor.c`) declines to seed the view's cache while the top is
estimated (the view caches hold certified lines only, edit-jf9). It is bounded
per turn (VIEW_SCAN_BOUND) but proportional to the cursor's byte offset,
about 2 s per GiB (M) in release, about 5x more under ASan (E: the sanitizer
slow-down this suite already shows for the 2 MiB case, 6 ms -> 30 ms).

This is not contention with the index job: holding the bulk lane does not remove
it. The fix belongs to the view's edit repair (a byte-relative repair like
edit-jf9's relative motion, or a certified anchor near the cursor) and is
larger than this bead. Reported for a separate bead; repro:
`EDIT_MMP_REPRO=1 EDIT_MMP_MIB=1024 build/tests/editor_large_test`.

## Test

`tests/editor_large_test.c` `typing_while_indexing`: 2 MiB COPY fixture, 40
opens per case. A bulk job yields its lane (`work_continue`) until the COPY is
ready, then holds the single bulk lane, so the adoption's index build queues
behind it and every key precedes index completion: deterministic 40 of 40 red.
A second case edits after the first index and continues during the rebuild.
Settle times are tracked prints. Each iteration has a private XDG_DATA_HOME
removed at its end; the fixture root is removed recursively on every exit path.
