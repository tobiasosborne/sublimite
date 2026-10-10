# edit-zzj.13 — session 9 worker report

Partial continuation. Done: findings **10, 15, 17**; **22 adopted from main and
covered by an editor failure regression**. Finding **11 has corrected accounting
only and remains open**. Remaining: **8, 9, 11, 13, 16, 21**. Finding 14 was
withdrawn before this session and was not changed.

Production changes stay in `src/editor/`; regressions are in
`tests/editor_test.c`. No dependency module, main, HANDOFF, worklog, git state
or bead database was modified. No new globals. Decisions are recorded in
`docs/decisions/edit-zzj.13.md`. Final verification and the remaining work are
recorded below. No pending worker action is required to understand this report.

## Findings and remaining work

| Finding | Result and scope |
| --- | --- |
| 8 | Remains. New opt-in stalled-remainder regression is red. First viewport still waits for bulk open. A safe fix needs a pending-open/prefix ownership and adoption state; deleting the count call would merely move lazy scanning into view queries. No partial production open change was made. |
| 9 | Remains, no new test/fix. `remember()` still invokes the traversing `undo_get_stats`. Bounded group/eviction information requires an undo API amendment outside the named files. Editor access to private undo record encodings was rejected. Full-history performance acceptance is missing. |
| 10 | Done in the editor update path. `changed()` no longer adopts completed results before cancelling them. A completed non-ASCII backlog regression fails first, then passes. Beginning edits against an unbuilt maximum-size index model exercise the actual editor metadata path. Existing lineidx relative offsets/bounded polling handle suffix maintenance; no dependency change. |
| 11 | Partial, remains. Complete mutation/action plus staging, blink/resize and compose/submit intervals now enter `longest_slice_ns`. A deliberately indivisible submit was missing from the old maximum. Resumable mutation/newline counting/replay/resize and bounded validation remain missing; no 0.5 ms (G) preemption claim. |
| 13 | Remains. Existing opt-in burst test still fails: rapid `abc` undo removes only the last key. Automatic grouping, timeout/focus/movement/repeat coverage and a group-aware model are missing. |
| 15 | Done. Per-buffer exact-size recycling storage retains trimmed slabs/checkpoints/temporary allocations for reuse. A mutex protects the arena and returned blocks for snapshot-thread compatibility. The repeated replacement undo/redo regression bounds arena high-water growth. |
| 16 | Remains, no new test/fix. Watch routing, focus/bulk identity checks, stale-source suspension and reload/keep UI are missing. |
| 17 | Done. Once an ordinary due pump schedules a journal job, its completion mailbox clears the editor's waiting state. Active and queued jobs no longer cause expired-deadline polling. Work-pool admission BUSY still retries; it does not prove job admission. |
| 21 | Remains, no new native allocation-attribution test/fix. Existing release typing guards remain active, but wholesale native IO suspension still excludes translation before dequeue. |
| 22 | Adopted. Main's undo atomic group transaction already restores failed groups. New editor allocator sweep checks bytes, view, history and journal recovery. Red links the prior undo source from `92ca926^` in build-only artifacts; green uses current main undo. No undo module was edited. |

## Pasted red and green evidence

All runs use `DISPLAY=:99 EDIT_DISPLAY=:99` where a display is required.
Power was AC; BAT0 initially reported Not charging, later Charging. The box was
loaded. Memory/metadata/wake counts below are (M)[AC]; bounds are (G), and no
single timing is a performance gate verdict.

### Finding 8 — still red

Command: `EDITOR_REVIEW_ONLY=8 build/tests/editor_test`; exit 1.

```text
editor_test:603: FAIL first_before_remainder
```

The test reserves an independent prefix lane and stalls bulk work until a
bounded watchdog releases it. The first submitted viewport follows that
release. There is no green run or claimed fix for finding 8.

### Finding 10

Red: `EDITOR_REVIEW_ONLY=10 build/tests/editor_test`; exit 1 before removing
the mutation-path poll.

```text
editor_test:625: FAIL !lineidx_any_nonascii(e->buffer->index)
```

Green: same command, exit 0.

```text
review 10: 10 GiB beginning edits metadata visits max=542 (M)[AC], bound=4096 (G)
review 10: mutation cancels completed backlog without adopting it passed
```

The large-size case is a metadata model, not a whole-file latency benchmark.
For edit-zzj.16: index dirty/refresh/seek behavior is unchanged; this session
does not repair its index lifecycle. For edit-czn: the paired existing G1
benchmark still hits its settle deadline at the published 0.9-lines position
on `/tmp/edit-corpus/log_1g.txt`; no G1 pass or cause attribution is claimed.

### Finding 11 — accounting only

Red: `EDITOR_REVIEW_ONLY=11-accounting build/tests/editor_test`; exit 1.

```text
editor_test:688: FAIL reported >= observed
review 11: indivisible submit observed=2078303 reported=2683 ns (M)[AC]
```

Green: same command, exit 0.

```text
review 11: indivisible submit observed=2059897 reported=2069161 ns (M)[AC]
```

The paired runs prove omitted accounting, not timing acceptance. Indivisible
work still exceeds the slice contract; finding 11 remains open.

### Finding 13 — still red

Command: `EDITOR_REVIEW_ONLY=13 build/tests/editor_test`; exit 1.

```text
editor_test:562: FAIL editor_length(e) == 0
```

No green run or claimed grouping fix.

### Finding 15

Red: `EDITOR_REVIEW_ONLY=15 build/tests/editor_test`; exit 1 with bump storage.

```text
editor_test:648: FAIL growth <= 65536
review 15: replay arena growth=3772416 bytes (M)[AC], bound=65536 bytes (G)
```

Green: same command, exit 0 with recycling storage.

```text
review 15: replay arena growth=0 bytes (M)[AC], bound=65536 bytes (G)
```

Storage classes are exact sizes; original/add data are not rounded to larger
powers of two. Lookup is bounded by 64 classes (G); class exhaustion returns
NOMEM. Returned storage remains in its class until buffer destruction.

### Finding 17

Red: `EDITOR_REVIEW_ONLY=17 build/tests/editor_test`; exit 1 before waiting-state
integration.

```text
editor_test:699: FAIL polls <= 4
review 17: stalled sync idle polls=29 (M)[AC], requested-timeout bound=4 (G)
```

Initial green: same command, exit 0.

```text
review 17: stalled sync idle polls=2 (M)[AC], requested-timeout bound=4 (G)
```

The final regression additionally covers a journal queued behind held bulk
work. A build-only variant using `HEAD:src/editor/editor.c` keeps the old polling
loop while retaining the new allocator and tests. Red, exit 1:

```text
editor_test:728: FAIL polls <= 4
editor_test:728: FAIL polls <= 4
review 17: active job idle polls=30 (M)[AC], requested-timeout bound=4 (G)
review 17: queued job idle polls=29 (M)[AC], requested-timeout bound=4 (G)
```

Current editor green, exit 0:

```text
review 17: active job idle polls=2 (M)[AC], requested-timeout bound=4 (G)
review 17: queued job idle polls=2 (M)[AC], requested-timeout bound=4 (G)
```

### Finding 22

The regression was written before adopting the existing atomic undo behavior.
Build-only legacy variant: `git show 92ca926^:src/undo/undo.c` compiled with the
current header and linked ahead of the normal archive. Only undo was replaced.
Red: `EDITOR_REVIEW_ONLY=22 build/s9-legacy-editor-test`; exit 1.

```text
editor_test:763: FAIL editor_length(e) == 1 && editor_read(e, 0, &current, 1) == 0 && current == 'x'
```

Green: `EDITOR_REVIEW_ONLY=22 build/tests/editor_test`; exit 0.

```text
review 22: allocator positions=22 failures=22; tree/view/history/journal rollback passed (M)[AC]
```

Each admitted failure leaves the replacement byte, selection, viewport,
visible grid cells, history cursor, line total and journal record count
unchanged. Explicit flush/close replay
recovers that same byte. Successful sweep positions recover the restored
original. The per-buffer allocator seam creates no process global.

## Verification and limitations

Strict GCC 13 release `make all`: exit 0. First full Clang 18 ASan/UBSan
`make check`: exit 0, 59 test binaries plus replay CLI (M)[AC]. The second full
campaign also exits 0. Clock-skew timestamp reuse left the older editor-test
wording in those logs, so the freshly forced sanitizer editor binary was also
run directly in full; it includes queued jobs, unique fixtures and visible-grid
replay assertions and exits 0. LeakSanitizer is disabled
with `ASAN_OPTIONS=detect_leaks=0`; the coordinator must rerun with leaks on.
Native/socket tests run with approved sandbox escalation because the sandbox
hides Xvfb :99 and Unix sockets. No display :0 was opened.

Editor fuzz: 61 seconds (M)[AC], against 60 seconds (G), exit 0:

```text
Done 16114 runs in 61 second(s)
```

No unrelated module fix was made. While preparing the replay fixture, a larger
untitled initial buffer exceeded journal setup admission and made editor_open
fail. The fixture was reduced to the existing supported size; large initial
journal registration is reported outside this session's fixes.

First whole-suite green:

```text
check: 59 test binaries passed
test_replay_cli: all passed
```

Final release editor suite, exit 0, with active guards (M)[AC]:

```text
editor_test: requested=gl actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: requested=raster actual=cpu-raster 10000 keys mallocs=0 guard=active
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: native X11 translation, editor loop and WM close passed
editor_test: all passed
```

The sanitizer guard is ASan-inert by design; the release counts establish the
existing mutation/layout/submit contract. They do not close finding 21.

Paired allocator TRACK benchmark command:
`--track --no-idle --no-p4 --keys=1000`, `EDIT_BACKEND=raster`. The baseline
compiles `HEAD:src/editor/buffers.c` as a build-only object ahead of the current
archive; the current variant uses recycling. Both runs exit 1 during the null
reference, before native rows or measured typing samples:

```text
baseline: STAMP (M)[AC] BAT0=Charging load1=7.50 TRACK shared box
current:  STAMP (M)[AC] BAT0=Charging load1=7.30 TRACK shared box
POSITION null lines=8947842 target_line=8053057 byte=966366840 index=published A=2880x1800 workload=alternating_insert_backspace
editor_bench:74 failed: bench_now_ns() < deadline
editor_bench:176 failed: settle(e) == 0
```

The declared A-size geometry is inherited benchmark output, not re-certified
here. This supports reporting the unresolved edit-czn workload, not a latency
regression verdict. Recycling was selected on the paired memory-bound
regression, not noisy single-sample timing.

The environment clock jumped backward during final verification, producing
Make clock-skew warnings. `make -B -j4 all build/san/tests/editor_test
build/fuzz/editor_fuzz` exits 0: all release targets and the sanitizer/editor
fuzzer binaries were explicitly rebuilt rather than relying on timestamps.
Compiler flags remain C11, `-Wall -Wextra -Werror -Wshadow -Wconversion`.

No scope was added after the cutoff. Final forced-binary release editor
verification exits 0, retaining all active zero-allocation typing assertions.
The strengthened visible-grid/viewport replay sweep also exits 0 under
ASan/UBSan with leaks disabled:

```text
editor_test: native X11 translation, editor loop and WM close passed
editor_test: all passed
review 22: allocator positions=22 failures=22; tree/view/history/journal rollback passed (M)[AC]
```

Final `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check`
also exits 0:

```text
check: 59 test binaries passed
test_replay_cli: all passed
```

Both complete sanitizer campaigns pass, as do the forced release build,
final release editor suite and final focused sanitizer replay sweep. The
entire freshly forced sanitizer editor suite also exits 0:

```text
review 17: active job idle polls=2 (M)[AC], requested-timeout bound=4 (G)
review 17: queued job idle polls=2 (M)[AC], requested-timeout bound=4 (G)
review 22: allocator positions=22 failures=22; tree/view/history/journal rollback passed (M)[AC]
editor_test: native X11 translation, editor loop and WM close passed
editor_test: all passed
```

The clock-skew warning is from the backward environment clock change; the
forced build explicitly recompiles all requested targets. `git diff --check` is clean.
The coordinator must rerun LeakSanitizer with leaks enabled.

The bead remains open: **8, 9, 11, 13, 16 and 21 are unfinished**. Finding 11's
accounting change does not close its timing/preemption requirement. There is
no claimed G1 latency gate result. No background worker action remains running
for this session.
