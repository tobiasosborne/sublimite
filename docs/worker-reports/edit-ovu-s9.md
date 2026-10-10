# edit-ovu-s9 — P4.3b scroll review fixes

This is an incomplete bead. Findings §2, §3 and §5 are implemented and tested.
Finding §4 has a tested resident-source worker/mailbox bridge, with host wiring
still required. Findings §6–8 remain unimplemented. Do not close edit-ovu on
this report alone. No renderer/layout/editor source changes were made.

## Per finding

| Review finding | Done | Remaining |
|---|---|---|
| §2: failed sources become false exact positions | Source failure propagates through delegated seeks and all adapter-owned reads; the public state commits only after complete success. Indexed, unindexed, partially failing and NULL-pointer spans preserve state. Failed partial worker windows are discarded without publishing chunk counts. | No known module-level gap. |
| §3: byte budget does not bound foreground work | Caller-owned resumable resolve/follow state; one shared literal positive byte budget, callback limit and wall deadline covers alignment, physical walking, clipping, seeking and relabelling. MORE preserves public state. Tests exercise one-byte spans, indexed/unindexed follow, deadline expiration and changed intent. | Host must retain the continuation and check input between slices. An indivisible source callback must itself be bounded; wall time can overrun on a callback or descheduling. |
| §4: foreground major faults | Bulk-worker resident windows, mailbox-only adoption, unavailable-data MORE, partial-read rejection and physical retirement before source/window reuse. A page-dropped mapped fixture asserts worker-only raw reads and zero foreground major faults. Allocation guard covers resident navigation. | Editor installation is not wired. Raw-source APIs remain available under an explicit resident/nonblocking-source precondition. Cold disk-cache eviction and end-to-end editor navigation are not qualified. This finding is not fully closed. |
| §5: obsolete synchronous bench contract | Immutable-source `lineidx_seek_start_owned` / `lineidx_seek_result`, bounded adoption, cancellation suppression and timeout. Timer includes enqueue, waiting, adoption, scroll resolution, layout and correct null submission/presentation. P3.4 integration contract corrected. | Sample adequacy belongs to edit-yqu; sample counts were left unchanged. |
| §6: renderer origin/overscan | Existing fractional accumulator retained. | No origin/clip API, overscan reservation, shared hit-test/damage transform or real-backend fractional rendering test implemented. No red/green claim for this finding. |
| §7: wrapped scroll adapter | Existing logical adapter explicitly remains wrap-off. | No bounded visual-row advance/affinity adapter or wheel/page/resize/EOF/follow tests implemented. No red/green claim for this finding. |
| §8: displayed G3z | Existing null work proxy remains labelled as a proxy, with displayed refreshes UNMEASURED. | No integrated displayed-frame-ID test, sidebar/fractional oracle or presentation-completion verdict implemented. No red/green claim for this finding. |

Feature work stopped after §5 to complete verification and this report within
the worker budget. The remaining fixes require renderer/layout/editor interface
implementation and integration tests; that work is incomplete.
The acceptance condition that every named finding has a red-first test and a
concrete fix is therefore **not satisfied for the whole bead**.

## Red runs, written before their fixes

Commands were run with `DISPLAY=:99 EDIT_DISPLAY=:99`; test/build logs live
under `build/` in this worktree. The failure line numbers below are those of
the test at the time of the red run.

§2, `make build/tests/scroll_test`, then `build/tests/scroll_test`, exit 1:

```text
scroll_test:201: FAIL scroll_resolve(&s, index, &src, 0) == SCROLL_ERR_SOURCE
```

§3, the same release test, exit 1:

```text
scroll_test:185: FAIL scroll_resolve(&s, index, &src, 0) == SCROLL_MORE
```

§4, the same release test with a raw-source UI-call assertion, exit 1:

```text
scroll_test:214: FAIL atomic_load(&f.ui_calls) == 0
```

The green version sends that source through the resident bridge. It maps an
anonymous file-backed fixture and drops mapping pages before navigation. This
checks worker-only access; it does not claim a cold storage-cache experiment.

§5, `build/bench/scroll_bench --track`, before the contract fix:

```text
scroll_bench: TRACK=1 warm mapped corpus, 80x24 null frames; (G) jump p50<=30ms p99<=50ms; (G) work max<=T/2=4.166667ms, 0/10000 over-budget steps
exit=2
```

The obsolete single synchronous seek returned before the deep target was
exact, so the benchmark exited before recording its first sample.

## Green runs

Latest release module run, exit 0 (also repeated successfully after the forced
release rebuild):

```text
scroll_test: partial worker read discarded; cancellation retains source until physical retirement PASS
scroll_test: resident worker navigation, foreground major faults=0 PASS
scroll_test: complete-operation callback/byte budget and resumable alignment/clipping PASS
scroll_test: indexed/unindexed follow, delegated reads and expired deadline share slice PASS
scroll_test: delegated indexed/unindexed/partial/NULL source failures preserve state PASS
scroll_test: accumulation/reversal/extreme deltas PASS
scroll_test: page/document ends/follow margins/resize/invalid PASS
scroll_test: byte/index correction preserves cursor row PASS; allocations=0 guard=active
scroll_test: unindexed page/document ends PASS
scroll_test: bounded cursor proof/margins/publication retry PASS; allocations=0 guard=active
scroll_test: bounded long-line/source errors/partial seeks PASS; allocations=0 guard=active
scroll_test: PASS
```

The release allocator guard is active; sanitizer interposition is inert.
The resident navigation test also requires zero allocations while worker
acquisition and mailbox adoption are active. Its foreground major-fault count
is zero (M)[AC]. Slice limits are 64 KiB default, at most 256 callbacks and
0.5 ms between callbacks (G); spans are capped at 4 KiB (G). Positive byte
budgets are literal. No new globals or allocation calls on the typing path.

Freshly force-relinked Clang ASan/UBSan module run, exit 0, with
`ASAN_OPTIONS=detect_leaks=0`. Pre-run power was Charging, load1 19.59 (M)[AC]:

```text
scroll_test: partial worker read discarded; cancellation retains source until physical retirement PASS
scroll_test: resident worker navigation, foreground major faults=0 PASS
scroll_test: complete-operation callback/byte budget and resumable alignment/clipping PASS
scroll_test: indexed/unindexed follow, delegated reads and expired deadline share slice PASS
scroll_test: delegated indexed/unindexed/partial/NULL source failures preserve state PASS
scroll_test: accumulation/reversal/extreme deltas PASS
scroll_test: page/document ends/follow margins/resize/invalid PASS
scroll_test: byte/index correction preserves cursor row PASS; allocations=0 guard=sanitizer-inert
scroll_test: unindexed page/document ends PASS
scroll_test: bounded cursor proof/margins/publication retry PASS; allocations=0 guard=sanitizer-inert
scroll_test: bounded long-line/source errors/partial seeks PASS; allocations=0 guard=sanitizer-inert
scroll_test: PASS
```

§5 TRACK contract run, exit 0; loaded AC box, power `Not charging`, load1
7.40 (M)[AC]. This is a descriptive run, not a timing gate verdict. The broken
variant produced no timing samples, so no paired timing improvement is claimed.
The benchmark's sample count and cadence rows are unchanged for edit-yqu.

```text
scroll_bench: TRACK=1 warm mapped corpus, 80x24 null frames; (G) jump p50<=30ms p99<=50ms; (G) work max<=T/2=4.166667ms, 0/10000 over-budget steps
JUMP (M)[AC] load1=7.40 power=Not charging sample=1 ns=185639755 target=8053057 byte=966366840 prefix_chunks=14745 correct=1
JUMP (M)[AC] load1=7.40 power=Not charging sample=2 ns=193336452 target=8053057 byte=966366840 prefix_chunks=14745 correct=1
JUMP (M)[AC] load1=7.40 power=Not charging sample=3 ns=109923209 target=8053057 byte=966366840 prefix_chunks=14745 correct=1
JUMP (M)[AC] load1=7.40 power=Not charging sample=4 ns=132935995 target=8053057 byte=966366840 prefix_chunks=14745 correct=1
JUMP (M)[AC] load1=7.40 power=Not charging sample=5 ns=121338922 target=8053057 byte=966366840 prefix_chunks=14745 correct=1
BENCH name=G7j_warm n=5 required_n=10000 p50=132935995 p99=193336452 ci95_p50=[109923209,193336452] ci95_p99=[185639755,193336452] gate_p50=30000000 gate_p99=50000000 dropped=0 (M)[AC] power=Not charging load1=7.40 verdict=TRACK
BENCH G7j_warm p99_ns=193336452 (M)[AC] load1=7.40 (G)<=50000000 TRACK=1
BENCH G7j_warm p50_ns=132935995 (M)[AC] load1=7.40 (G)<=30000000 TRACK=1
BENCH G3z_work_proxy (M)[AC] load1=7.40 power=Not charging n=10000 p50_ns=64312 p99_ns=112365 max_ns=1380373 over_T_half=0 (G)max_ns<=4166666 (G)over_T_half=0 TRACK=1
G3z displayed refreshes: UNMEASURED; requires real display and editor wiring. Sub-row pixel origin is retained by scroll; null render currently draws integral rows.
exit=0
```

## Acceptance and environment

- GCC 13.3.0 release and Clang 18.1.3 ASan/UBSan compile with the repository's
  `-Wall -Wextra -Werror -Wshadow -Wconversion` flags.
- `DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all`: exit 0. The final combined
  `make -j4 all build/fuzz/scroll_fuzz` also exited 0.
- The first sandbox `make check` stopped because `cli_test` could not connect
  to Xvfb's socket. The socket-enabled rerun was invoked with DISPLAY=:99 and exited 0:
  `check: 59 test binaries passed`; `test_replay_cli: all passed`.
- The environment clock rewound after the first passing run, leaving future-
  dated generated libraries/binaries. A subsequent ordinary make compiled new
  objects but did not relink those future-dated outputs. That stale rerun was
  stopped. `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -B -j4 check`
  rebuilt every object, archive and test binary from the final sources and
  exited 0. The final count is 59 test binaries (M)[AC], plus replay CLI checks.
  The final log still prints the clock-skew warning because source timestamps
  remain in the future; -B forced compilation, archive creation and relinking.
  `DISPLAY=:99 EDIT_DISPLAY=:99 make -B -j4 all` also exited 0, refreshing
  generated release outputs after the clock rewind. Scope was frozen before
  the cutoff; validation is complete for the implemented module changes.
- Every sanitizer run uses `ASAN_OPTIONS=detect_leaks=0`: LeakSanitizer cannot
  run in the worker sandbox. The coordinator must rerun with leaks enabled.
- Final module fuzz command:
  `DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/scroll_fuzz -max_total_time=60 -max_len=2048 -artifact_prefix=build/`.
  Pre-run power was `Charging`, load1 11.61 (M)[AC]. It exited 0:

```text
Done 13603 runs in 61 second(s)
```

The fuzz duration and run count are (M)[AC]. The only fuzz change adds bounded
continuation pumping at the existing resolve/follow call sites; generator/model
expansion is left to edit-yqu to minimize merge overlap. A smoke run also passed.
No corpus fixture was changed. No window was opened on :0. Git was used only
for read-only diff inspection; no add/commit/stash/checkout/rebase/reset or bd.
HANDOFF.md and docs/worklog/ were untouched.

## Decisions and integration

Design decisions are recorded in `docs/decisions/edit-ovu.md`. P3.4's live
contract now uses bounded/resident slices and asynchronous deep seeking; its
historical measurement evidence remains historical. The module STATUS.md is
updated. New code is `src/scroll/resident.c`; the public caller-owned bridge
and continuation types are in `src/scroll/scroll.h`.

Keep a zero-initialized continuation across MORE slices. Source edits require
cancelling/clearing it and replacing the immutable resident source only after
physical retirement. The bridge borrows the source; it does not invoke its
release hook. Mailbox adoption, not polling a shared done flag, exposes bytes.
A terminal message can precede worker return, so window reuse also checks the
work handle's physical completion. A full mailbox yields the bulk job.

The UI byte-to-line query captures a metadata-only chunk baseline and scans
the suffix with its own continuation; the incomplete legacy result is never
published. Proof exhaustion differs from slice exhaustion: cursor follow can
remain pending until index publication proves the missing boundary/margins.
These decisions preserve the existing coarse-byte fallback without claiming
wrapped-row or renderer integration.

A final seeded fuzz run passed with the same unchanged generator and longer
existing-model event/publication sequences. It exited 0: Seeds and generated corpus
are under `build/scroll-fuzz-corpus/`; command: `build/fuzz/scroll_fuzz
build/scroll-fuzz-corpus -max_total_time=60 -max_len=2048 -artifact_prefix=build/`,
with DISPLAY=:99 and ASAN_OPTIONS=detect_leaks=0. Pre-run power Charging and
load1 9.21 (M)[AC]. Terminal result (M)[AC]:

```text
Done 251 runs in 61 second(s)
```

Final forced full-suite green run, exit 0 (counts measured (M)[AC]):

```text
check: 59 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

`git diff --check` passed. All implemented-code checks are green; the bead
remains incomplete for the open integration findings listed at the top.
