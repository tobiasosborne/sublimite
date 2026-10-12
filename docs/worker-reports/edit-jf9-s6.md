# edit-jf9 worker report (s6)

Implemented byte-relative unwrapped arrows/PageUp/PageDown during lazy indexing.
The resident scroll path owns estimated viewport labels and existing index
publication relabels them. View's exact line/query caches never receive estimated
ordinals. No bd or git command was run; edits stay in this worktree. The decision
is recorded in `docs/decisions/edit-jf9.md`.

## Files touched

- `src/view/view.h`: relative-motion API and caller-owned continuation state.
- `src/view/view.c`: cooperative LF-boundary traversal, exact grapheme columns,
  selection and horizontal follow, cancellation and normalization reset.
- `src/editor/input.c`: lazy/inexact unwrapped motion dispatch.
- `src/editor/scrollctl.c`: avoid adopting an unavailable exact `v.line` when
  index completion races relative motion.
- `tests/editor_large_test.c`: held-index page regression and headless motion
  oracle; compare arrows/pages, extension, boundaries, tabs, CRLF, ZWJ,
  cooperative long-line cancellation/restore and allocation-free motion.
- `docs/decisions/edit-jf9.md` and this report.

## Bound and measurement derivation

Quoted from `docs/decisions/edit-91c.md`:

> G5 warm, column A: open request -> correct first viewport = 64 KiB read + full frame, PB 1.025 ms (E), gate 6 / 9 ms p50/p99 (G).

The resident-window read is the same `SCROLL_SCAN_BUDGET` read (P), followed by
the same full frame; the existing warm-read plus frame estimate is derived in
`perf/01-perf-target.md` section 2.4. The first held-build PageUp uses the existing
p99 bound as requested by this bead. It introduces no allowance. Other page
measurements retain the fixture's TRACK policy. These are sanitizer/null-renderer
measurements, not release GPU certification.

BAT0 was `Not charging`, hence [AC], checked before the timing runs. Input-to-submit
is the submit hook timestamp minus the injected key's `t0_ns` (M)[AC]. Raw transcript
line numbers, enum values and return statuses are diagnostic identifiers (P:
compiler source locations, declared API enums and process return codes).

The headless prefix size is 28,670 bytes (P): floor(((sizeof line + initial unit)
× VIEW_SCAN_BOUND) / payload length) × payload length, with the CRLF fixture's
payload length obtained from `sizeof line - terminator unit`. The original exact
path examines that prefix. The green charged work is 2,560 bytes (P): configured
page rows plus origin and following proofs, each using the existing query quantum;
byte-column and destination scans at the empty EOF column consume no extra work.
Both use the existing 4,096-byte `VIEW_SCAN_BOUND` (P: existing per-call work bound).

## Red runs (verbatim)

The test was written and run against ordinary view before implementation:
`ASAN_OPTIONS=detect_leaks=0 EDIT_JF9_VIEW=1 build/san/tests/editor_large_test`
(failed at its local-work assertion).

```
editor_large_test:1142: FAIL scanned <= VIEW_SCAN_BOUND
edit-jf9 relative PageUp: rc=0 scanned=28670 bound=4096 (P)=VIEW_SCAN_BOUND
```

The held-index editor baseline was additionally reproduced by temporarily
routing the editor motion through ordinary `view_command`, then restoring the
fixed dispatcher. The headless helper still used the relative API during this
baseline rerun. `EDIT_91C_STARVED=1` selects the held-build sequence:

```
edit-91c stalled: hold=1 action=1 scroll_pending=1 resident_pending=0 windows=2/0 view_busy=1 phase=10 query_pos=580352 query_target=1073741824 index_complete=0 (M)[AC]
editor_large_test:1082: FAIL editor_get_stats(e).submitted_sequence >= sequence
edit-jf9 relative PageUp: rc=0 scanned=2560 bound=4096 (P)=VIEW_SCAN_BOUND
edit-jf9 headless relative motion: ok
```

It misses the first-PageUp frame bound while view is still resolving the exact
EOF query and the index is incomplete. During edge-case development, additional
red tests caught Left at a line start, a restore retaining relative mode, and
boundary no-ops losing preferred column; their fixes are included.

## Green runs (verbatim)

Final sanitizer held-build run:

```
edit-jf9 relative PageUp: rc=0 scanned=2560 bound=4096 (P)=VIEW_SCAN_BOUND
edit-jf9 headless relative motion: ok
edit-jf9 first_page_submit_ns=776045 (M)[AC]=submit-t0 bound_ns=9000000 (G)=G5 warm A p99
edit-91c hold=1 after_index=0 ctrl_end_submit_ns=1104182 worst_page_submit_ns=776045 (M)[AC]=submit-t0 budget_ns=6000000/9000000 (G)=G5 warm A p50/p99, 64 KiB read + full frame, PB 1.025 ms (E) index_complete=0->0 pages=79 gate=TRACK
edit-91c bulk-held during build follows outside laid-out rows: ok
```

Final real-index run:

```
edit-jf9 relative PageUp: rc=0 scanned=2560 bound=4096 (P)=VIEW_SCAN_BOUND
edit-jf9 headless relative motion: ok
edit-jf9 first_page_submit_ns=875060 (M)[AC]=submit-t0 bound_ns=9000000 (G)=G5 warm A p99
edit-91c hold=0 after_index=0 ctrl_end_submit_ns=1310974 worst_page_submit_ns=913524 (M)[AC]=submit-t0 budget_ns=6000000/9000000 (G)=G5 warm A p50/p99, 64 KiB read + full frame, PB 1.025 ms (E) index_complete=0->0 pages=79 gate=TRACK
edit-91c index-building follows outside laid-out rows: ok
```

The traversal count in those transcripts is derived by the existing tail oracle:
EOF-window line starts divided by configured page rows, plus the pages needed to
cross the resident-window boundary (P). The test independently checks every
cursor/anchor byte and submitted top against the tail's certified LF starts.
The bulk handle remains unfinished throughout the held run. The first PageUp
submits inside the requested G5 bound without index publication.

Release `EDIT_JF9_VIEW=1 build/tests/editor_large_test` also passes its active
allocator guard: zero allocations (P: the motion uses only existing view scratch;
(M)[AC]: the guard assertion during each motion). ASan owns malloc, so its allocator
hook is intentionally inert; the release run supplies that verification.

## Build and suite results

`make all` passes with gcc release and the tree's C11 warning flags. Sanitizer
binaries build with clang and ASan/UBSan. LeakSanitizer must be disabled in this
sandbox: its default teardown reports that it cannot run under ptrace. ASan and
UBSan remain enabled in every reported sanitizer run.

- `view_test`, `scroll_test`, `editor_scroll_test`: PASS on final source.
- `editor_large_test`: the headless regression, held-index/after-index/real-index
  page sequences, near-end and remaining lazy-file cases pass. Full suite stops
  at `mapped_startup_ipc`: `ipc_server_init` is denied by the sandbox.
- `editor_test`: initial run hit the find/replace error-injection assertion;
  a serial rerun passed that assertion and advanced to display-dependent opens,
  which fail here. The explicit `--backend-only` check also fails at editor open.
- `make check`: all sanitizer binaries build, but the driver stops at `cli_test`
  when its IPC server bind is denied. The normal shared display lock was already
  held and eventually timed out. An additional sandbox-only run disabled its wrapper via
  `DISPLAY_TEST_SAN=` after confirming X socket connection itself returns
  `PermissionError: Operation not permitted`. This does not claim a full pass.
- The non-display sweep runs every binary outside the Makefile's display group,
  plus the replay CLI shell test. Everything passes except the failures detailed
  below. Source changes after that sweep affect only relative motion; the final
  view/scroll/editor-scroll and headless motion suites were rerun.

Actual X-dependent suites fail inside this sandbox because the X socket is
inaccessible. The coordinator must rerun display suites on Xvfb :99, and rerun
IPC-dependent suites where Unix socket creation is allowed.

## Outside scope, reported without fixes

- `file_kill_test` is absent from the Makefile display group but its viewport and
  queued-open/save checks create a display; those fail here at viewport init and
  child status. This is a display dependency, not a relative-motion failure.
- `ipc_test` fails at server init with `errno=EPERM` from the sandbox.
- `journal_kill_test` reproducibly fails in its first kill-before-pump replay.
  Its WINDOW callback still expects the legacy record shape; `journal_set_window`
  now writes `JOURNAL_WINDOW_BYTES` and chosen-size flags. Neither journal nor
  that fixture was changed by this bead.
- The initial `editor_test` find/replace error-injection failure was intermittent:
  the serial rerun passes it. Reported for the coordinator; no change made.
- `large_doc_end` still clears horizontal scroll. Exact visual column discovery
  on a long last line needs scanning/continuations, not a one-line assignment;
  the requested conditional fix therefore remains separate.
- Copy-mode foreground-prioritized index work sharing the foreground window lane
  remains as reported by edit-91c. No lane/index-build policy change here.
- Very long individual lines can still require many bounded continuations for
  exact grapheme columns. The removed wait is the unrelated file-prefix scan.

## Benchmarks

`build/bench/view_bench` and `build/bench/scroll_bench --track --probe`
complete on [AC], using their existing workload and TRACK derivations. View's
reported allocation guards pass. Scroll reports its CPU proxy separately from
unavailable physical display cadence; no physical-display claim is made.

`build/bench/editor_large_bench --track` stops producing output after its first
sample's attribution. It was interrupted, so it supplies no complete result.
That benchmark uses default wrapping and a broader open/find/save workload;
no additional path was changed to make it finish. This remains a coordinator
investigation, separate from the passing unwrapped held-index motion regression.
Raw benchmark logs remain local build artifacts under `build/jf9/`.

## What remains

The implementation and requested regression are complete. Full green verification
requires the coordinator's socket-enabled reruns and triage of the unrelated
journal kill oracle. The horizontal Ctrl+End caret issue remains outside the
conditional one-line allowance. No further bead work is undertaken.
