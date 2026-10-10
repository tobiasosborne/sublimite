# edit-457.10b (P4.10) worker report, session 9

The large-file implementation from `wt/edit-457.10` is ported to current
multi-buffer main. Large state is per buffer; work completion routes by the
owner's work identity or file pointer/generation, and uses that owner's tree
and index. The eager mapped-file foreground line-count scan is removed.
Find and save retain the worker APIs and snapshot contracts. No new globals,
no dependency module changes, no Makefile changes, no git writes, no bd,
no HANDOFF/worklog edits. Design: ../decisions/edit-457.10b.md.

## Per finding / red-green

1. Missing large-file path after multi-buffer integration: ported
   tests/editor_large_test.c was installed before implementation. It covers
   mmap prefix estimates, byte goto, estimated-to-exact viewport correction,
   exact line conversion, snapshot find, edited save bytes, typing allocations,
   independent find/save state across tabs, and pending-job close/eviction.
   RED, command `make build/tests/editor_large_test`, exit 2:

```text
tests/editor_large_test.c:6:10: fatal error: editor/large.h: No such file or directory
    6 | #include "editor/large.h"
      |          ^~~~~~~~~~~~~~~~
compilation terminated.
make: *** [Makefile:51: build/rel/tests/editor_large_test.o] Error 1
```

2. Imported completion loss when the shared mailbox is full: a filler job
   saturates the mailbox before find completes. The test was added and failed
   before the fix. Warm/find now retry publication with cancellation polling.
   RED, release test, exit 1:

```text
editor_large_test:53: FAIL trace_now_ns() < deadline
editor_large_test:80: FAIL step_until(e, find_done, UINT64_C(500000000)) == 0
editor_large_test:140: FAIL mailbox_pressure(e, needle) == 0
```

3. Exact-index arrival during a busy view discarded the gutter redraw: a
   controlled busy-view regression was written before the fix. Publication
   now latches full_pending before deferring viewport correction.
   RED, release test, exit 1:

```text
editor_large_test:125: FAIL e->full_pending
```

4. Imported bench dimensions disagreed with main's default font: viewport
   dimension asserts were written before selecting the matching font size.
   RED, release bench, exit 1 (M)[AC], BAT0 Not charging, load1 8.88:

```text
editor_large_bench:79 failed: grid->dims.cols * grid->dims.cell_w == 2880
editor_large_bench:136 failed: open_ed(&e, &b, &r, path) == 0
```

5. Cooperative indexing changed index-before-warm ordering: a lease assertion
   was written before the fix. Warm snapshots/jobs are now prepared during open,
   and submitted without allocation after the owning index is published.
   RED, release test, exit 1:

```text
editor_large_test:102: FAIL !e->buffer->lg.warm_h.epoch
```

6. Prepared IPC buffers were absent from the central routing array: a small
   forced-mmap prepared-buffer test was written first, then a fresh rebuild
   demonstrated lost warm completion before installation. Warm/find leases now
   bind their mailbox handler directly to the owning buffer. Teardown unbinds,
   cancels and physically joins the leases before reclaiming the owner.
   RED, freshly forced release test, exit 1:

```text
editor_large_test:101: FAIL prepared->lg.warm_done
editor_large_test:169: FAIL prepared_completion(e) == 0
```

The earlier implementation run also caught a test expectation error: typing
splits the third NEEDLE, so the subsequent search counts two matches. That
assertion was corrected; the pre-edit search still asserts three.

GREEN, final release contract (M)[AC], exit 0:

```text
editor_large_test: ok
```

## Validation

Final `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0
make -B -j4 all check` exited 0 (M)[AC]. This freshly rebuilt the entire release
and sanitizer trees after the clock shift, using gcc 13.3 and clang 18.1,
with C11, -Wall -Wextra -Werror -Wshadow -Wconversion. Final GREEN:

```text
== build/san/tests/editor_large_test
editor_large_test: ok
check: 60 test binaries passed
test_replay_cli: all passed
```

The find/refwin fixture FAIL diagnostics are intentional negative self-checks;
the full driver exited zero. No ASan/UBSan error was reported.
The final forced editor module fuzz build/run exited 0 (M)[AC]:

```text
#2870 DONE cov: 11193 ft: 23916 corp: 118/490b lim: 8 exec/s: 47 rss: 355Mb
Done 2870 runs in 61 second(s)
```

This meets the 60 s duration (G). The host clock moved backward during the session, making old object timestamps lie in the
future. An initial “ok” after adding the prepared-buffer test was an old binary,
so it is excluded. `make -B` freshly rebuilt the test, reproduced the RED above,
and produced GREEN after the owner-binding fix. Final verification is forced
as well; make's clock-skew warning is an environment/timestamp warning, not a
C compiler warning.
The previous complete `DISPLAY=:99 EDIT_DISPLAY=:99
ASAN_OPTIONS=detect_leaks=0 make -j4 all check` exited 0 (M)[AC]:

```text
check: 60 test binaries passed
test_replay_cli: all passed
```

The first sandbox attempt failed to connect to Xvfb in cli_test; rerunning with
access to the existing :99 socket passed. Display environment was :99 throughout;
no :0 window was requested. LeakSanitizer is disabled because it cannot run
inside this sandbox; the coordinator must rerun with leaks on.

## Loaded measurements and comparison

All rows below are p50 / p99 in ms (M)[AC], loaded TRACK. Final-source
bench exited 0, with prefix glyph correctness and viewport dimension assertions
active. It is TRACK and intentionally does not turn noisy gate misses into an
exit failure. Raw final output: edit-457.10b-s9-bench-final.txt.

Final stamp: `STAMP (M)[AC] BAT0=Charging load1=31.18 loaded TRACK shared box`.
s8 is a historical comparison, not a paired timing comparison: its branch used
the old single-buffer editor under different load. The final foreground typing
rows contain zero allocations (M)[AC], against zero (G).

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

The warm-order variants ran back to back on the same loaded box. Raw prior
variant: edit-457.10b-s9-bench-eager-warm.txt; deferred variant:
edit-457.10b-s9-bench.txt. These runs preceded the prepared-buffer receiver fix;
the final-source run above includes that fix.

Eager warm G7: 485.215 / 491.677 ms (M)[AC], 6.91 load1; deferred warm G7: 275.097 / 304.165 ms (M)[AC], 9.49 load1.
This supports retaining the original index-before-warm ordering; it does not
certify a timing gate. No gate pass is claimed from either report's rows.

Final exact log lines: 8947842 (M)[AC], estimate 8947713 (E, recorded at first
viewport). Final find count: 298542 (M)[AC]. Save status is zero and combined
completion also requires exact index publication; its time is tracked in raw
output. G5 sparse and G6/G7 log timings remain above their references, and the
final foreground rows include misses; acceptance remains unproven on this
loaded box. No retry was made to seek a quiet window.

## Known edit-czn G1 issue, unchanged in scope

A diagnostic rebuilt main's original editor.c and buffers.c read-only via
`git show` in build/large-baseline, using the current private structure layout
and the same unchanged dependency objects. It compared that eager-open logic
and the port back to back with the same null viewport, exact index and target
line floor(0.9 times 8947842) = 8053057. No journal was enabled. The port waited
for warm completion as well. Requested eight alternating insert/backspace keys
per variant; both exceeded the diagnostic's 40 s settle deadline (G, harness
limit). Raw output: edit-457.10b-s9-g1-pair.txt.

Main completed seven keys at 1.810–6.748 s (M)[AC], load1 18.31, then timed out;
the port completed its first key at 2.767 s (M)[AC], load1 22.34, then timed out.
This is persistence of the known far-line layout stall, not a performance
improvement claim. It was not fixed. A separate existing editor_bench null row
also recorded slow keys and zero typing allocations before the sandbox blocked
its native backend. No timing gate verdict is drawn from these probes.

## Missing / limits reported, not fixed

- Final foreground/native G3/G2c gate certification remains outside these null
  backend loaded TRACK rows. G5/G6/G7 and foreground timing acceptance cannot
  be certified from a single noisy run; all final rows are reported explicitly.
- The known edit-czn far-line typing stall persists in both variants.
- The imported synthetic anchor for a byte jump inside a line longer than its
  backward probe remains; sparse_10g is exercised for open/first viewport/index.
- Find/save are APIs, with no main.c keybindings or new gutter estimate marker.
  Saved-marker/journal transaction integration remains the existing separate
  UI/controller work; this port verifies snapshot save contents/completion.
- Typing may cancel an unfinished index and continue with estimates while
  main's existing bounded refresh runs. No dependency index policy is changed.
- One physical bulk worker remains; index/find/save are queued job types.

Implementation scope closed before the minute-30 cutoff (G). Final build,
full suite and fuzz verification are complete. The timing/native/UI limitations
above remain explicit missing acceptance items; the port does not certify bead
closure. No further implementation scope was added after the cutoff.
