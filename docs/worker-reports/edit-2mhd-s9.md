# edit-2mhd worker report

Scope: minimap viewport grab offset, in the assigned worktree only. No `bd`
or `git` commands were run. Implementation and required verification are complete.

## Files touched

- `src/editor/paint.c`: capture the painted viewport's pointer offset on a
  primary press; inside presses preserve the viewport, outside presses use its
  centre; subtract the fixed pixel offset before existing hit resolution.
- `src/editor/private.h`: embedded scalar drag offset.
- `tests/editor_test.c`: inside/outside drag regressions in the normal suite
  and selectable cases, including line/byte assertions and an allocation
  guard; update the existing minimap click expectation for centring.
- `docs/decisions/edit-2mhd.md`: grab rule and geometry choice.
- `docs/worker-reports/edit-2mhd-s9.md`: this report.

## Evidence and command derivations

(P) Build concurrency `-j3`, display `:99`, and
`EDIT_DISPLAY_LOCK_TIMEOUT=1800` come directly from the brief.
`ASAN_OPTIONS=detect_leaks=1` enables its required leak check. Exit status
`0` denotes success; `1` below denotes the deliberately failing assertion.
Source line numbers are compiler-provided assertion locations.

(E) Test geometry reuses the existing minimap compose fixture: columns `40`,
rows `8`. (P) The configured rows minus the tab row give `8 - 1 = 7` visible
rows. Initial top is `7`; grab offset and motion are `floor(7 / 2) = 3` rows.
The alphabet fixture has `sizeof "a\n" - 1 = 2` bytes per line. Existing
minimap geometry uses `2` pixels per summary; the test clicks its second
scanline to exercise a pixel offset within the row. Inside click:
`7 + 3 = 10`, expected top after motion `7 + 3 = 10`. Outside click:
`7 + 7 + 3 = 17`, expected centred top `17 - 3 = 14`, then `14 + 3 = 17`
after motion. Expected byte positions are these line positions times the
fixture byte stride. Allocation expectation `0` follows the no-malloc law.
Observed output and power/load stamps are (M)[AC].

## Red run, before production changes

`make -j3 build/san/tests/editor_test` exited `0`.

For each selector below, run with a fresh `runtime=$(mktemp -d)` and remove
that directory on shell exit:

```sh
cat /sys/class/power_supply/BAT0/status
uptime
XDG_RUNTIME_DIR="$runtime" DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 \
  ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh \
  build/san/tests/editor_test --minimap-grab-inside
# Repeat with --minimap-grab-outside.
```

```text
Charging
 02:23:45 up 48 min,  1 user,  load average: 41.47, 31.26, 15.89
editor_test:370: FAIL moved_line == expected + motion && moved_byte == (expected + motion) * stride
edit-2mhd inside: press top=10 expected=7; motion top=13 expected=10; mallocs=0 guard=ASan-inert
red inside exit=1
Charging
 02:23:46 up 48 min,  1 user,  load average: 41.47, 31.26, 15.89
editor_test:370: FAIL moved_line == expected + motion && moved_byte == (expected + motion) * stride
edit-2mhd outside: press top=17 expected=14; motion top=20 expected=17; mallocs=0 guard=ASan-inert
red outside exit=1
```

The initial sandboxed selectors each exited `1`: they reached the intended
assertion, then LeakSanitizer reported its ptrace restriction. The captured
red run above used the approved outside-sandbox execution with leak checking
enabled. All subsequent sanitizer runs used that execution mode.

## Green run and verification

Sanitizer rebuild command:
`make -j3 build/san/tests/editor_test build/san/tests/editor_scroll_test`
exited `0`. The red selectors, with the same display/leak environment, now
produce:

```text
Charging
 02:26:57 up 52 min,  1 user,  load average: 38.00, 34.90, 20.22
waiting for display :99 (held by 66196)
edit-2mhd inside: press top=7 expected=7; motion top=10 expected=10; mallocs=0 guard=ASan-inert
green inside exit=0
Charging
 02:31:29 up 56 min,  1 user,  load average: 29.52, 31.98, 22.81
waiting for display :99 (held by 90215)
edit-2mhd outside: press top=14 expected=14; motion top=17 expected=17; mallocs=0 guard=ASan-inert
green outside exit=0
```

All build/run commands below exited `0`. The display commands shared a
fresh private runtime directory, removed on shell exit, and this environment:

```sh
export XDG_RUNTIME_DIR="$runtime" DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800
export ASAN_OPTIONS=detect_leaks=1
```

| Verification command | Exit |
| --- | --- |
| `make -j3 build/san/tests/editor_test` (red build) | `0` |
| `make -j3 build/san/tests/editor_test build/san/tests/editor_scroll_test` (green build, Makefile sanitizer targets) | `0` |
| `make -j3 build/tests/editor_test` | `0` |
| `make -j3 all` | `0` |
| `sh tools/with_display_lock.sh build/san/tests/editor_test --minimap-grab-inside` | `0` |
| `sh tools/with_display_lock.sh build/san/tests/editor_test --minimap-grab-outside` | `0` |
| `sh tools/with_display_lock.sh build/san/tests/editor_test` | `0` |
| `sh tools/with_display_lock.sh build/san/tests/editor_scroll_test` | `0` |
| `sh tools/with_display_lock.sh build/tests/editor_test --minimap-grab-inside` | `0` |
| `sh tools/with_display_lock.sh build/tests/editor_test --minimap-grab-outside` | `0` |
| `EDITOR_ALLOC_ONLY=null sh tools/with_display_lock.sh build/tests/editor_test` | `0` |

The release build used gcc; sanitizer builds used clang with ASan/UBSan.
The Makefile's C standard and warning flags were retained. Suite completion
excerpts follow; the complete logs are under `build/edit-2mhd-s9/`.
Their diagnostics/counts are the existing test fixtures' output (M)[AC].

```text
editor_test: all passed
editor_test sanitizer exit=0
Charging
 02:40:22 up  1:05,  1 user,  load average: 24.62, 27.50, 24.81
editor_scroll_test: wrapped exact visual-row wheel and reversal PASS
editor_scroll_test: refused scroll snapshot defers the slice, keeps the window, notices persistence PASS
editor_scroll_test sanitizer exit=0
Charging
 02:45:09 up  1:10,  1 user,  load average: 19.08, 22.75, 23.57
```

Release allocation evidence (M)[AC]: (P) expected allocations are `0` by
the no-malloc law. The typing count `10000` is inherited from the existing
`allocation_case` loop; no new stress count was introduced.

```text
Charging
 02:45:30 up  1:10,  1 user,  load average: 17.93, 22.26, 23.39
edit-2mhd inside: press top=7 expected=7; motion top=10 expected=10; mallocs=0 guard=active
release inside exit=0
Charging
 02:45:30 up  1:10,  1 user,  load average: 17.93, 22.26, 23.39
edit-2mhd outside: press top=14 expected=14; motion top=17 expected=17; mallocs=0 guard=active
release outside exit=0
Charging
 02:45:30 up  1:10,  1 user,  load average: 17.93, 22.26, 23.39
editor_test: null 10000 keys mallocs=0 guard=active
release allocation guard exit=0
Charging
 02:45:30 up  1:10,  1 user,  load average: 17.93, 22.26, 23.39
```

## Existing editor benchmark

Command, under the same display lock/environment, exited `0`:

```sh
cp src/editor/paint.c "$bench_fixture"
read -r -a bench_args < bench/editor_bench.args
sh tools/with_display_lock.sh build/bench/editor_bench "${bench_args[@]}" "--file=$bench_fixture"
```

(E) Arguments are the checked-in `bench/editor_bench.args` verbatim:
`--track --no-idle --no-p4 --serial-only --keys=64`; the sample count is
inherited from that fixture. (P) Input is a private copy of the changed source,
not a new large corpus; an EXIT trap removes it. Cleanup was verified with
`test ! -e build/edit-2mhd-s9/bench-fixture.c`, exit `0`.

(M)[AC] The following nanosecond values are copied from the existing driver's
`*_injection_T4_G1` records: recorded input-injection to T4 deltas, summarised
by its existing statistic routines. All backend rows reported `structural=OK`
and `allocations=0`; the allocation expectation follows the no-malloc law.
Power/load stamps bracket this timing excerpt:

```text
Charging
 02:45:30 up  1:10,  1 user,  load average: 17.93, 22.26, 23.39
```

| Requested / actual backend | p50_ns (M)[AC] | p99_ns (M)[AC] |
| --- | ---: | ---: |
| null / null | 2354535 | 2611377 |
| raster / cpu-raster | 34438703 | 50345758 |
| gl / cpu-raster fallback | 33937378 | 56509472 |

```text
editor benchmark exit=0
Charging
 02:45:40 up  1:10,  1 user,  load average: 17.02, 21.92, 23.27
```

The measured TRACK timings exceed the existing benchmark aspirations on the
loaded shared box. The GL request reported EGL/GL/Present unavailable and
used the existing raster fallback; native GL performance was not measured.
These observations are outside this grab-offset bead and were not changed.

## Fixture/disk accounting

No GB-scale on-disk fixture was created. The private source-copy benchmark
fixture was removed; ordinary build products and validation logs remain.
(M)[AC] `df -h /` before and after, observed on the shared filesystem:

```text
Before:
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  379G   77G  84% /
After:
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p5  480G  386G   71G  85% /
```

## Remaining work and outside-scope findings

The bead's implementation and required checks are complete. The decision
note records the grab rule. Existing grouped/sampled byte-to-line mapping
and endpoint clamping remain the navigation contract.

Outside this bead: the benchmark's shared-box timing misses and unavailable
native GL path above. No additional source defect was found or fixed. No
new review pass, installation, tracking action, or version-control action
was performed.
