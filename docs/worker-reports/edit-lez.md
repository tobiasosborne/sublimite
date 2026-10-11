# edit-lez — trace-driven regex whole-word continuation

The review section numbers below identify findings in `P4-modules-2.md`; they
are identifiers, not measured quantities. Continued from the `edit-lez-s9*`
reports and decision record. This slice fixes the remaining persistent regex
whole-word traversal in §22. The bead remains incomplete for §25, §29 and §30.
The worker/counted-core red traces supplied the evidence required by the backlog
triage rule. No `bd` or `git` command was run; repository edits stay in this
worktree. No editor internals or Makefile changes.

## Files and behavior

- `src/find/find.c`: shared regex traversal with ASCII word-boundary filtering;
  persistent request reader, boundary reader, prefix plan and retry/cancellation
  meter. Accepted matches keep non-overlap semantics; rejected candidates retry
  from their next starting byte. Empty matches retain source-end termination.
- `src/find/visit.h`: additive internal `find_regex_word_visit` entry point.
  All legacy entry points and frozen `src/find/find.h` remain unchanged.
- `src/findui/worker.c`: whole-word regex uses the bounded visitor instead of
  repeated next calls and separate snapshot reads. Exact visible cardinality
  still counts discarded highlights; publication remains bounded.
- `src/findui/findui.h`: clarifies that the existing scan hook marks matcher
  traversals; internal boundary reads are metered inside the traversal. No
  findui signature or structure changes; host mutations and leases stay stable
  for edit-73j. Hooks no longer fire separately for each regex candidate/read.
- `tests/findui_test.c`: active regression for actual worker scan-stage count,
  bounded output, prefix/no-prefix regexes, rejected overlaps, anchors, empty
  matches, underscore and high-byte boundaries. The environment selector runs
  the same regression in isolation; it also runs in the default suite.
- `tests/find_p1r_test.c`: counted production-copy prefix-plan regression and a
  cancellation sweep across every checkpoint measured in a successful filtered
  traversal. Default suite includes it.
- `fuzz/find_fuzz.c`: independent endpoint-set oracle for accepted whole-word
  ranges, selected/prefix/visible publication and exact visible cardinality on
  flat and fragmented sources.
- `bench/find_probe.h`: aliases the new internal export in the benchmark's
  private source copy, matching its existing alias scheme. The initial release
  build exposed a duplicate-symbol link error until this alias was added.
- `docs/decisions/edit-lez.md`, module `STATUS.md` files, `HANDOFF.md` and the
  current worklog record the design, verification and remaining integration.

No production allocation, global mutable state, UI document scan or typing-path
work was added. Full findui verification includes its active allocation guard.
The existing restarted anchored NFA and shared work-limit policy remain; there
is no new general regex linearity or displayed-latency claim.

## Derivations and red run

(G) `F = FIND_MAX_OFFSETS = 4096`, from the frozen find header's required offset
capacity. The fixture is `2 * F` bytes: an `a ` pair per slot, hence exactly `F`
accepted words. Its independent prefix/visible caches each reserve (G) one range,
the minimal nonempty capacity. Publication is bounded by prefix + visible +
requested ordinal, at most (G) `1 + 1 + 1` ranges; this fixture produces (M) one
because the initial requests refer to the same first range.

The red worker trace has `F + 1` matcher calls (including the final miss),
`F - 1` left-boundary reads and `F` right-boundary reads: `(F + 1) + (F - 1) + F
= 3 * F = 12288` stages. The red core prepares a prefix plan for each matcher
seek: `F + 1 = 4097`. Green requires (G) one matcher traversal and one prefix
plan per request, independent of candidate count. Semantic fixture counts and
byte offsets derive from their indexed literal strings, documented in the test.
The fuzzer's boundary storage derives from the existing endpoint model's subject
byte domain plus its terminal boundary.

Built the failing tests first with Clang ASan/UBSan and the required C warning
flags. Runtime red evidence, before production edits:

```sh
EDIT_LEZ_REGEX_WORD=1 ASAN_OPTIONS=detect_leaks=0 build/san/tests/findui_test
```

(M)[AC], power `Charging`; load1 (M) `29.45`, read from `/proc/loadavg`:

```text
S22 regex word trace: pattern=a+ count=4096 stages=12288 ranges=1
tests/findui_test.c:758: assertion failed: __c11_atomic_load(&f.hook.scans, 5) == 1
exit=134
```

```sh
EDIT_LEZ_REGEX_WORD=1 ASAN_OPTIONS=detect_leaks=0 build/san/tests/find_p1r_test
```

(M)[AC], power `Charging`; load1 (M) `30.25`, read from `/proc/loadavg`:

```text
FAIL tests/find_p1r_test.c:153: probe.regex_plans == 1
S22 regex prefix trace: count=4096 plans=4097
exit=1
```

Exit codes are (M) observed process statuses: the assertion abort is the shell's
signal-exit convention, and the core test returns failure from its `CHECK`.
Source line numbers and atomic-order values in assertions are literal compiler
output, not performance bounds. Logs: `/tmp/edit-lez-red-ui.log` and
`/tmp/edit-lez-red-core.log`.

## Green run

Rebuilt after the implementation, using the same sanitizer configuration and
commands. (M)[AC], power `Charging`; load1 (M) `33.24`, read from `/proc/loadavg`:

```text
S22 regex word trace: pattern=a+ count=4096 stages=1 ranges=1
S22 regex word trace: pattern=[a] count=4096 stages=1 ranges=1
S22: PASS persistent regex whole-word visitor, boundaries, empty matches and rejected overlap
exit=0
S22 regex prefix trace: count=4096 plans=1
exit=0
```

Logs: `/tmp/edit-lez-green-ui.log`, `/tmp/edit-lez-green-core.log`.
The complete findui suite also passed, including its allocation guard:

```text
findui_test: allocator faults and replacement cancellation preserve one exact undo group passed
findui_test: mailbox backpressure/cancel, source versions, alias edits and empty windows passed
findui_test: bottom-row rendering, damage validation and typing allocation guard passed
findui_test: all passed
exit=0
```

Full log: `/tmp/edit-lez-findui-full.log`. The final full sanitizer suite below
also recompiles the additional cancellation-checkpoint sweep.

## Release, fuzz and TRACK verification

`make -j"$(getconf _NPROCESSORS_ONLN)" all` passed with GCC release and all required
C warning flags. Parallelism derives directly from online processors, not an
invented workload budget. Compiler identities were read with `--version`.
The final build log is `/tmp/edit-lez-all-final.log`. It ends with:

```text
gcc -O2 -g -msse2 -pthread build/rel/bench/find_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/find_bench
exit=0
```

Clang built `build/fuzz/find_fuzz` and `build/fuzz/findui_fuzz`. Each campaign used
(G) `-runs=4096`, derived from `F`, as a finite coverage probe, not a statistical
qualification; `-max_len=384` reuses the existing findui fuzzer input cap.
Durations below are (M) libFuzzer's observed wall time, not assigned deadlines.
Power was read before each run and was `Charging` (M)[AC].

Core launch load1 (M) `53.56`; panel launch load1 (M) `61.81`, read from
`/proc/loadavg`. Both exited successfully with no sanitizer/oracle finding:

```text
find_fuzz:   Done 4096 runs in 16 second(s)
exit=0
findui_fuzz: Done 4096 runs in 187 second(s)
exit=0
```

Logs: `/tmp/edit-lez-find-fuzz.log`, `/tmp/edit-lez-findui-fuzz.log`.

Existing `build/bench/find_bench --quick` passed, TRACK only; its quick-mode
sample count and scaled fixture sizes come from the existing harness, with
measured times from its clock. Log: `/tmp/edit-lez-find-bench.log`.

```text
find_bench: PASS (quick TRACK only; full gates not tested)
exit=0
```

The existing panel benchmark `build/bench/findui_bench --word --probe --track`
verified exact counts and first offsets. Probe mode requests (G) one observation
per variant, derived from that existing CLI mode; it exercises the initial
window only. The source byte count comes from the unchanged corpus fixture's
filesystem size. All times and identical single-sample percentiles below are
(M)[AC], nanoseconds from the benchmark clock; power `Charging`, launch load1
(M) `85.27`. Existing gate constants in its log come from the harness/performance
reference, not new limits.

```text
WORKLOAD findui bytes=1073741824 mapping=NEW samples=1 kind=G6_full_source (G)
plain a-space: p50=p99=713721190 ns
word a-space:  p50=p99=8016779927 ns
findui_count: exact 1073741824-byte total/first 4096 checked; initial/late/empty windows; mapping=NEW; (M)[AC]
exit=3
```

The benchmark's completion message lists all window modes even for a probe;
this run selected the initial mode by its existing sample-index expression.
The nonzero exit is its existing insufficient-sample rejection in
`bench/findui_count.h`, despite TRACK mode. This is an out-of-scope harness item
for edit-lwq, reported without changing it. No timing miss is a blocker or a new
bead here. Log: `/tmp/edit-lez-findui-bench.log`.

## Remaining and coordinator ownership

Review §25 still needs bounded multibyte/regex late-window scans and immediate
window priority; query counts and selection already remain stable from prior
work. Review §29 needs actual editor mutation binding, and §30 needs editor
buffer eviction/reload and independent save-storage leases. The existing
findui host/lease APIs stay available for the dedicated editor worker. Generic
regex candidate simulation, prefix snapshot seeks and replacement-page rescans
retain their documented limitations.

The coordinator reruns display suites on the supplied safe Xvfb display,
LeakSanitizer enabled outside this sandbox, then the next Astra comparison drive
on the same build. This worker cannot certify Sublime-relative frame latency or
hand the editor to Tobias. No additional bead was pulled and no unrelated source
defect was repaired.

## Final sanitizer manifest and sandbox exceptions

Ran the actual `make check` with Clang ASan/UBSan after the GCC build. The initial
attempt built the sanitizer binaries, passed base and waited on another
process's display lock at cli. Interrupted that already-blocked command; its
observed shell status was (M) interruption, not a successful check. Repeated:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=0 \
XDG_RUNTIME_DIR="$PWD/build/lez-runtime" ASAN_OPTIONS=detect_leaks=0 make check
```

The display identifier is (G) the safe Xvfb destination supplied in the brief.
The lock setting is (G) a nonblocking acquisition, derived from avoiding any
wait on an unrelated owner while the sandbox cannot access X; it is not a
new elapsed-time budget. Observed result (M):

```text
base_test: PASS
== build/san/tests/cli_test
waiting for display :99 (held by unknown)
cli_test: display lock timeout: display :99 (held by unknown)
make: *** [Makefile:105: check] Error 1
exit=2
```

Log: `/tmp/edit-lez-check-final.log`. The initial build/check log is
`/tmp/edit-lez-check.log`. Makefile line numbers and exit statuses are literal
observations, not gates. Continued directly through the remaining binaries and
check shell contracts, retaining each suite's output and power/load metadata
under `/tmp/edit-lez-suites/`. The actual manifest is
`/tmp/edit-lez-suites/summary.txt`.

(M)[AC] **46 successful binary results out of 63 binary manifest entries**:
these counts derive by counting `_test` entries and their `exit=0` fields in
that manifest. The remainder derives as `63 - 46 = 17`: ordinary failures plus
an interrupted editor run described below. Shell contracts are separate from
that binary count. All find/findui suites pass:

```text
find_bench_test: exit=0
find_bestof_test: exit=0
find_p1r_test: exit=0
find_simd_filter_verify_test: exit=0
find_test: exit=0
find_timeout_test: exit=0
findui_test: exit=0
replay_cli: exit=0
```

Every other pure module suite completed successfully except `ipc_test`, whose
local socket bind is denied. The complete native/mixed editor suites cannot
complete successfully here. I do **not** claim that every nondisplay suite or
the full `make check` passed.

Display-required failures (each observed failure status in the manifest):

- `editor_close_test`: its native backend case fails `editor_open`; its headless
  close case prints PASS first.
- `file_kill_test`: the viewport fixture fails `view_init`, which opens the
  platform; its queued view children fail too. This suite needs a display even
  though Makefile's display-lane filter does not include its name. Reported only.
- `raster_test`: native platform initialization fails; its pure rendering and
  completion cases print GREEN.
- `refproto_test`, `refwin_timeout_test`: connection initialization fails.
- `refwin_test`: its internal shared-display wrapper cannot acquire the
  externally held lock with the nonblocking setting.
- `x11_clip_review_test`, `x11_clip_test`, `x11_identity_test`, `x11_live_test`,
  `x11_order_test`, `x11_stall_test`: display fixture/platform initialization
  fails. No live X behavior was verified.

Additional socket/environment exceptions:

- `cli_test` and `editor_p4_test` fail local IPC server initialization.
- `ipc_test` reports `init rc=4 errno=1 (Operation not permitted)`. These are
  (M) its actual return/errno values; symbolic cause is `EPERM` on bind.
- `editor_large_test` initially exceeded its existing cooperative-job deadline
  under the load stamped in its log; the first retry did too. On the final
  lower-load retry (M)[AC], power `Charging`, load1 (M) `15.38` from
  `/proc/loadavg`, warm/find cooperation and the subsequent pure large-buffer
  cases passed. It then failed `mapped_startup_ipc` at server bind. Thus the
  final full-suite result is still failure, without an unresolved deadline
  claim. Logs: `/tmp/edit-lez-editor-large-retry.log` and
  `/tmp/edit-lez-editor-large-final.log`. No deadline or source was changed.
- `editor_test` reported native/source initialization failures and did not
  finish the mixed suite. Interrupted it after those recorded failures so it
  could not prevent validation of the independent module suites. The manifest's
  interruption field records the observed worker-command signal status; it is
  not a claimed complete binary verdict. Its additional buffer-prepare failure
  was not diagnosed or repaired outside scope.
- `display_lock_contract` cannot acquire its holder fixture's existing shared
  lock. Its output names the external owner as unknown in this process
  namespace. No lock/harness code was changed.

Direct environment probes confirmed `AF_UNIX` socket creation succeeds, but
binding an own-worktree socket and connecting to the supplied X socket both
raise `EPERM`. Local IPC therefore needs an external rerun as well as the X
suites. `gl_gate_test` and `gl_test` pass their pure/mock checks; `gl_test`
explicitly reports `native diagnostics SKIP`. X alignment/input/identity-policy
mock tests that passed do not certify a live display.

Both sanitizer and release null-backend editor allocation-only runs passed.
(G) The unchanged fixture generates `100 * 100 = 10000` key events, from its
existing batching loops. Measured allocator observations (M)[AC]:

```text
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
exit=0
editor_test: null 10000 keys mallocs=0 guard=active
exit=0
```

The zero allocation requirement derives from CLAUDE.md's typing-path law. Logs:
`/tmp/edit-lez-editor-alloc.log`, `/tmp/edit-lez-editor-alloc-release.log`.

A direct leak-enabled probe failed with the sandbox restriction, (M):

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
exit=1
```

Log: `/tmp/edit-lez-lsan-probe.log`. Consequently the sanitizer runs above use
`detect_leaks=0`; the coordinator must rerun with leaks enabled outside the
sandbox. Source/doc whitespace checks are clean. No other production source
changes were made after the green builds.

This slice is ready for coordinator review, not bead closure or Tobias hand-over:
GCC builds, scoped sanitizer tests/fuzz and active typing-allocation verification
are green; the full acceptance run retains the exact external reruns above and
the documented viewport/editor integration work.
