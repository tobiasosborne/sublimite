# edit-tn2 worker report

Implementation complete in this worktree. Release build and focused sanitizer
regressions pass. Full verification remains incomplete because of sandbox
socket restrictions and an unrelated journal-kill failure. No `bd` or `git`
was run; no other bead or protected document was edited.

## Files touched

- `src/scroll/scroll.c`: extent changes clamp only an exact label against an
  exact count; estimated labels retain their byte anchor and pending request.
- `src/editor/scrollctl.c`: wrapped certified starts are accepted by
  `editor_scroll_place`, enabling the existing byte-jump call to use
  `editor_refresh_placed`. Logical scroll state is not used for visual rows.
- `src/editor/large.c`: wrapped index publication clears the view's
  approximation flag when it replaces the estimated logical label.
- `tests/scroll_test.c`: strengthen the inherited estimated-extent regression
  to cover an exact label with an inexact count as well.
- `tests/editor_large_test.c`: strengthen the inherited wrapped placement and
  byte-jump regressions; force mapped acquisition before holding the bulk
  worker; wait for index publication without requiring an unstarted find.
- `docs/decisions/edit-tn2.md`: anchor policy, wrapped placement, retained
  logical-label publication and retained scrollctl workaround.
- This report. Generated binaries, fixtures and evidence logs are in `build/`.

The worktree already contained bead-specific tests without the production
fixes. Assertions were strengthened before implementation. After correcting
the wrapped integration fixture's setup, its red run restored the old wrapped
rejection in `editor_scroll_place`, then the new branch was reinstated for
green. No production diagnostic instrumentation remains.

No allocation was added. Production changes are state assignments and a
conditional clamp; existing errors remain return codes. Page-motion and
estimated-label assertion work assigned to other workers was not changed.

## Evidence conventions

Power checked before runs: BAT0 `Not charging`, [AC]. All pasted output below
is observed (M)[AC]. Source line numbers are source positions; exit/errno
values are process/OS return codes (P), rather than budgets. Fixture sizes and
ordinals derive from literal text and `write_fixture` geometry (P); viewport
and watchdog settings reuse `gutter_estimates` (E). No new duration or sample
budget was introduced.

`ASAN_OPTIONS=detect_leaks=0` disables the leak pass because LeakSanitizer
reports that it cannot run under this sandbox's ptrace. ASan and UBSan remain
enabled. The coordinator must rerun with leak detection enabled outside the
sandbox.

## Red runs

Observed output (M)[AC], before the respective production fix:

```text
$ env ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_TN2_EXTENT=1 build/san/tests/scroll_test
scroll_test:124: FAIL s.first_line == before.first_line && s.request == SCROLL_READY
exit=1

$ env ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_TN2_PLACEMENT=1 build/san/tests/editor_large_test
editor_large_test:623: FAIL editor_scroll_place(&e, sizeof prefix - 1u, sizeof prefix - 1u, true)
editor_large_test:1183: FAIL wrapped_placement() == 0
exit=1

$ env ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_TN2_WRAPPED_JUMP=1 build/san/tests/editor_large_test
editor_large_test:668: FAIL editor_view(e).wrap && editor_view(e).approximate
exit=1
```

The wrapped jump fixture additionally checks a genuine soft wrap, an unchanged
byte/visual anchor across index publication, and an exact final logical label.
The pure placement fixture checks that the buffer's logical scroll state is
unchanged for both estimated and exact placements.

## Green runs

Observed output (M)[AC], with final code:

```text
$ env ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_TN2_EXTENT=1 build/san/tests/scroll_test
scroll_test: estimated extent retains byte anchor until exact relabel PASS
exit=0

$ env ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_TN2_PLACEMENT=1 build/san/tests/editor_large_test
editor_large_test: wrapped certified placement preserves estimated/exact label PASS
exit=0

$ env ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_TN2_WRAPPED_JUMP=1 build/san/tests/editor_large_test
editor_large_test: wrapped byte jump keeps certified anchor through index publication PASS
exit=0

$ make all
make: Nothing to be done for 'all'.
exit=0

$ env ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/editor_test --headless
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
editor_test: headless passed
exit=0

$ env DISPLAY=:99 build/tests/editor_test --headless
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: headless passed
exit=0
```

The typing population is the existing allocation-test loop; the release guard
counts malloc calls across input through submit (P: existing test definition).
Both headless editor commands ran the complete headless mode, not just the
allocation fixture. The configured gcc release and clang sanitizer builds
compiled with the Makefile's required standard and warnings.

Required suite outcomes, observed (M)[AC]; exit values follow process status (P):

| Suite | Exit | Result |
|---|---|---|
| `scroll_test` | 0 | Complete sanitizer suite PASS |
| `scroll_display_test` | 0 | Complete sanitizer suite PASS |
| `editor_scroll_test` | 0 | Complete sanitizer suite PASS |
| `editor_large_test` | 1 | New wrapped cases and preceding null cases pass; stopped at `mapped_startup_ipc` socket initialization |
| `editor_test` | 1 | Full mode fails at display/launcher cases; complete headless mode passes |

Observed full-check failure (M)[AC]:

```text
$ env ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 make check
== build/san/tests/base_test
base_test: PASS
== build/san/tests/cli_test
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:133: check] Error 1
exit=2
```

Because `make check` stops at that failure, every sanitizer test binary was
also attempted serially with a private runtime directory. The source manifest
and result manifest both contain 86 suites; 67 passed and 19 failed
(M)[AC]: counts from `tests/*_test.c` and `build/edit-tn2-suite-results.json`,
with passed/failed obtained by comparing each recorded process status to
success (P). Full output is in `build/edit-tn2-suite-matrix.log` and per-suite
`build/edit-tn2-suite-<suite>.log`.

Both shell checks following the Makefile test loop pass when run separately:
`tools/test_replay_cli.sh` with the same leak-pass suppression, and
`tools/test_display_lock.sh contract`. The latter's deliberately timed-out
children are expected by its contract test.

## Bench observations

Power checked again: `Not charging` [AC]. `scroll_bench --track --probe`
completed successfully; its physical displayed-cadence result is UNAVAILABLE,
as its existing null-backend probe specifies. Evidence:
`build/edit-tn2-scroll-bench.log`.

`editor_large_bench --probe-three` completed the index/find/save fixture but
returned failure at `report_fg`: the probe's CLI-selected sample population
is below `BENCH_INTERACTION_MIN_N`. Its output says `mode=TRACK structural=FAIL`.
This existing qualification behavior is outside the bead and was not changed.
Its injected-to-submit observation is preserved in
`build/edit-tn2-editor-large-bench.log`; no performance claim is made from the
unqualified population.

## What remains / outside scope

The coordinator must rerun full checks with leak detection enabled and Xvfb
:99. Display suites fail inside this sandbox for lack of the X socket.
Failed display-dependent suites were `editor_test`, `editor_close_test`,
`file_kill_test`, `raster_test`, `refproto_test`, `refwin_test`,
`refwin_timeout_test`, `x11_clip_review_test`, `x11_clip_test`,
`x11_font_size_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`,
and `x11_stall_test`. For example, observed (M)[AC]:

```text
refproto_test: window=0x00000000 connect DISPLAY=:99: connection error=1
refproto_test:62: FAIL refproto_display_open(d,"refproto_test",true) == 0
```

Socket restrictions also block non-display IPC tests: `ipc_test`, `cli_test`,
and the IPC portions of `editor_large_test` and `editor_p4_test`. Observed
(M)[AC], with errno from the operating system (P):

```text
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK
editor_large_test:215: FAIL ipc_server_init(&server, runtime) == IPC_OK
editor_large_test:1225: FAIL mapped_startup_ipc() == 0
```

An additional non-display failure was found in `journal_kill_test`. Its cause
was not investigated or fixed; its linked binary contains none of the changed
scroll/large entrypoints. Observed output (M)[AC]; replay rc derives from
`JOURNAL_CALLBACK` in `journal_error` (P):

```text
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
```

Thus the requirement that every non-display suite pass is not fully met:
IPC is sandbox-blocked, and the unrelated journal-kill failure needs coordinator
triage. The tree builds. No bead implementation is left unfinished, no module
STATUS split was needed, and the retained scrollctl workaround is documented
as optional cleanup after unrestricted integration verification.
