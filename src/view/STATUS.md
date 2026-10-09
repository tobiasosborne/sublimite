# edit-zzj.10 — P3.2b finishing and current-main port

## Coordinator-main fix-up

Fixed the wrapped visual-motion regression reported after the coordinator rebase.
Preferred-column discovery cleared origin affinity before re-entering row lookup;
`visual_command` now uses saved `before_state.visual_end` for that origin.
Destination affinity and bounded column scanning retain their existing behavior.
No public header, layout implementation, wrap-suite expectation, or typing
allocation path changed.

New `view_test wrap-selection` coverage checks anchored Shift+Up/Down, reversal,
fresh preferred-column discovery with a backward selection, soft endpoints and
hidden separator whitespace. Expanded `wrap-port` checks long-cluster column
continuations from a trailing endpoint, cancellation/retry and anchored reversal;
its existing edited-cancellation fallback assertion remains. Adjacent rows are
cached to check exact visual motion. Earlier review regressions still pass.

Design and red/green evidence: [P3.2b.md](../../docs/decisions/P3.2b.md), with
the wrap integration note in [P4.1.md](../../docs/decisions/P4.1.md).

Required safe-display commands remain the commands below; focused entry points:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/view_test wrap-selection
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/view_test wrap-port
```

Fix-up release verification: (M)[AC], BAT0=Not charging, load1=2.10:

```text
wrap_selection: anchored Shift+Up/Down preserves soft-row affinity and preferred column
wrap_port: long visual-column continuation, anchored soft-end motion and cancellation passed
view_test: all passed
make all: exit 0
make fuzz: exit 0
fuzz: 23 fuzzers built
```

Unchanged release `wrap_test`: all passed, (M)[AC], Not charging, load1=4.53.
Full sanitizer-suite verification: (M)[AC], Not charging, load1=1.69 before
launch, with socket access to Xvfb :99 and `ASAN_OPTIONS=detect_leaks=0`:

```text
wrap_selection: anchored Shift+Up/Down preserves soft-row affinity and preferred column
wrap_port: long visual-column continuation, anchored soft-end motion and cancellation passed
view_test: all passed
wrap_test: all passed
make check: exit 0
check: 45 test binaries passed
test_replay_cli: all passed
```

The sandbox-only attempt stopped because the Xvfb socket is not visible there.
The socket-access run completed the entire suite, including live raster/X11;
the raster test's long wait also completed successfully. Separate focused
sanitizer view/wrap runs passed, (M)[AC], Not charging, load1=8.85. Leak-enabled
verification remains the coordinator's.

Fix-up view-fuzzer campaign: (M)[AC], Not charging, load1=1.69 before launch,
safe display and `detect_leaks=0`, using the existing accumulated small corpus.
Requested `-seed=20261009 -max_total_time=120 -max_len=4096` (G).
Successful completion, exit zero, no sanitizer/oracle failure:

```text
Done 663 runs in 135 second(s)
```

The requested time is a campaign limit, not a duration gate; completing an input
can overrun it. The file corpus was not regenerated.

One `view_bench --track /tmp/edit-corpus 5` campaign, exit zero; start stamp
(M)[AC], Not charging, load1=1.69. Shared-box TRACK only, no gate verdict. This
existing benchmark covers wrap-off module regression; exact wrapped affinity is
verified by the integration tests. Selected verbatim rows:

```text
BENCH view_log_1g.txt_vertical_bulk1_command mode=TRACK n=5 p50=2008 p99=3034 ns (M)[AC] load1=1.69 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_vertical_bulk1_command mode=TRACK n=5 p50=862 p99=3078 ns (M)[AC] load1=1.69 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
ALLOCATIONS /tmp/edit-corpus/log_1g.txt mallocs=0(M)[AC] load1=1.69 gate=0(G) guard=active pass=1
ALLOCATIONS /tmp/edit-corpus/oneline_1g.txt mallocs=0(M)[AC] load1=1.69 gate=0(G) guard=active pass=1
ALLOCATIONS /tmp/edit-corpus/unicode.txt mallocs=0(M)[AC] load1=1.69 gate=0(G) guard=active pass=1
```

Existing uncached oversized-cluster/visual-row approximation and opaque layout
query bounds remain outside this view-affinity fix. Original port evidence below
is historical; the fix-up results above describe the coordinator's current main.

## Original port details and evidence

The supplied older-main patch is ported onto current main. View review findings
1–4 and 7–16 have regression evidence and view-local fixes. Full per-finding
red/green diagnostics and design decisions are in
[`P3.2b.md`](../../docs/decisions/P3.2b.md).

Changed files: `src/view/view.c`, `src/view/view.h`, this STATUS,
`tests/view_test.c`, `bench/view_bench.c`, `fuzz/view_fuzz.c`,
`docs/decisions/P3.2b.md`. No layout/editor/frozen-header/build/tracking edits.

Done:
- Trailing-empty End/Shift+End, exact resumable columns and rendered-cursor follow.
- Coherent edited error/cancel fallback, preserving wrap and resetting visual state.
- Boundary/empty-edit no-ops, plus compatible legacy empty-TYPE external repair.
- Bounded, demand-driven view-owned line queries; no opaque piece line-count API
  in view. Unindexed TYPE, protected-tail Down, EOF and page motion are tested.
- Sliced large-selection capture with owned TYPE input, per-call change tuples,
  bounded pre-mutation prefix metadata, cancellation and one optional undo group.
- Conservative decoder/query/capture work accounting and chunk deadline checks.
- Optional undo mutation routing; deterministic allocation-failure/prefix matrix.
- Explicit-affinity external rebasing and untrusted-state restoration.
- Independent byte/movement/selection/preferred-column/viewport fuzz oracle,
  structured long-cluster/checkpoint/cancel cases and multi-prefix mutation model.
- Complete-source benchmark stops, independent endpoint validation, effective
  synthetic timing-gate checks, bounded setup/command reporting and bulk readers.
- Current P4 APIs/prototypes/state payload preserved. A new wrap-port probe tests
  resumed long-cluster visual columns and cancellation after a wrapped edit.

Integration:
- TYPE consumes input synchronously. On every command/continuation/error, process
  that call's `view_change` once and invalidate affected external checkpoints.
  Large replacements can emit multiple tuples. Finish pending following before
  drawing; cancellation accepts any committed prefix at a visible byte-zero state.
- Attach a matching undo log with `view_set_undo` before direct view edits if
  desired. Each editing command is one explicit group; separate commands do not
  automatically coalesce into typing bursts. Existing editor-owned mutation and
  empty-TYPE repair remain compatible and pass the editor suite.
- Cancel before external mutation, invalidate checkpoint providers, then prefer
  `view_notify_edit` or `view_restore`. Equal-length external edits cannot be
  inferred merely from unchanged cursor/length. Normalization emits no mutation.

Limits/open work outside this bead:
- Finding 7's view-owned queries are bounded. P4's uncached layout visual-row
  lookup still uses opaque piece line queries inside `src/layout/wrap.c`; that
  inherited path needs a bounded layout/index adapter in another bead.
- Replacement is progress with truthful successful prefixes, not a reserved
  atomic transaction. Large TYPE payload insertion remains synchronous under the
  existing API. Insufficient piece/undo reserve can terminate with a prefix.
- Work budgets cannot bound page faults, scheduling or one opaque piece/layout
  operation. Whole-editor typing/frame gates remain coordinator verification.
- Layout findings 5–6 remain edit-zzj.11. No changes were made for them.

Verify with the safe display:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check
DISPLAY=:99 EDIT_DISPLAY=:99 make fuzz
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/view_test
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/view_test wrap-port
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/view_bench --self-check-start
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/view_bench --self-check-gates
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/view_fuzz --oracle-self-check
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/view_fuzz -max_total_time=120 -max_len=4096 -artifact_prefix=/tmp/zzj10- /tmp/zzj10-view-corpus
```

The :99 socket is visible only outside this sandbox; full checks use that
execution surface. All windows stayed on :99. LeakSanitizer is disabled here;
the coordinator reruns with leaks enabled. Before measured runs, BAT0 status and
load1 were read. `Not charging` maps to [AC]. No corpus fixture was regenerated.

## Verification evidence

Final release view/wrap suites: (M)[AC], BAT0=Not charging, load1=13.03.

```text
review_1: trailing empty End/Shift+End passed
review_2: exact long-line follow and rendered cursor passed
review_3: cancelled edit repair viewport passed
review_4: long-cluster boundary no-ops and legacy external repair passed
review_7: protected-tail queries and exact resumable line motion passed
review_8: bounded replacement/progress/input ownership/cancel/undo passed
review_9: conservative resumable scan budget passed
review_10: undo delete/type/replace/group/redo integration passed
review_11: reused error change output passed
review_14: external rebase/affinity/cluster/restore passed
review_15: insert/delete/prefix allocation failures and retry passed
view_test: 10000 keys mallocs=0 guard=active
wrap_port: long visual-column continuation and edited cancellation passed
view_test: all passed
wrap_test: 10000 relayouts mallocs=0 guard=active
wrap_test: 10000 typing edits mallocs=0 guard=active
wrap_test: all passed
```

Each number in this measured block carries the block's (M)[AC]/load stamp.
Final build results: `make all` exit zero (gcc, strict C11 warnings),
`make fuzz` exit zero. Build inventory: `fuzz: 21 fuzzers built` (M).

Final full sanitizer verification: (M)[AC], BAT0=Not charging, load1=13.43,
`ASAN_OPTIONS=detect_leaks=0`, DISPLAY/EDIT_DISPLAY=:99, exit zero:

```text
editor_test: all passed
wrap_port: long visual-column continuation and edited cancellation passed
view_test: all passed
wrap_test: all passed
check: 39 test binaries passed
test_replay_cli: all passed
```

Final self-checks (synthetic, not timings):

```text
review_16 GREEN: benchmark start uses complete clusters
review_12 GREEN: delayed command rejected by G1/G3/slice checks
review_13 GREEN: wrong movement rejected by independent oracle
port API: all existing prototypes and public payloads preserved; view-owned opaque line queries absent
```

Fuzzing used the same accumulated small corpus, `detect_leaks=0`, safe display,
and a requested `-max_total_time=120` seconds (G) each run. Completed runs:

| Implementation stage | Runs/time (M) | Stamp |
|---|---:|---|
| After oracle bookkeeping correction | 1994 / 121 seconds | (M)[AC], Not charging, load1=5.23 |
| Before final prefix-retention fix | 434 / 206 seconds | (M)[AC], Not charging, load1=29.18 |
| Final source | 783 / 121 seconds | (M)[AC], Not charging, load1=13.30 |

All completed runs exited zero; no sanitizer/oracle failures in them. The middle
run's seed processing overshot the requested time on the loaded box; that is
observed wall time, not a duration gate pass. Final verbatim completion:

```text
Done 783 runs in 121 second(s)
```

The first attempt exited on the oracle bookkeeping assertion documented in the
decision. Its input was retained and succeeds in the completed runs. LSan still
needs coordinator verification outside this sandbox.

## Single TRACK benchmark

`build/bench/view_bench --track /tmp/edit-corpus 5`, exit zero. Start stamp:
(M)[AC], BAT0=Not charging, load1=29.10; subsequent rows load1=29.18.
This is shared-box TRACK only. Necessary limits are (G), not gate verdicts.
The measured run predates the final protected-prefix retention fix; the remote
oneline replacement rows below are incomplete durations/lower bounds. That
failure was subsequently fixed with a protected-prefix test, not another bench
run. Deep-log preparation remains explicitly skipped at the setup budget.

Selected verbatim evidence:

```text
BENCH view_log_1g.txt_vertical_bulk1_command mode=TRACK n=5 p50=6704 p99=9174 ns (M)[AC] load1=29.10 necessary_gate=5000000/5555555 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_log_1g.txt_unindexed_type_bulk1_command mode=TRACK n=5 p50=11461 p99=17270 ns (M)[AC] load1=29.18 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
SETUP_LIMIT deep_type skipped=5(M)[AC] load1=29.18 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
SETUP_LIMIT selection_1MiB_replace skipped=5(M)[AC] load1=29.18 mode=TRACK verdict=TRACK setup_cap=100000000 ns(G)/1024 calls(G)
BENCH view_oneline_1g.txt_deep_type_bulk1_command mode=TRACK n=5 p50=10437 p99=15122 ns (M)[AC] load1=29.18 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_command mode=TRACK n=5 p50=5398562 p99=5554192 ns (M)[AC] load1=29.18 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=1 dropped=0
BENCH view_oneline_1g.txt_selection_1MiB_replace_bulk1_continue mode=TRACK n=5120 p50=5410 p99=10991 ns (M)[AC] load1=29.18 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_command mode=TRACK n=5 p50=4919847 p99=10745196 ns (M)[AC] load1=29.18 necessary_gate=1000000/2000000 ns(G) verdict=TRACK incomplete=0 dropped=0
BENCH view_unicode.txt_selection_1MiB_replace_bulk1_continue mode=TRACK n=2565 p50=2955 p99=10027 ns (M)[AC] load1=29.18 necessary_gate=500000/500000 ns(G) verdict=TRACK incomplete=0 dropped=0
SLICE_MAX selection_1MiB_replace 9037052 ns(M)[AC] load1=29.18 hard_limit=500000 ns(G) verdict=TRACK incomplete_commands=0
ALLOCATIONS /tmp/edit-corpus/log_1g.txt mallocs=0(M)[AC] load1=29.18 gate=0(G) guard=active pass=1
ALLOCATIONS /tmp/edit-corpus/oneline_1g.txt mallocs=0(M)[AC] load1=29.18 gate=0(G) guard=active pass=1
ALLOCATIONS /tmp/edit-corpus/unicode.txt mallocs=0(M)[AC] load1=29.18 gate=0(G) guard=active pass=1
```

Logical remote/indexing commands and deep-log setup are not certified by these
rows. Several maximum slices exceed the necessary limit on this loaded box;
there is no quiet gate claim. The `bulk3` mode submits three reader jobs (G),
including the active job; measured output's `three-queued` label was corrected to
`three-submitted` after this run. Synthetic gate/start self-checks remain green.
