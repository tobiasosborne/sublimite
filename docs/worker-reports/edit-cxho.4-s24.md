# edit-cxho.4 worker report

Pure parser implementation and required Linux verification complete.
Scope is parser, tests, fuzz entry, and build discovery only.

## Files touched

| File | Change |
| --- | --- |
| `src/dnd/dnd.h` | Public arena/request API, result codes, skipped/overflow counts and ownership contract. |
| `src/dnd/dnd.c` | Local file URI and absolute-path parser; UTF-8 validation; atomic arena failure. |
| `tests/dnd_test.c` | Acceptance cases, IPC boundaries, ownership, errors and allocation guard. |
| `fuzz/dnd_fuzz.c` | Arbitrary bytes, entry/storage invariants and percent-encoding oracle. |
| `Makefile` | Documents existing automatic pure-module/test/fuzzer discovery. |
| `mk/win.mk` | Adds the pure module to `WIN_MODULES`. |
| `docs/decisions/edit-cxho.4.md` | API and accepted/rejected form choices. |
| `docs/worker-reports/edit-cxho.4-s24.md` | This report. |

## Red then green

Wrote the test suite and public contract before implementing the parser.
An empty-output scaffold built successfully using
`make -j4 build/san/tests/dnd_test`, then the sanitizer test failed on the
expected CRLF list result. The build parallelism comes from the brief (P);
exit statuses below are observed command results (M)[AC], with zero the shell
success convention (P).

```text
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/dnd_test
dnd_test:17: FAIL request->count == count
dnd_test:42: FAIL paths_equal(&request, want, sizeof want / sizeof want[0]) == 0
red test exit: 1
```

After implementation, rebuilt using the same Makefile sanitizer target:

```text
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/dnd_test
dnd_test: all parser cases passed
green test exit: 0
```

The first sandboxed red invocation also produced LeakSanitizer's ptrace
restriction diagnostic. Repeated the red test outside the tracing sandbox
before implementation; the red output above is that clean assertion failure.
Green sanitizer verification also ran outside the tracing sandbox with leak
detection enabled.

## Verification

Observed exit statuses (M)[AC]; build parallelism, fuzz run count and maximum
input length come from the brief (P: its bounded-run example and oversized
path acceptance fixture). No timing threshold was added.

| Command | Exit |
| --- | --- |
| `make -j4 build/san/tests/dnd_test` (red scaffold and green implementation) | `0` |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/dnd_test` (red scaffold) | `1`, expected assertion failure |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/dnd_test` (implemented) | `0` |
| `make -j4 all` | `0` |
| `build/tests/dnd_test` | `0` |
| `nm -g --defined-only build/rel/src/dnd/dnd.o` | `0`; only `dnd_parse_uri_list` exported |
| `make -j4 build/fuzz/dnd_fuzz` | `0` |
| `ASAN_OPTIONS=detect_leaks=1 build/fuzz/dnd_fuzz -runs=200000 -max_len=5000 build/dnd-verification/corpus` | `0` |
| `ASAN_OPTIONS=detect_leaks=1 EDIT_REFUSE_REAL_DISPLAY=1 EDIT_DISPLAY=:99 DISPLAY=:99 XDG_RUNTIME_DIR="$check_runtime" make -j4 check` | `0` |

The release test exercises the counting malloc guard around decoding. Fuzz
seeds come directly from the acceptance fixtures. ASan/UBSan fuzz result
and adjacent power/load stamps (M)[AC]:

```text
Before fuzz (M)[AC], BAT0 and load:
Charging
 11:15:11 up 1 day, 13:18,  1 user,  load average: 1.83, 2.58, 2.33
#200000 DONE   cov: 335 ft: 1640 corp: 350/251Kb lim: 5000 exec/s: 12500 rss: 392Mb
Done 200000 runs in 16 second(s)
After fuzz (M)[AC], BAT0 and load:
Not charging
 11:15:28 up 1 day, 13:18,  1 user,  load average: 3.13, 2.84, 2.42
```

Full check used `check_runtime=$(mktemp -d)` with an exit/signal trap to remove
it. The Makefile routes display suites through `sh tools/with_display_lock.sh`;
the assigned display was reachable. The display number and real-display guard
derive from the brief (P). Leak detection stayed enabled throughout. Final
summary and adjacent power/load/disk stamps (M)[AC]:

```text
Before full check (M)[AC], BAT0, load and disk:
Charging
 11:15:12 up 1 day, 13:18,  1 user,  load average: 1.83, 2.58, 2.33
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  145G  1.6T   9% /
check: 111 test binaries passed
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
After full check (M)[AC], BAT0, load and disk:
Charging
 11:26:21 up 1 day, 13:29,  1 user,  load average: 0.44, 0.99, 1.64
Filesystem               Size  Used Avail Use% Mounted on
/dev/mapper/vgmint-root  1.8T  147G  1.6T   9% /
make -j4 check exit: 0
```

Existing full-suite sparse large-file fixtures ran unchanged; this bead
introduces no large on-disk fixture. Build/run logs and fuzz corpus remain
under `build/dnd-verification/`. The report includes the red, green and fuzz
result lines above; full logs also retain the existing suites' diagnostics.

## Remaining / outside scope

X11 target support and editor drop handling remain the following beads.
Directories are deliberately passed through. An empty parsed request must
be ignored by its editor caller; dispatching it creates an untitled buffer
under the existing editor contract. Overflow is reported for caller policy.

No required parser work remains. No unrelated source defect was found.

Windows module discovery is wired, but the Windows toolchain/run is outside
this Linux brief and was not attempted. No editor or platform sources were
changed. No additional review pass was run.
