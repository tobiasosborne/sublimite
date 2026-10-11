# edit-2ty — near-end large-file interaction

Implemented the prefix-replay fix. The production release regression is green in nowrap and the file-default wrapped mode. Full check acceptance remains blocked by sandbox X/Unix socket restrictions; no display or Sublime parity verdict is claimed. The next Astra drive remains coordinator work.

## Changed files

- `src/editor/large.c`: install certified EOF/previous-page anchors, use bounded nearby backward reads with index fallback, answer pending queries into the view's fixed cache, and delimit seeding with trace events. No allocation, new global, worker wait or prefix walk is introduced.
- `tests/editor_large_test.c`: failing Astra sequence first; trace input-to-submit assertions, an independent tail-newline/viewport oracle, cursor and inserted-byte checks, active release allocation counting, wrapped/default coverage and queued-page continuation coverage.
- `src/editor/STATUS.md`, `docs/decisions/edit-2ty.md`, `docs/worklog/2026-10-11.md`, `HANDOFF.md`: current worker state and decisions.
- This required worker report.

No `bd` or `git` was run. Work stayed in this worktree. No input, paint, clipboard, save, scroll, lineidx, file or view implementation was changed beyond the scoped `large.c` host integration.

## Cause and derivations

The complete index and warmed piece tree were already available in the loaded-buffer repro. View's cooperative line queries only have a small cache of exact anchors. Ctrl+End lacks an EOF anchor. Page Up asks for an earlier line than the old viewport seed; once earlier entries are evicted, the query starts at byte zero again. This is the requested prefix-reseek hypothesis, in the host/view query path rather than in repeated sparse-index construction. The wrapped control below captures the active query still scanning the prefix while targeting the near-end byte.

Typing in the original nowrap run completes once pending navigation finishes; the apparent inert Home/End/typing follows the outstanding page query. No repeated resident-window rejection was observed. The scroll resident bridge is not wired into this editor keyboard path (source inspection); no scroll/file change was needed.

The bound is `EDITOR_SLICE_NS = 500000 ns (G)` from `src/editor/editor.h`; `500000 / 1000000 = 0.5 ms (G)` is only a unit conversion. The bead explicitly requests this existing bound. Every measured duration below is the matching frame's trace `T4 - T0`, including injection-to-dequeue and host seeding. Raw action ordinals are the test sequence's loop indices. Trace rings are dumped only after all editor workers have shut down.

The fixture size is `1024 * 1024 * 1024 = 1073741824 bytes (P, GiB definition; M[bat], stat asserted by the test)` from the supplied corpus. The page count `20 (G)` comes from the supplied Astra repro. Total actions `24 (G/M[bat]) = Ctrl+End (1) + Page Up (20) + Home/End/Z (3)`; the test requires every frame endpoint, with no missing sample accepted. Geometry `60 columns / 8 rows (G)` is reused from the existing mapped-startup headless fixture, not a new timing allowance. Page distance is the actual configured view row count. The default wrapped mode comes from `view_wrap_default("log_1g.txt")`.

The test waits for existing index/warm completion to model Sublime's **loaded-buffer** comparison. Open/index/warm latency and unindexed early input are not certified by this regression. The backward walker reuses `VIEW_SCAN_BOUND` and `VIEW_WINDOW`; long pages fall back to at most the target index chunk. Layout anchors must match the current tree length and viewport, with no pending reset or approximate metadata. This preserves exact coordinates while avoiding a chunk scan for ordinary nearby log pages.

**Timing-build interpretation:** the production release build enforces the literal bound for every action. Sanitizer builds retain all byte/state/trace assertions and report timing against the same bound as TRACK; no larger sanitizer budget is invented. ASan's allocator guard is intentionally inert in this repository, so allocation evidence comes from the active release guard. I asked for clarification on enforcing the timing bound under ASan too and, without a reply, used this production-build interpretation. Instrumented timings exceed the bound as shown below; this is not a claim of instrumented timing acceptance.

## Red first

Before changing production code, added the regression and ran:

```sh
make build/tests/editor_large_test
EDIT_2TY_REPRO=1 build/tests/editor_large_test
```

The first execution failed `bounded`; the full sequence run on unchanged production code also failed. BAT0 was `Discharging` (M)[bat]. Raw output follows, with duration derivations on each row. The source-line number belongs to that red test revision.

```text
editor_large_test:852: FAIL bounded
edit-2ty action=0 Ctrl+End input_submit_ns=2795746297 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=1 PageUp input_submit_ns=63168 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=2 PageUp input_submit_ns=50936 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=3 PageUp input_submit_ns=50477 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=4 PageUp input_submit_ns=2625956941 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=5 PageUp input_submit_ns=59621 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=6 PageUp input_submit_ns=60906 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=7 PageUp input_submit_ns=52013 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=8 PageUp input_submit_ns=2699093397 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=9 PageUp input_submit_ns=60478 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=10 PageUp input_submit_ns=50081 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=11 PageUp input_submit_ns=50394 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=12 PageUp input_submit_ns=2641622118 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=13 PageUp input_submit_ns=94703 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=14 PageUp input_submit_ns=55833 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=15 PageUp input_submit_ns=48249 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=16 PageUp input_submit_ns=1749394496 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=17 PageUp input_submit_ns=30273 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=18 PageUp input_submit_ns=23863 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=19 PageUp input_submit_ns=24587 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=20 PageUp input_submit_ns=1332053310 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=21 Home input_submit_ns=29051 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=22 End input_submit_ns=40110 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=23 Z input_submit_ns=117541 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
```
The initially failing test preceded all production seeding changes. Default wrapping was then added to cover the actual file policy. A controlled removal of only the new seeding helpers/call reproduced the wrapped stall; the fixed source was restored before green builds:

```sh
EDIT_2TY_REPRO=1 EDIT_2TY_WRAP=1 build/tests/editor_large_test
```

```text
edit-2ty stalled: action=18 query_pos=894521344 query_target=1073735849 phase=11 (M)[bat]
editor_large_test:930: FAIL bounded
edit-2ty mode=file-default-wrap power=Discharging (M)[bat]
edit-2ty action=0 Ctrl+End input_submit_ns=2309841498 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=1 PageUp input_submit_ns=147808 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=2 PageUp input_submit_ns=72665 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=3 PageUp input_submit_ns=81118 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=4 PageUp input_submit_ns=72711 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=5 PageUp input_submit_ns=68479 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=6 PageUp input_submit_ns=78758 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=7 PageUp input_submit_ns=71756 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=8 PageUp input_submit_ns=75681 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=9 PageUp input_submit_ns=4013521724 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=10 PageUp input_submit_ns=84255 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=11 PageUp input_submit_ns=70082 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=12 PageUp input_submit_ns=79901 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=13 PageUp input_submit_ns=78799 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=14 PageUp input_submit_ns=93155 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=15 PageUp input_submit_ns=82766 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=16 PageUp input_submit_ns=75866 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty action=17 PageUp input_submit_ns=68933 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS
edit-2ty typing_path_allocations=0 (M)[bat]=counting allocator; required=0 (G)=CLAUDE law 2
```
In this control, `query_pos` and `query_target` are measured view-state fields, and `phase` is the measured continuation phase. The setup/settle watchdog was reused from the existing suite, not introduced as an acceptance budget.

## Green runs on final source

```sh
make -j4 all build/san/tests/editor_large_test
EDIT_2TY_REPRO=1 build/tests/editor_large_test
EDIT_2TY_REPRO=1 EDIT_2TY_WRAP=1 build/tests/editor_large_test
ASAN_OPTIONS=detect_leaks=0 EDIT_2TY_REPRO=1 build/san/tests/editor_large_test
ASAN_OPTIONS=detect_leaks=0 EDIT_2TY_REPRO=1 EDIT_2TY_WRAP=1 build/san/tests/editor_large_test
```

All selected commands returned `0 (M)[bat]`, read from their recorded process return codes. The release build's allocation guard is active. Each mode independently checks the full sequence, exact logical viewport membership, Home/End behavior, cursor/anchor, inserted byte and final length. The nowrap run also checks queued Ctrl+End/Page Up after an edit/index restart. Sanitizer functional/trace results are green; their timing gate is TRACK as explicitly described above.

### release-nowrap

Maximum `T4 - T0 = 104363 ns (M)[bat]`, derived as `max` of the `24 (M)[bat]` matched durations below (action-count derivation above).

```text
edit-2ty mode=nowrap power=Discharging (M)[bat]
edit-2ty action=0 Ctrl+End input_submit_ns=41649 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=1 PageUp input_submit_ns=28472 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=2 PageUp input_submit_ns=26877 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=3 PageUp input_submit_ns=26294 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=4 PageUp input_submit_ns=26555 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=5 PageUp input_submit_ns=25006 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=6 PageUp input_submit_ns=25693 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=7 PageUp input_submit_ns=24433 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=8 PageUp input_submit_ns=23900 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=9 PageUp input_submit_ns=24530 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=10 PageUp input_submit_ns=32928 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=11 PageUp input_submit_ns=30219 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=12 PageUp input_submit_ns=29684 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=13 PageUp input_submit_ns=25691 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=14 PageUp input_submit_ns=25410 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=15 PageUp input_submit_ns=23723 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=16 PageUp input_submit_ns=23541 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=17 PageUp input_submit_ns=24638 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=18 PageUp input_submit_ns=23634 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=19 PageUp input_submit_ns=25154 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=20 PageUp input_submit_ns=25095 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=21 Home input_submit_ns=22225 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=22 End input_submit_ns=39701 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=23 Z input_submit_ns=104363 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty typing_path_allocations=0 (M)[bat]=counting allocator; required=0 (G)=CLAUDE law 2 guard=active
edit-2ty near-end input/submit: ok
```

### release-wrap

Maximum `T4 - T0 = 303436 ns (M)[bat]`, derived as `max` of the `24 (M)[bat]` matched durations below (action-count derivation above).

```text
edit-2ty mode=file-default-wrap power=Discharging (M)[bat]
edit-2ty action=0 Ctrl+End input_submit_ns=106883 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=1 PageUp input_submit_ns=224618 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=2 PageUp input_submit_ns=130067 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=3 PageUp input_submit_ns=149640 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=4 PageUp input_submit_ns=142896 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=5 PageUp input_submit_ns=303436 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=6 PageUp input_submit_ns=93875 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=7 PageUp input_submit_ns=73961 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=8 PageUp input_submit_ns=67706 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=9 PageUp input_submit_ns=78509 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=10 PageUp input_submit_ns=81526 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=11 PageUp input_submit_ns=67404 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=12 PageUp input_submit_ns=78456 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=13 PageUp input_submit_ns=74689 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=14 PageUp input_submit_ns=86342 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=15 PageUp input_submit_ns=79536 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=16 PageUp input_submit_ns=72468 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=17 PageUp input_submit_ns=66869 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=18 PageUp input_submit_ns=78808 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=19 PageUp input_submit_ns=71877 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=20 PageUp input_submit_ns=67955 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=21 Home input_submit_ns=18027 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=22 End input_submit_ns=18663 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty action=23 Z input_submit_ns=77845 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=release
edit-2ty typing_path_allocations=0 (M)[bat]=counting allocator; required=0 (G)=CLAUDE law 2 guard=active
edit-2ty near-end input/submit: ok
```

### san-nowrap

Maximum `T4 - T0 = 519995 ns (M)[bat]`, derived as `max` of the `24 (M)[bat]` matched durations below (action-count derivation above).

```text
edit-2ty mode=nowrap power=Discharging (M)[bat]
edit-2ty action=0 Ctrl+End input_submit_ns=318216 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=1 PageUp input_submit_ns=262739 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=2 PageUp input_submit_ns=261789 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=3 PageUp input_submit_ns=334930 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=4 PageUp input_submit_ns=279450 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=5 PageUp input_submit_ns=266116 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=6 PageUp input_submit_ns=225914 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=7 PageUp input_submit_ns=164785 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=8 PageUp input_submit_ns=159162 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=9 PageUp input_submit_ns=164519 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=10 PageUp input_submit_ns=161986 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=11 PageUp input_submit_ns=169504 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=12 PageUp input_submit_ns=172024 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=13 PageUp input_submit_ns=161968 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=14 PageUp input_submit_ns=169674 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=15 PageUp input_submit_ns=158962 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=16 PageUp input_submit_ns=161358 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=17 PageUp input_submit_ns=160949 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=18 PageUp input_submit_ns=166753 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=19 PageUp input_submit_ns=165268 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=20 PageUp input_submit_ns=172062 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=21 Home input_submit_ns=128298 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=22 End input_submit_ns=272747 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=23 Z input_submit_ns=519995 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty typing_path_allocations=0 (M)[bat]=counting allocator; required=0 (G)=CLAUDE law 2 guard=ASan-inert (release required)
edit-2ty near-end input/submit: ok
```

### san-wrap

Maximum `T4 - T0 = 1665572 ns (M)[bat]`, derived as `max` of the `24 (M)[bat]` matched durations below (action-count derivation above).

```text
edit-2ty mode=file-default-wrap power=Discharging (M)[bat]
edit-2ty action=0 Ctrl+End input_submit_ns=245369 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=1 PageUp input_submit_ns=1665572 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=2 PageUp input_submit_ns=771931 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=3 PageUp input_submit_ns=1290429 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=4 PageUp input_submit_ns=552516 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=5 PageUp input_submit_ns=526088 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=6 PageUp input_submit_ns=652430 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=7 PageUp input_submit_ns=540884 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=8 PageUp input_submit_ns=503072 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=9 PageUp input_submit_ns=598768 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=10 PageUp input_submit_ns=591054 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=11 PageUp input_submit_ns=525658 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=12 PageUp input_submit_ns=617646 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=13 PageUp input_submit_ns=549724 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=14 PageUp input_submit_ns=516437 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=15 PageUp input_submit_ns=616214 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=16 PageUp input_submit_ns=544135 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=17 PageUp input_submit_ns=501229 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=18 PageUp input_submit_ns=614361 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=19 PageUp input_submit_ns=519297 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=20 PageUp input_submit_ns=467402 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=21 Home input_submit_ns=111270 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=22 End input_submit_ns=108683 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty action=23 Z input_submit_ns=439023 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-sanitized
edit-2ty typing_path_allocations=0 (M)[bat]=counting allocator; required=0 (G)=CLAUDE law 2 guard=ASan-inert (release required)
edit-2ty near-end input/submit: ok
```

The final trace artifacts are `build/edit-2ty.trace` and `build/edit-2ty-wrap.trace` (last sanitized runs). The older release trace copies and all red/green logs remain under `build/edit-2ty-*`. No displayed frame latency is inferred from the null renderer.

## Required builds and complete suite sweep

`make all` (gcc release, the Makefile's C11 warning flags) passed on final source. Build log: `build/edit-2ty-build-geometry-guard.log`; final `make all` confirmation: `build/edit-2ty-all-confirm.log`.

`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check` was run as required. The completed first run returned `2 (M)[bat]` from make and stopped at CLI's denied IPC bind. A later run waited for the shared display lock (owner reported unknown); I interrupted that wait. A nonblocking retry used the existing `EDIT_DISPLAY_LOCK_TIMEOUT=0` option and failed at that lock (`build/edit-2ty-check-no-wait.log`). The final attempt after the viewport guard rebuilt the sanitizer binaries, reached CLI, and again failed its denied IPC bind (`build/edit-2ty-check-final.log`); make returned `2 (M)[bat]`, its actual process exit. Logs: `build/edit-2ty-check.log`, `build/edit-2ty-check-no-wait.log`, `build/edit-2ty-check-final.log`. All sanitizer binaries were built. Leak detection is disabled according to HANDOFF's sandbox restriction; coordinator must rerun with leaks enabled.

To exercise every binary after make's early exit, ran them sequentially with ASan/UBSan and safe DISPLAY, recording each return code and complete output. The final direct sweep avoids the contended shared display lock: this sandbox cannot acquire a display or bind any Unix socket. A separate AF_UNIX probe inside this worktree confirmed socket creation succeeds but bind returns EPERM. This is an environment failure, not a timing or editor assertion failure.

`63 suites (M)[bat] = number of rows in build/edit-2ty-suite-results.tsv`; `46 passed (M)[bat] = count(returncode == 0)`; `17 failed (M)[bat] = 63 - 46`. The permissions-blocked failures are:

| Actual restriction | Suites | Evidence |
|---|---|---|
| X display connection/initialization | `editor_close_test`, `file_kill_test`, `raster_test`, `refproto_test`, `refwin_test`, `refwin_timeout_test`, `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`, `x11_stall_test` | Native editor/platform init, reference-tool connection error, fixture initialization, or private Xvfb connection fails. File-kill's viewport/queued cases call the real platform initializer from its included bench. |
| Local Unix socket bind | `cli_test`, `editor_large_test`, `editor_p4_test`, `ipc_test` | `ipc_server_init[_display]` fails. The new large-file regressions pass in both modes before the legacy mapped-startup IPC case fails. |
| Mixed native X / launcher IPC | `editor_test` | Native editor-open cases and the reload launcher fail; preceding headless checks run. |

`gl_test` and `gl_gate_test` pass their available pure cases; **native GL diagnostics explicitly SKIP** because an Xvfb/EGL context is unavailable. This is not native GL verification. Pure X11 alignment/input/XI2/display-guard cases pass; the live X11 suites above fail for display reasons. The coordinator needs the full Xvfb `:99` rerun outside this socket-denied sandbox, including the IPC-dependent headless suites.

Passed suite names (derived directly by selecting successful rows from the TSV):

`base_test`, `file_test`, `find_bench_test`, `find_bestof_test`, `find_p1r_test`, `find_simd_filter_verify_test`, `find_test`, `find_timeout_test`, `findui_test`, `font_test`, `gl_gate_test`, `gl_test`, `harness_test`, `indent_test`, `journal_kill_test`, `journal_test`, `keys_test`, `layout_test`, `lineidx_test`, `minimap_test`, `piece_bounds_test`, `piece_mem_test`, `piece_mt_test`, `piece_test`, `prewake_test`, `render_test`, `replay_test`, `runtime_test`, `savectl_reload_test`, `savectl_test`, `scan_test`, `scroll_test`, `tabs_test`, `trace_test`, `undo_test`, `unicode_render_test`, `utf8_tables_test`, `utf8_test`, `view_test`, `work_foreground_test`, `work_test`, `wrap_test`, `x11_alignment_test`, `x11_display_guard_test`, `x11_input_test`, `x11_xi2_test`.

`sh tools/test_replay_cli.sh build` passed; full output is in `build/edit-2ty-replay-cli.log`. The remaining display-lock/refwin shell contract needs the coordinator's display environment. All per-suite output is in `build/edit-2ty-suite-logs/`; no failing suite was suppressed in the sweep. The full sweep preceded the final viewport-consistency guard. After that guard, rebuilt all release targets, repeated make check, and reran both release and sanitizer near-end modes; the guard only narrows the nearby fast path to exact matching metadata and otherwise uses the already-tested index fallback.

## Existing module bench and findings outside scope

Ran the existing `build/bench/editor_large_bench --attribute-log` on the shared box, stamped `Discharging` (M)[bat]. The final invocation returned `255 (M)[bat]` (captured process exit), because its existing structural population requirement is unmet, not because this patch's regression failed. This attribution mode collects `3 open samples (M)[bat]` (its existing CLI mode passes that count to `row_open`); the harness asks for `10000 (G, existing BENCH_INTERACTION_MIN_N)` samples. The navigation population below is also smaller than that existing requirement. Raw final rows:

```text
STAMP (M)[bat] BAT0=Discharging load1=2.16 shared box mode=TRACK
BENCH name=editor_large_log_1g_G5_open_first_viewport n=3 required_n=10000 p50=10301896 p99=14054817 ci95_p50=[9338304,14054817] ci95_p99=[10301896,14054817] gate_p50=6000000 gate_p99=9000000 dropped=0 (M)[bat] power=Discharging load1=- verdict=TRACK
BENCH name=editor_large_log_1g_G7_open_index_exact n=3 required_n=10000 p50=458352265 p99=459040710 ci95_p50=[322488396,459040710] ci95_p99=[458352265,459040710] gate_p50=80000000 gate_p99=125000000 dropped=0 (M)[bat] power=Discharging load1=- verdict=TRACK
LINES log_1g estimated_at_first_viewport=8947713 exact_after_index=8947842 (G7 gate is 1 GB; sparse_10g tracked)
BENCH name=editor_large_log_1g_navigation_regression n=2529 required_n=10000 p50=376141 p99=520999 ci95_p50=[374850,377678] ci95_p99=[498942,540913] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[bat] power=Discharging load1=- verdict=TRACK
FG log_1g mix=index_active samples=2529 (M)[bat] metric=navigation regression (G)1/2ms mode=TRACK structural=FAIL
editor_large_bench:246 failed: report_fg(name,&fg,"index_active",track,false) == 0
```
These attribution rows are tracked, with no distribution/gate acceptance claim. Some inherited ATTR lines incorrectly hardcode `[AC]` despite the bench's actual `[bat]` STAMP; no bench code was changed. Full log: `build/edit-2ty-bench-final.log`.

Other scoped-out findings: this sandbox also denies **local** Unix binds, not just the X socket; the shared display lock can be held by an owner unavailable in this process namespace; pure/native suites sometimes combine both prerequisites in one binary. I reported these rather than changing IPC, test infrastructure or unrelated module implementations.

## What remains

Coordinator: run the full sanitizer/leak-enabled suite outside the sandbox on Xvfb `:99`, then the next Astra vs Sublime drive on the resulting build. Do not hand the editor to Tobias before that drive. Native display/GL, actual visible-input latency and Sublime parity are unverified here. Early input before index/warm completion and arbitrarily long-line fallback timing are not certified by this loaded-log test. The pending-query bridge intentionally depends on the view's documented-in-code private query tags; future view-protocol changes must preserve/update that bridge.

If the coordinator requires the same wall-time performance assertion in instrumented ASan builds, that acceptance is still open: the printed sanitizer timings exceed the unchanged production slice bound. This report does not turn those measurements into passes.

Worker stops after this bead; no commit, bead closure, other fix or external message was performed.
