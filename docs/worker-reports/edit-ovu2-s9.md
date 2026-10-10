# edit-ovu2-s9 — P4.3b scroll review fixes, slice 2

Branch/worktree: `wt/edit-ovu2`. Scope: `docs/reviews/P4-modules-2.md` §6–8.
Read CLAUDE.md, the assigned bead, the named review sections, scroll STATUS,
and the prior slice's report/decisions. Git was used only for read-only diff
inspection. No `bd`, Git mutations, HANDOFF or worklog edits were performed.
No editor, raster, render, layout or view implementation was changed.

## Per finding: done and remaining

### §6 — consumable smooth-scroll origin and overscan

Done: `scroll_frame_plan`, `scroll_plan_frame` and `scroll_frame_hit` are
public scroll APIs. They define exact Q8 origin, viewport clip, one extra
layout row (G), inverse hit testing and full viewport damage when fractional
movement changes the plan. Reservation covers cells/metadata/wrap plans;
visible surface geometry remains fixed. Tests prove fractional movement at
row height 17 (G fixture), lower-edge content coverage, clipped hit rejection,
reversal, stable-plan damage and error-output preservation.

Remaining outside this scope: editor consumption, backend Q8 translation,
layout overscan wiring and actual translated pixels through a real renderer.
The contract is implemented/tested in scroll; the renderer behavior is not
claimed fixed. Required host operations are specified in P3.4.

### §7 — wrapped visual-row adapter

Done: `src/scroll/visual.c` implements caller-owned visual-row navigation with
bounded resident source callbacks, an affinity-aware locate operation and a
resumable resolver. Wheel accumulation, page/document motions, byte-anchor
reflow, fractional carry, EOF clipping and cursor follow use visual ordinals.
The wrap descriptor preserves logical line/column/indent/boundary fields for
layout seeding. Exact total counts are optional; unknown counts never clamp
a deep intent to an estimate. Bottom lookahead/End publish a proven exact EOF
extent. Pending unknown-count End can be canceled by wheel/page/follow.
Integer-limit resize carry saturates before clipping. State remains unchanged
on MORE/errors, and intent/source/generation changes cancel old progress.

The default slice permits two queries (G), at most 256 queries (G), with a
0.5 ms (G) default deadline checked between bounded callbacks. Producers must
themselves perform bounded resident work or yield. No allocation/global storage
was added to scroll. The release test's active allocator guard records zero
allocations (M)[AC] across wrapped transitions/resolution/follow/reflow.

Remaining outside this scope: the editor/layout owner must provide resident,
exact wrap boundary metadata and route wrapped events through this adapter.
Existing layout queries can return approximate deep boundaries and are not a
sufficient exact metadata producer. Pending reflow events must be queued by
the host until its new seed resolves; the adapter explicitly rejects wheel/page
while reflow is pending. Tests exercise supplied wrapped descriptors, not a
fully wired wrapped editor.

### §8 — displayed G3z observability

Done: a public-editor/null-backend test correlates available submit/present IDs
and probes wheel input. The required integration probe fails because wheel
input schedules no frame. `on_present` observes present submission; null
completion is synthetic. The existing public API cannot expose displayed
refresh opportunities, fractional submitted plans or per-frame sidebar
identity, so a displayed-frame-ID G3z test is not possible in this slice.

As explicitly permitted by the bead, P3.4 specifies the required public
`on_refresh` hook: every refresh opportunity including repeats/drops, displayed
ID/refresh sequence/actual completion time, explicit synthetic/unsupported
status, immutable input/source/scroll-plan/wrap/sidebar metadata, and correct
stale completion handling. The future verdict must reject a deliberately
repeated every-second frame across 10,000 refreshes (G), with zero misses (G).
The benchmark's proxy is now explicitly labelled CPU regression work plus
synthetic null completion and `displayed_verdict=UNAVAILABLE`. Sample counts
and timing policy were not changed; those belong to edit-yqu.

Remaining: editor scroll wiring, the public refresh observer and real backend
refresh/completion correlation. **Displayed G3z remains unmeasured/open.**
`scroll_test --require-editor-scroll` intentionally remains red for the editor
owner. The ordinary suite's green capability probe does not claim this finding
fixed; it reports absent wheel frames and unavailable displayed G3z.

## Red runs (tests written before their implementation)

Each command below exited 1 (M)[AC]. Runtime stubs returned ARG while the
first API tests were present; the fixes followed the observed failures.

§6: `make build/tests/scroll_test`, then
`DISPLAY=:99 build/tests/scroll_test`:

```text
scroll_test:611: FAIL scroll_plan_frame(&s, 80, 68, NULL, &p) == 0
```

§7 initial adapter, same build/test command:

```text
scroll_test:642: FAIL scroll_visual_init(&s, (scroll_config){4, 17, 1}, &source, seed) == 0
scroll_test: Q8 origin/clip/overscan, inverse hit transform and damage PASS
```

§7 estimated extent/reflow coverage added before its fix:

```text
scroll_test:716: FAIL rc == 0 && s.first.ordinal == 3 && !s.source.exact
scroll_test: Q8 origin/clip/overscan, inverse hit transform and damage PASS
```

§7 integer-limit carry added before saturation fix:

```text
scroll_test:805: FAIL s.first.ordinal == UINT64_MAX - 4u && s.viewport.subrow_q8 == 0
scroll_test: Q8 origin/clip/overscan, inverse hit transform and damage PASS
scroll_test: wrapped wheel/page/reflow/EOF/follow affinity, bounded pending/error/cancel PASS; allocations=0 guard=active
```

§7 pending End cancellation added before cancellation fix:

```text
scroll_test:719: FAIL scroll_visual_wheel(&s, -256) == 0
scroll_test: Q8 origin/clip/overscan, inverse hit transform and damage PASS
```

§8 public editor probe:
`DISPLAY=:99 build/tests/scroll_test --require-editor-scroll`:

```text
scroll_test:828: FAIL observer.submissions > baseline
```

This is a retained integration failure, not a fixed displayed-cadence test.
The host hook specification is the authorized fallback for this finding.
The final release binary was also checked again with that flag, exit 1
(M)[AC], confirming the retained integration gap:

```text
scroll_test:873: FAIL observer.submissions > baseline
```

## Green module runs

Release: `make build/tests/scroll_test`, then
`DISPLAY=:99 build/tests/scroll_test`, exit 0 (M)[AC]:

```text
scroll_test: Q8 origin/clip/overscan, inverse hit transform and damage PASS
scroll_test: wrapped wheel/page/reflow/EOF/follow affinity, bounded pending/error/cancel PASS; allocations=0 guard=active
scroll_test: wrapped resize carry saturates at integer-limit EOF PASS
scroll_test: public editor/null frame IDs correlate at submit/present; wheel frames=absent; displayed G3z UNAVAILABLE (hook required) PASS
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

Clang ASan/UBSan module run:
`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/scroll_test`,
exit 0 (M)[AC]. New-feature rows:

```text
scroll_test: Q8 origin/clip/overscan, inverse hit transform and damage PASS
scroll_test: wrapped wheel/page/reflow/EOF/follow affinity, bounded pending/error/cancel PASS; allocations=0 guard=sanitizer-inert
scroll_test: wrapped resize carry saturates at integer-limit EOF PASS
scroll_test: public editor/null frame IDs correlate at submit/present; wheel frames=absent; displayed G3z UNAVAILABLE (hook required) PASS
scroll_test: PASS
```

Sanitizer allocation interposition is inert; the release guard above supplies
the allocation evidence. LeakSanitizer cannot run inside this sandbox;
`ASAN_OPTIONS=detect_leaks=0` was used throughout. The coordinator must rerun
with leaks enabled.

## Validation, fuzz and environment

GCC 13 release `make all` exits 0 (M)[AC], with
`-Wall -Wextra -Werror -Wshadow -Wconversion`. Clang 18 uses the same warnings
plus ASan/UBSan. No new globals were introduced. All display commands used
`:99`; no command opened a window on `:0`.

The initial sandbox `make check` reached `cli_test` and failed to connect to
`:99`. A read-only check outside the sandbox confirmed the existing Xvfb
server at `:99`; the suite was rerun outside the isolated socket namespace,
with the same display and leak option. No test source was altered to bypass
display checks. Final full-suite status is recorded below.

A fuzz smoke first exposed old compatibility-call expectations left by the
prior slice. A fragmented source legitimately yields from the one-slice API:

```text
scroll_fuzz:221: FAIL scroll_seek_byte(&s, anchor) == 0 && scroll_resolve(&s, index, &src, 0) == 0
```

Minimal fuzz-only adjustment: pump the already-existing caller-owned
continuation helpers at the expected-completion call sites. Preserve the
expected-MORE long-gap proof test. No earlier resolver implementation was
changed. New fuzz operations compare wrapped motion/reflow/follow and Q8
origin/hit transforms against an independent scalar pixel/row model, including
estimated totals, affinity and one-query continuation slices.

Final current-tree release build: `make -j4 all`, exit 0 (M)[AC].
The Clang sanitizer module run above also uses the final source.

Final seeded, mutating fuzz run:

```sh
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/scroll_fuzz \
  build/worker-evidence/final-fuzz-corpus -max_total_time=60 \
  -artifact_prefix=build/worker-evidence/
```

Exit 0 (M)[AC], 62 seconds (M)[AC], 233 inputs executed (M)[AC]; no
ASan/UBSan diagnostics or model failures. Fresh seeds exercise every visual
operation and both affinities; they include the old one-slice reproducer.
Power was `Not charging`; load1 was 7.63 (M)[AC] during final validation.
The earlier current-tree seed-replay run also completed 90 seconds (M)[AC]
clean; its larger corpus spent the runtime in seed initialization, so the
fresh-seed run above supplies mutation evidence.

```text
#233 DONE cov: 3268 ft: 11471 corp: 136/1894b lim: 17 exec/s: 3 rss: 55Mb
Done 233 runs in 62 second(s)
```

The first outside-sandbox full `make check` exited 2 (M)[AC] after passing
raster conformance. Its unrelated reference-window test failed:

```text
raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)
refwin_test: FAIL headless CSV completeness/monotonicity
make: *** [Makefile:101: check] Error 1
```

No reference-window code was changed. Its standalone retry on the same `:99`
display exited 0 (M)[AC]:

```text
refwin_test: PASS 12 editor/reference pairs, complete monotonic CSV, incomplete/wrong-frame runs rejected (Xvfb phase approximate)
```

The fresh full current-tree retry completed successfully, exit 0 (M)[AC]:

```sh
DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check
```

It ran outside the display-isolated sandbox against the existing Xvfb `:99`,
with Clang 18 ASan/UBSan and the required warnings. All 59 test binaries
(M)[AC] and the replay CLI checks passed. The full run included the final
wrapped cancellation/carry tests and the explicit public-editor capability
probe. Terminal evidence:

```text
refwin_test: PASS 12 editor/reference pairs, complete monotonic CSV, incomplete/wrong-frame runs rejected (Xvfb phase approximate)
scroll_test: Q8 origin/clip/overscan, inverse hit transform and damage PASS
scroll_test: wrapped wheel/page/reflow/EOF/follow affinity, bounded pending/error/cancel PASS; allocations=0 guard=sanitizer-inert
scroll_test: wrapped resize carry saturates at integer-limit EOF PASS
scroll_test: public editor/null frame IDs correlate at submit/present; wheel frames=absent; displayed G3z UNAVAILABLE (hook required) PASS
scroll_test: PASS
check: 59 test binaries passed
test_replay_cli: all passed
```

The isolated retry and full retry show the reference-window failure is
intermittent; its cause was not established and no out-of-scope fix was made.
`git diff --check` also exits 0 (M)[AC].

Acceptance achieved for this slice's implementation: release build, full
sanitizer suite with leak detection disabled, and module fuzz runtime. The
external integration gaps described per finding remain open, especially
displayed G3z; none are presented as a renderer/display gate pass.

No new performance gate verdict is claimed. The proxy label was changed only;
no benchmark timing/sample policy was altered or single noisy timing treated
as a pass/fail. Measured test/fuzz evidence is on AC; pre-run power was
`Not charging`. The box is shared with other workers.

At the minute-30 checkpoint (G worker budget), implementation was frozen.
The full sanitizer retry had passed index/piece checks and was running live
raster conformance. This report was already present with complete per-finding
done/remaining and red/green evidence; only terminal full-suite status remained
to be recorded.

## Decisions and scope notes

Design choices are recorded in `docs/decisions/edit-ovu2.md` and the consumable
integration contract in `docs/decisions/P3.4.md`: Q8 plan/overscan/hit/damage,
resident visual metadata callbacks with optional exact totals, lazy EOF proof,
leading/trailing affinity, reflow/cancellation/lease rules, and the required
public displayed-refresh observer.

Out-of-scope observations left unchanged: editor wheel routing is absent;
backend origin/overscan support and exact deep wrap metadata publication are
missing; the initial P3.4 overview still describes the older no-clock/no-retained
source design, whereas the preceding slice's resolver/resident bridge already
uses caller-owned clocks/source leases. The §6–8 integration amendment is
current; earlier implementation contracts were not broadened by this worker.
