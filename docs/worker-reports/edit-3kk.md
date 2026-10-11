# edit-3kk — editor resident scroll and refresh wiring

Scope: the assigned editor obligations in P3.4 and edit-ovu2-s9, with renderer
origin consumption. No `bd` or `git` commands were run. All writes stayed in
this worktree or temporary storage. The Makefile and src/scroll implementation
are unchanged. Design choices: [edit-3kk](../decisions/edit-3kk.md).

## Changes

- `src/editor/scrollctl.c`, `private.h`: per-buffer immutable source lease,
  resident bridge, requested/committed viewport and bounded resolve/follow
  continuations. Missing windows preserve the displayed viewport; edits cancel
  old navigation and retain old backing through physical retirement. Certified
  resident layout rows avoid unnecessary copying for warm wheel/typing paths.
- `src/editor/input.c`, `editor.c`, `buffers.c`, `large.c`, `open.c`, `paint.c`:
  wheel dispatch stays in input.c; adoption stays in scrollctl.c. Wire logical
  page/document motions, completed motion/edit following, source replacement,
  tab activation, resize and minimap seeks. Keep cursor/selection ownership in
  view and adopt scroll-owned vertical fields. Reserve and compose overscan
  independently of visible surface geometry.
- `src/editor/editor.h`, `refresh.c`: public refresh callback and opportunity
  ingestion; immutable input/source/geometry/selection/scroll/wrap/sidebar
  metadata; native completion correlation and explicit synthetic/unsupported
  opportunity-stream status. Repeats do not release a pending newer frame.
- `src/render/render.h`, `render.c`, `src/raster/raster.h`, `raster.c`,
  `raster_kernel.c`, `src/gl/renderer.inc`: validated Q8 text transform, extra
  row capacity, immutable renderer snapshots, raster translated pixel sampling,
  and GL origin/clip uniforms. Fixed chrome/sidebar retain surface coordinates.
- `tests/editor_scroll_test.c`, `raster_scroll_test.c`, `gl_scroll_test.c`,
  `scroll_test.c`, `editor_test.c`: null integration, retained source cancellation,
  refresh/repeat/missing/stale identity, raster pixel and GL snapshot coverage.
  The deep EOF typing fixture now uses the specified last full viewport, derived
  from its newline count and visible row count; its substantive checks remain.
- `bench/scroll_bench.c`: public-editor synthetic refresh verdict plus an
  intentionally repeated stream; retains the separately labelled CPU proxy.
  `--displayed-probe` runs the integration without the unrelated giant seek
  campaign. `fuzz/editor_fuzz.c` adds wheel events to the byte/selection model.
- Documentation: this report, the decision, P3.4 amendment, editor STATUS,
  worklog and HANDOFF entry.

## Red first

Power was `Charging` (M)[AC], read before validation. Values in the following
terminal excerpts are measured outputs: source line numbers come from compiler
or assertion locations; return statuses come from the invoked processes.

The initial null wheel test was written before implementation:
`make build/tests/editor_scroll_test && build/tests/editor_scroll_test`,
failed (M)[AC]:

```text
editor_scroll_test:38: FAIL o.frames > baseline
```

The refresh/immutable-plan assertions were added before their API and host
implementation. `make build/tests/editor_scroll_test` failed because the hook
and observation type did not exist (M)[AC], with then-current compiler source
positions:

```text
tests/editor_scroll_test.c:7:46: error: unknown type name ‘editor_refresh’
tests/editor_scroll_test.c:34:34: error: ‘editor_config’ has no member named ‘on_refresh’
```

Full compiler output is retained in the local evidence directory.

The raster translation test was written against a pass-through stub before
sampling was implemented. `make build/tests/raster_scroll_test &&
build/tests/raster_scroll_test` failed (M)[AC]:

```text
raster_scroll_test:20: FAIL pixels[2] == 0x808080 && pixels[3] == 0x0000ff
```

Further red runs pointed directly at integration gaps and preceded their fixes
(M)[AC], assertion locations derived from the then-current test files:

```text
editor_scroll_test:94: FAIL editor_inject(e, &event) == 0 && settle(e) == 0
editor_scroll_test:55: FAIL rc == 0 && backend.stats.submitted_frames > baseline
```

The former is fractional resize; the latter is wrap-off EOF typing while the
bulk worker is deliberately held. The model fuzzer independently reproduced
the stale resize rectangle. Existing pending-preview checks exposed an
uninitialized overscan tail when gutter width stayed unchanged; initialization
now covers the reserved row even before a wrapped initial view. The existing
positional-open regression exposed loss of its certified line label under
partial indexing; the scroll seed now retains the worker-proven byte/line pair.

The expected mixed channel is derived from exact half-pixel Q8 coverage:
`(black * half + white * half + rounding) / pixel_unit`; the fixture writes
the channel arithmetic beside its assertion. Chrome/sidebar assertions are
independent fixed-coordinate checks.

## Green

Focused release validation (M)[AC], active allocator guard; assertions/counts
come directly from the executed suites:

```text
editor_scroll_test: null fractional/core wheel, overscan, immutable refresh/repeat/missing/stale correlation PASS
editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=0 guard=active PASS
editor_scroll_test: fractional resize rebuilds clip/overscan and preserves origin PASS
editor_scroll_test: wrap-off EOF typing submits while bulk worker is held PASS
raster_scroll_test: exact Q8 translated pixels, bottom overscan, fixed chrome/sidebar PASS
gl_scroll_test: immutable Q8 plan/overscan snapshot, unchanged surface geometry PASS
editor_test: partial index deep typing reuses viewport line starts passed
P1-1 section 6: inactive exact counts/jump, bulk cancellation/retry, mallocs=0 guard=active passed (M)[AC]
P4-3 section 31: real mapped truncation during copy preserves viewport passed
P4-3 section 31: real mapped truncation after publication preserves viewport passed
scroll_test: public editor/null wheel IDs correlate at submit/present/refresh; synthetic completion, physical opportunity stream unsupported PASS
```

Commands: `build/tests/editor_scroll_test`, `build/tests/raster_scroll_test`,
`build/tests/gl_scroll_test`, `build/tests/editor_test --line-start-only`,
`build/tests/editor_test --index-repair`, and
`build/tests/scroll_test --require-editor-scroll` after `make all`.

The public hook benchmark (M)[AC], `build/bench/scroll_bench --displayed-probe`:
the opportunity population is the existing G3z contract population; the rejected
population is that population divided by alternate-frame repetition. Power and
load are measured by the benchmark before this probe, not guessed:

```text
G3z displayed_verdict=SYNTHETIC_PASS physical_verdict=UNSUPPORTED (M)[AC] load1=28.38 power=Charging opportunities=10000 misses=0 repeated_stream_misses=5000 derivation=public_null_refresh_callbacks;alternate_previous_ID_injection=10000/2 physical_reason=native_completion_is_not_every_refresh_opportunity
```

This is a synthetic observer/verdict test. It does not certify displayed G3z
on a physical backend or timing against Sublime. The existing CPU-work proxy
cannot certify that either. No giant seek or unrelated benchmark campaign was
rerun to obtain a quieter result.

## Final build and sanitizer verification

GCC release `make all` passes (M)[AC], process exit status zero, with C11 and
`-Wall -Wextra -Werror -Wshadow -Wconversion`. Clang ASan/UBSan builds every
suite with the same warnings. Final `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99
EDIT_DISPLAY=:99 make check` exits with make's failure status (M)[AC], measured
as `2`, after the CLI suite's IPC setup refusal:

```text
base_test: PASS
== build/san/tests/cli_test
cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:105: check] Error 1
```

Every sanitizer suite was then invoked individually, preserving failing
assertions and recording each exit status. The local `results.tsv` contains
66 test binaries (M)[AC], derived by enumerating `build/san/tests/*_test`:
49 passed and 17 failed (M)[AC], counted by their recorded process statuses,
with `49 + 17 = 66`. Replay CLI passed separately (M)[AC]; therefore the matrix
contains `66 + 1 = 67` entries and `49 + 1 = 50` successful entries. All suites
that require neither display access nor restricted Unix IPC passed. Hybrid
suites stopped at those restricted operations, after passing their available
null checks. The exact failed suite list is:

| Restriction | Suites |
| --- | --- |
| Native display setup | `editor_close_test`, `file_kill_test`, `raster_test`, `refproto_test`, `refwin_test`, `refwin_timeout_test`, `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`, `x11_live_test`, `x11_order_test`, `x11_stall_test` |
| Unix IPC bind/send | `cli_test`, `editor_large_test`, `editor_p4_test`, `ipc_test` |
| Both IPC and native display subcases | `editor_test` |

`gl_test` passed its pure snapshot/completion/trace checks but explicitly
skipped native diagnostics. Native GL still belongs to the coordinator's
display rerun. The newly added GL suite is a pure immutable-snapshot check.

Final focused Clang ASan/UBSan output (M)[AC]:

```text
editor_scroll_test: null fractional/core wheel, overscan, immutable refresh/repeat/missing/stale correlation PASS
editor_scroll_test: held worker cancellation retains snapshot, typing submits, allocations=0 guard=sanitizer-inert PASS
editor_scroll_test: fractional resize rebuilds clip/overscan and preserves origin PASS
editor_scroll_test: wrap-off EOF typing submits while bulk worker is held PASS
raster_scroll_test: exact Q8 translated pixels, bottom overscan, fixed chrome/sidebar PASS
gl_scroll_test: immutable Q8 plan/overscan snapshot, unchanged surface geometry PASS
editor_test: partial index deep typing reuses viewport line starts passed
P1-1 section 6: inactive exact counts/jump, bulk cancellation/retry, mallocs=0 guard=ASan-inert passed (M)[AC]
scroll_test: public editor/null wheel IDs correlate at submit/present/refresh; synthetic completion, physical opportunity stream unsupported PASS
```

The release-only existing burst guard also passed (M)[AC]:

```text
editor_test: null 10000 keys mallocs=0 guard=active
```

Its event population is derived from the existing allocation test's batch
loop, not a new test budget; the allocation count is the sum of its guard
results over those mutation/layout/submit segments. Sanitizer guard results
are explicitly inert and are not allocation evidence.

Local logs, test-by-test statuses, red compiler output, deterministic inputs
and helper reproducers are under `build/edit-3kk-evidence/`. Leak detection was
disabled because this sandbox cannot support the required LSan inspection;
the coordinator must repeat with leaks enabled.

The sandbox denies display access and Unix IPC binding/sending. Direct probes
returned `PermissionError(1, 'Operation not permitted')` (M)[AC] for connecting
to the assigned Xvfb socket and binding a socket inside this worktree. `1` is
the kernel's EPERM errno, not a budget. A C socket-pair probe also reproduced `send: Operation not permitted`;
its pair creation succeeded, so the launcher-wait subcase can fail later at
sending even after pair setup. Therefore a failure at IPC setup is
distinguished from a failure at native display setup. Tests were not weakened
or made to skip their assertions. The coordinator must rerun those suites
outside the sandbox on the assigned Xvfb display, with leaks enabled.

## Remaining and outside scope

- Real XShm/GL transformed pixels, native completion/MSC behavior and the next
  Astra/Sublime drive require the coordinator's display run. GL snapshot tests
  are not a shader or device validation. No physical latency/cadence pass is
  claimed.
- The hook accepts every explicit refresh opportunity, including repeats and
  missing IDs. Native backends currently publish completions, not every
  physical opportunity. That producer remains required for a physical G3z
  verdict; the callback labels the limitation instead of inventing opportunities.
- Exact resident wrap metadata and wrapped wheel routing are the separate
  visual-adapter integration requirement. This bead does not apply logical
  deltas to wrapped buffers. Coordinate pointer dispatch and inverse hit
  transforms with edit-73j using `scroll_frame_hit` and the submitted plan.
- The pre-existing layout/render typing path still reads piece data; this
  bead moves scroll's raw boundary acquisition to resident workers and does
  not redesign the layout source contract. A cold layout fault remains outside
  the scroll adapter's proof.
- `x11_order_test` continues after its denied fixture initialization and calls
  `plat_map` with no connection. Its final sanitizer log consequently contains
  a null-address fault in libxcb. This is the existing test's failure-path
  behavior after denied display setup; no X11/test teardown fix was made.
- Final model fuzzing is **not green**. It stopped at a pre-existing pure-view /
  oracle disagreement after Right at EOF. That result is detailed below; neither
  view behavior nor the oracle was weakened to hide it.


## Fuzzer observation outside this bead

The editor model driver was compiled with libFuzzer plus ASan/UBSan and linked
to the current sanitizer production archive. Sanitizers cover the production
code; coverage counters instrument this driver rather than the separate
`make fuzz` object tree. This is a model smoke, not a claim of a full instrumented
module campaign. Its runtime cap reuses the existing editor STATUS fuzz command
(G); the input bound comes from `MODEL_STEPS` times the model's event byte width.
Power before the final run was `Charging`, load1 `11.88` (M)[AC], read from the
same power/load sources used by the benchmark.

An earlier model smoke completed cleanly with `8086` inputs in `61` seconds
(M)[AC], both derived from libFuzzer's terminal counters. It preceded the final
EOF proof change and is historical evidence, not a final-source pass. The
final-source run stopped at this disagreement (M)[AC], values emitted by the
independent byte/selection model:

```text
editor_fuzz step=6 op=8 sym=65362 got=0/0 model=4/4 len=7
cc8161620a6364 before=7/7 preferred=18446744073709551615 actual_preferred=0
fuzz/editor_fuzz.c:153: assertion failed: s.cursor == m.cursor && s.anchor == m.anchor
```

The enum/key values identify Up; the maximum-width preferred-column value is
`VIEW_PREFERRED_UNSET`, derived from `UINT64_MAX`, not a duration or budget.
The fixture is a leading combining acute mark followed by `ab\ncd`. Model
length is the original fixture plus the UTF-8 combining bytes; expected cursor
byte is the combining prefix plus the first row's ASCII letters. Repro sequence:
insert that mark, Shift+Down, Down to EOF, Shift+Down, Shift+Right at EOF, Up.
The view retains the earlier preferred column, while the oracle expects Right
at EOF to reset it. Which policy Sublime uses was not investigated in this bead.

A separate executable calling only unchanged `piece`/`view` APIs reproduces
the same discrepancy, without editor or scroll calls:

```text
pure_view_expected_cursor=4 observed_cursor=0
```

The model's expected offset is the sum of the UTF-8 prefix and the ASCII
letters; the observed offset is the returned view state (M)[AC]. Reproducer:
`build/edit-3kk-evidence/view-combining-probe.c`; final fuzz artifact:
`build/edit-3kk-evidence/crash-21bf78c5b328b79403bd5155dd5d9654bd0c49c0`.
Per the one-bead rule, this finding is reported without changing src/view or
adjusting its model. The earlier stale-rectangle fuzz finding was within this
bead and was fixed with a red/green null resize regression.
