# edit-zw9 worker report

## Result and files

Fixed wrapped adjacent-row queries stopping at blank lines. A newline row
advances its byte boundary without scanning a text cluster; the old progress
test therefore stopped wheel continuation early or returned an earlier blank
row during reverse lookup. Progress now follows the row boundaries, and LF
bytes count toward the existing scan bound. No allocation or new budget.

Final changed files:

- `src/layout/wrap.c`: row-boundary progress and LF scan accounting.
- `tests/editor_scroll_test.c`: independent tall-viewport oracle, wrapped
  prose/blank-line fixture, exact per-notch travel, far/near reversal,
  submitted top, unchanged selection and restored text-cell equality.
- `docs/decisions/edit-zw9.md`: design and fixture derivations.
- `docs/worker-reports/edit-zw9-s6.md`: this report.

Input, find panel and scroll controller code have no final change. Read the
matching landing decisions before investigating input. No `bd` or `git` was
run. Work stayed in this worktree; prohibited documents were not edited.

## Evidence and derivation

Supplied Astra observation (M)[AC]: line four at y = 240 pixels, text top at
y = 40 pixels, row pitch = 40 pixels. Therefore (240 - 40) / 40 = five visual
rows for three wheel notches, while three notches times `SCROLL_CORE_ROWS`
(three rows per notch) requires nine rows (P). The reported thirty-notch
forward/reverse pair should restore the preceding view. Those supplied
notch counts define this regression; no new duration or count was invented.
The review and recordings cited in the brief are absent from this worktree.

Test geometry reuses `PROBE_COLS` = 32 and `PROBE_ROWS` = 8 (E: existing
scroll fixture controls). (P) Maximum forward travel = (3 + 30) * 3 = 99
rows; oracle height and paragraph count = 99 + 8 = 107; paragraph byte
width = 2 * 32 = 64; document size = 107 * 64 = 6848 bytes. Each paragraph
ends with its LF and an empty line's LF (P). The taller viewport has the
same text width and document geometry, so its normally laid-out row starts
are an independent oracle for wheel queries. Comparison arrays reuse the
existing viewport's cell capacity (P: columns times rows).

All emitted fixture byte offsets and observations below are (M)[AC], read
directly from the fixture and its oracle; diagnostic source-line numbers and
exit codes are structural (P). Power was checked at execution:
`BAT0: Not charging` = [AC]. Toolchains are the installed gcc 13 release and
clang 18 ASan/UBSan required by the brief; patch-version strings are tool
identity, not budgets. Build parallelism comes from `nproc`, the available
CPU count, rather than a newly selected worker count.

## Red run

Before the implementation change:

```text
$ make build/san/tests/editor_scroll_test
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_scroll_test
editor_scroll_test: wrapped notch=1 top=60 expected=60
editor_scroll_test: wrapped notch=2 top=63 expected=83
editor_scroll_test:196: FAIL e->row_byte[0] == tops[(notch + 1u) * SCROLL_CORE_ROWS]
editor_scroll_test: null fractional/core wheel, overscan, immutable refresh/repeat/missing/stale correlation PASS
editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=0 guard=sanitizer-inert PASS
editor_scroll_test: fractional resize rebuilds clip/overscan and preserves origin PASS
editor_scroll_test: wrap-off EOF typing submits while bulk worker is held PASS
exit: 1
```

The original simple fixture passed; adding the copied-PRD-style empty
paragraph separators exposed the defect. The independent tall viewport
avoids deriving expected rows from the failing adjacent-query code.

## Green run

```text
$ make -j"$(nproc)" all
exit: 0
$ make build/san/tests/editor_scroll_test
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_scroll_test
editor_scroll_test: wrapped notch=1 top=60 expected=60
editor_scroll_test: wrapped notch=2 top=83 expected=83
editor_scroll_test: wrapped notch=3 top=127 expected=127
editor_scroll_test: wrapped near top=127 expected=127
editor_scroll_test: null fractional/core wheel, overscan, immutable refresh/repeat/missing/stale correlation PASS
editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=0 guard=sanitizer-inert PASS
editor_scroll_test: fractional resize rebuilds clip/overscan and preserves origin PASS
editor_scroll_test: wrap-off EOF typing submits while bulk worker is held PASS
editor_scroll_test: wrapped exact visual-row wheel and reversal PASS
exit: 0
```

The release regression also passed with the allocator guard active. Logs
are under `build/edit-zw9/`. The restored views compare equal text cells in
the null backend; this does not claim a native pixel capture.

## Full validation and what remains

The required release `make all` passed with the project's C11 warning flags.
The sanitizer `make check` built all suites, then stopped in `cli_test`:

```text
$ ASAN_OPTIONS=detect_leaks=0 EDIT_DISPLAY_LOCK_TIMEOUT=0 make check
== build/san/tests/cli_test
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:133: check] Error 1
```

The lock timeout override means no waiting for another worktree's display
runner (P: immediate attempt); no display-lock bypass was used. The earlier
attempt waited for the shared display lock and was interrupted before
retrying. The sandbox also rejects local AF_UNIX bind with
`PermissionError(1, 'Operation not permitted')`, which explains the CLI's
local IPC prerequisite failure before it can exercise a display.

LeakSanitizer fails in this sandbox with its documented ptrace restriction:

```text
LeakSanitizer has encountered a fatal error.
LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

Leak detection was disabled for the sanitizer reruns; ASan/UBSan stayed
enabled. The coordinator must run leak-enabled checks outside this tracing
environment. Native display suites fail here because the X socket is
unavailable; the coordinator must rerun them on Xvfb :99 (the display named
by the brief), including native pixel reversal acceptance.

The serial sanitizer sweep completed. (M)[AC], derived by counting `RESULT`
lines in `build/edit-zw9/headless.log`: 71 commands = 66 successful commands
plus 5 unsuccessful commands. Additional targeted cases are recorded in
`follow-up.log` and `p4-null.log`. Headless editor, editor scroll, editor
session, editor close, layout, wrap, view, scroll, synthetic display-scroll,
raster-scroll and the other socket-independent module suites passed.
GL's pure snapshot/completion checks passed with native diagnostics skipped.
The wrapped viewport command and the pointer/wrap/tab/resize null cases also
passed. File cancellation passed when run through its headless selector.

Failures left outside this bead's scope:

| Suite | Finding / remaining check |
| --- | --- |
| `ipc_test` | Local socket initialization fails; the direct bind probe confirms sandbox EPERM. |
| `editor_large_test` | Reaches `mapped_startup_ipc` after the preceding null large-file cases pass, then fails local socket initialization. Later cases need the coordinator rerun. |
| `editor_p4_test --headless` | Its shared IPC server fails to initialize. `--73j-null` separately passes the null pointer/wrap/tab/resize cases. |
| `file_kill_test` | Native viewport and queued-open/save display prerequisites fail. `FILE_KILL_CANCEL_ONLY=1` passes the headless cancellation cases. |
| `journal_kill_test` | Independent non-display replay assertion fails identically on a serial rerun; no journal code was changed. Coordinator investigation remains required. |

The journal failure is (M)[AC]; emitted trial/operation counts derive from
the existing kill fixture's shared progress fields, and the return code is
its replay API result (P):

```text
== build/san/tests/journal_kill_test
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
RESULT 1 build/san/tests/journal_kill_test
```

Thus a fully green non-display `make check` cannot be claimed: the local
socket restriction and the independent journal failure remain. Neither was
fixed outside the assigned bead. Other harnesses print expected forced-failure
diagnostics while testing timeout/error handling; their exit status passed.

## Layout benchmark

The existing wrapped-layout benchmark ran against the existing corpus, read
only, with these commands; both returned success (P: process exit status):

```text
build/bench/layout_bench /tmp/edit-corpus unicode.txt --wrap-only
build/bench/layout_bench /tmp/edit-corpus oneline_1g.txt --wrap-only
```

(M)[AC], `BAT0: Not charging`; shared-box load readings were 7.85 for Unicode
and 7.09 for prose, directly read by the benchmark from `/proc/loadavg`.
The times below derive from the existing benchmark's monotonic end-minus-start
samples and percentile calculation; units are nanoseconds.

| Existing wrapped workload | p50 (M)[AC] | p99 (M)[AC] |
| --- | ---: | ---: |
| Unicode full layout | 502110 | 824722 |
| Unicode typing row | 6087 | 12995 |
| Prose full layout | 443996 | 478526 |
| Prose typing row | 500924 | 539596 |
| Prose deep byte query | 264514 | 279299 |

Full-layout rows printed MISS against their existing tracked bound; the
benchmark exited successfully under the settled performance policy. No new
gate or performance bead was introduced. Full outputs are in
`layout-unicode-bench.log` and `layout-prose-bench.log` under `build/edit-zw9/`.
Native pixels, display suites, leak-enabled checks, socket-dependent cases
and the independent journal failure remain for the coordinator. This worker
stops after edit-zw9.
