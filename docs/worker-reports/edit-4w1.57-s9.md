# edit-4w1.57 — session 9 worker report

Scope: bead edit-4w1.57, `docs/reviews/P1-1.md` §5 and §12 only.
Read CLAUDE.md, the bead, the named review sections, perf §0.2 and the lineidx
status before implementation. Git was read-only; no bd, HANDOFF.md or worklog
edits. All display execution used DISPLAY=:99 EDIT_DISPLAY=:99.

## §5 — combined mmap ownership

Done. The regression opens the supplied `/tmp/edit-corpus/sparse_10g.bin`
through the production file lifecycle in mmap mode, attaches a real piece tree,
and retains independent index, find and save snapshots. It reserves actual find
result/program/scratch allocations, compiles a regex, then pauses the real index
worker inside its snapshot span callback. ASan's live allocation census measures
all opaque file/piece/find/index malloc ownership once, including temporary
piece-builder allocations through an instance-local counting allocator, excluding clean
file-backed mapping bytes as required by G10f. It asserts the acquisition,
attached-consumer and queued-build peak, ownership through logical cancellation,
snapshot survival after file/tree destruction, and return to the baseline.
Worker scanning and bounded adoption retain these preallocated arrays and do
not allocate additional build storage; the held job therefore covers the build
storage needed through normal completion as well as cancellation.
The ordinary sanitizer suite runs this test; the release binary explicitly
reports that the allocator census requires the sanitizer build.

Written and run before the production fixes:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/lineidx_test --review=combined
G10f combined sparse_10g.bin (M)[AC]: open=1063432 attached_find_save=4284568 index_queued=9585864 peak=9585864 gate=7242880 (G)
tests/lineidx_test.c:354: FAIL peak <= gate
lineidx_test: 1 failure(s)
exit=1
```

Fix: build scratch uses an immutable uint16 length-minus-one array and uint32
result array, with a scalar 64-bit continuation offset. The count has sufficient
bits for an entire LF-only chunk; nonascii and built flags are separate. Empty
sources, partial scans, built-chunk skips and worker seeks preserve the existing
semantics. Scratch ownership accounting includes both active and retiring jobs.
Successful mmap `file_attach` frees the acquisition prefix and makes subsequent
prefix queries borrow the full mapping with the same length. Previously borrowed
prefix pointers expire at this documented handoff. The file.c change is confined
to attachment, away from fault/readiness paths owned by edit-4w1.55.

Green:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/lineidx_test --review=combined
G10f combined sparse_10g.bin (M)[AC]: open=1063432 attached_find_save=3235992 index_queued=6898888 peak=6898888 gate=7242880 (G)
lineidx_test: ok
exit=0
```

The mmap gate is `32 * 163840 + 2000000 = 7242880` bytes (G). Measured peak
is 6,898,888 bytes (M)[AC], including conservative maximum find storage, below
that fixed ownership limit. This is an allocation census, not a timing verdict.
The fixture is read-only. No new globals or typing-path allocations were added.

## §12 — valid pending publication

Done. A benchmark self-check fills a mailbox with foreign messages followed by
a valid index range, waits for both workers' physical completion without UI
adoption, and invokes the production benchmark waiter. With a full mailbox,
bounded selective receive initially has no staged index results. The old waiter
rejects the completed worker although its result remains deliverable.
The continuation worker can publish more ranges than a naive chunks-per-range
calculation predicts, so the final fixture uses a single LF-only chunk behind
foreign traffic to make full-mailbox completion deterministic. The range also
checks that compact counts preserve a full chunk of newlines.

Written and run before the waiter fix:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/lineidx_bench --self-check=pending
valid pending batches: completion failure prefix=0/1 complete=0 building=0 deadline_expired=0
SELF_CHECK pending FAIL completed=1 pending_before=256 adopted=0 exact=0
exit=1
```

Fix: after physical completion, the waiter continues bounded drain/poll slices
while mailbox traffic remains. It fails at the existing deadline, or when the
index is incomplete, physical/staged work has stopped and no messages remain.
The final-publication race recheck is retained. The new row is part of
`--self-check` and can also run as `--self-check=pending`.

Green:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/lineidx_bench --self-check=pending
SELF_CHECK pending PASS completed=1 pending_before=256 adopted=1 exact=1
exit=0
```

Full benchmark self-check also exits zero (M)[AC]: fresh jump/oracle/cold-refusal,
invalid workloads, stranded work/retirement deadlines, final-publication race
and pending-result adoption all pass. Expected stranded-case diagnostics are
negative-test output, not failures.

## Validation

- `make all`: exits zero (M)[AC], gcc 13.3.0 release, C11 and the required
  -Wall -Wextra -Werror -Wshadow -Wconversion flags.
- Full `make check`: exits zero (M)[AC], all 59 test binaries plus replay CLI
  checks passed. The first sandbox run failed because local sockets prevented
  access to existing Xvfb :99. The authorized rerun uses local socket access
  and clang 18.1.3 ASan/UBSan. The final rerun after strengthening transient
  piece ownership counting also exits zero (M)[AC].
- `ASAN_OPTIONS=detect_leaks=0` was used throughout sanitizer/fuzz execution.
  LeakSanitizer cannot run inside the sandbox; the coordinator must rerun with
  leaks enabled.
- `build/fuzz/lineidx_fuzz -max_total_time=60`: exits zero (M)[AC], requested
  60 seconds (G), completed 17,146 executions in 61 seconds (M)[AC]. No sanitizer
  findings. Additional fixed seed inputs exercise full LF chunks, an empty
  worker seek and edited/built geometry followed by seek; they also exit zero.
- Release allocation guard:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/lineidx_test --review=alloc
lineidx typing: keys=10000 allocations=0 guard=active
lineidx_test: ok
```

- Full release `build/tests/lineidx_test`: exits zero (M)[AC], including
  byte/line models, edits, seek, mailbox and live-memory guards.
- `git diff --check`: clean. No other module production fixes.

Final full-suite green transcript:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 check
check: 59 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
exit=0
```

## Back-to-back TRACK comparison

The worker scan path changed, so the old lineidx implementation was compiled
from read-only `git show HEAD:src/lineidx/lineidx.c` into a build-only alternate
library. Both variants use the fixed waiter and identical benchmark executable
source. They ran consecutively in the same minute on the supplied log fixture,
with three measured repetitions (M)[AC], while other work was active. Power was
Charging [AC]; load stamps ranged from 16.79 to 14.82 (M)[AC]. Neither run claims
latency gate acceptance; both exit zero in TRACK mode.

| Row | Old scratch p50 / p99 ns (M)[AC] | Compact scratch p50 / p99 ns (M)[AC] |
|---|---:|---:|
| G7 warm | 148374796 / 151837258 | 147060248 / 246024975 |
| Prefaulted build | 101068093 / 138882619 | 108986119 / 179219311 |
| Fresh unindexed jump + null viewport | 145272287 / 153316013 | 147065755 / 155353924 |
| Partial worker seek + null viewport | 107885713 / 115524823 | 74461525 / 86600506 |

The spread supplies no reliable timing winner or gate verdict. The decision is
based on deterministic ownership reduction and preserved correctness. Cold and
real-display latency acceptance remain unvalidated.

## Decisions and remaining work

Design details are in `docs/decisions/edit-4w1.57.md`. Both named findings are
implemented. Leak-enabled validation remains coordinator work. The memory test
holds save/find snapshots and reserves their state; it does not perform a full
10 GiB save or search. It covers the maximum allocated build geometry while
queued/paused and retiring, rather than scanning the full fixture to completion.
Other P1-1 findings are outside this bead and were not fixed.

Environment observation outside scope: the system wall clock moved backward
late in verification. The final `make all` was a no-op, exited zero, and emitted
`Clock skew detected. Your build may be incomplete.` The earlier complete
release rebuild after the final source/test changes exited zero, and the final
full sanitizer rerun passed. No source changes followed those builds; only the
report and status documentation changed. Clock/timestamp repair was not attempted.
