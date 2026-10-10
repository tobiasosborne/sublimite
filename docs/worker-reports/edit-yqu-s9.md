# edit-yqu — session 9 worker report

This bead is **incomplete; do not close it**. The edits stay within the assigned
bench/test/fuzz files and src/savectl for permission validity. No Git mutations,
bd, HANDOFF.md, docs/worklog edits, Makefile edits, or new state globals.
Production changes are only the setup-time creation permission options and
worker argument forwarding. No allocation was added to the typing path.

## Status by finding

- Benchmark continuation (a): **partial**. Default scroll jump, both savectl
  save sizes, and findui cancellation now request the shared interaction
  minimum (G) 10000; storage matches the counts. Compile assertions reject the
  old counts. All three .args files were deleted. Findui cancellation emitted a
  qualified row through make bench. Scroll exits before its first row because
  of the pre-existing single-call sliced-index incompatibility (§5, outside
  this bead). The savectl run timed out before printing a row. The optional
  findui --count row remains limited to its old counts in bench/findui_count.h;
  that helper is outside the explicitly assigned file list. Full make bench
  acceptance and the required pasted scroll/save rows remain missing.
- §33: **done**. Every savectl test, bench and fuzz pool (including journal
  pools in the new scenarios) uses aligned_alloc and asserts its address before
  initialization. work_pool_init initializes the storage.
- §34: **done**. Normal release and sanitizer builds enforce UI I/O guards
  and run the deterministic held-publication schedule. Interposition covers
  calls from the controller and the linked file library. Separate mutation
  probes demonstrated rejection of UI stat and completion-after-wake behavior.
- §35: **partial**. The original short-line pixel model remains.
  Added long newline-free regions exceeding the proof budget, exact visibility
  after index completion, unchanged-state MORE, staged callback failures with
  unchanged pending state, and source/index insertions/deletions. The scalar
  oracle independently transforms anchors, including deleting their preceding
  newline. Fully indexed delegated source-error schedules remain missing;
  this does not fix the separate module findings §2–4.
- §36: **partial**. Random small prefix/visible capacities, overflow and lazy
  ordinals, larger subjects, raw literal queries, malformed classes/escapes,
  partial drains and interleaved source/query/window changes, deterministic
  running-worker cancellation/source retirement, query boundary lengths,
  initialization allocation failure, replacement allocator faults, expired
  replacement deadlines and cancellation are covered. Full arbitrary regex
  grammar and the default case-folded long-query regression (§21) remain.
  The endpoint-set oracle deliberately keeps raw arbitrary queries literal.
- §37: **partial**. Added private journal pools, prepare/finish/invalid
  checkpoints, BASE_CHANGED, retained-token refusal, successful retry,
  controlled sync failure and fresh-inode rotation, paused save races/partial
  drains, mapped-mode validator acquisition failures, missing/nonregular
  targets, missing-directory I/O failure, and reload metadata allocation fault
  with preserved outputs and retry. Real mapped backing/fault-epoch acquisition, deterministic multi-chunk
  mid-reload rewriting/restored-mtime schedules and fuller slot-reuse schedules
  remain.
- §39: **done**. create_mode/create_mode_valid are forwarded. Tests cover
  explicit zero, unset default, ignored invalid numeric hint, and explicit
  nonzero bits under a restrictive process umask. Existing file-core defaults
  and legacy baseline hints remain compatible. See the decision document.

## Red runs, written before fixes

Benchmark sample assertions, while constants still had the old counts:

```text
bench/savectl_bench.c:13:1: error: static assertion failed: "G8 small needs qualified samples"
bench/savectl_bench.c:14:1: error: static assertion failed: "G8 large needs qualified samples"
bench/scroll_bench.c:17:1: error: static assertion failed: "G7j needs qualified samples"
bench/findui_bench.c:17:1: error: static assertion failed: "G6c needs qualified samples"
```

§33, alignment assertions added while allocation still used calloc:

```text
tests/savectl_test.c:49: (uintptr_t)pool % _Alignof(work_pool) == 0
```

§34, required-instrumentation check first failed in the ordinary configuration:

```text
tests/savectl_test.c:452: false
```

That temporary configuration check was replaced by unconditional normal-build
instrumentation. Subsequent negative mutation probes (temporary mutations
removed) established that the actual guards reject the two regressions:

```text
tests/savectl_test.c:18: !ui_no_io
tests/savectl_test.c:138: savectl_get_model(f.s).state==SAVECTL_SAVED
```

§35, a long-line coverage contract first rejected the former short-line
construction at difficult_model's gap > SCROLL_SCAN_BUDGET assertion:

```text
SUMMARY: libFuzzer: deadly signal
```

§36, a boundary-capacity coverage contract first rejected the former
fixed-capacity configuration:

```text
fuzz/findui_fuzz.c:224: assertion failed: config.match_capacity <= 64 && config.visible_capacity <= 64
SUMMARY: libFuzzer: deadly signal
```

§37, a journal-session coverage contract first rejected the former journal-free
setup:

```text
fuzz/savectl_fuzz.c:82: assertion failed: options.journal != ((void*)0)
SUMMARY: libFuzzer: deadly signal
```

§39, the new test first failed compilation because the fields did not exist:

```text
error: ‘savectl_options’ has no member named ‘create_mode’
error: ‘savectl_options’ has no member named ‘create_mode_valid’
```

After adding the fields, BEFORE forwarding validity, the same test failed
behaviorally on the permission bits:

```text
tests/savectl_test.c:465: base_id(f.path).mode == expected
```

## Green runs and benchmark evidence

All numerical harness results below are (M)[AC]; all configured thresholds and
sample requirements are (G). Loaded timings are **only verdict-logic evidence**,
not a performance gate acceptance or a variant comparison.

Qualified findui cancellation through the actual make bench recipe, with its
.args file absent:

```text
== build/bench/findui_bench
findui_bench: power=Charging [AC] load1=2.18 fixture=/tmp/edit-corpus/log_1g.txt track=0
BENCH name=G6c_findui_keystroke_logical_ack n=10000 required_n=10000 p50=1727 p99=7870 ci95_p50=[1713,1740] ci95_p99=[6267,8688] gate_p50=1000000 gate_p99=5000000 dropped=0 (M)[AC] power=Charging load1=2.18 verdict=PASS
findui_scan_thread_check (M)[AC] load1=2.18 calling_thread_scans=0 expected=(G)0
== build/bench/scroll_bench
make: *** [Makefile:113: bench] Error 2
```

A separate make bench invocation restricted to savectl was bounded by a
(G) 120-second timeout and did not produce a completed row:

```text
== build/bench/savectl_bench
make: *** [Makefile:113: bench] Terminated
```

Its default large-save workload writes roughly (E) 10 TiB for the required
samples. No reduced fixture, weakened gate or lower sample minimum was used.

Release and ASan/UBSan savectl tests pass after alignment, instrumentation and
permission forwarding:

```text
savectl normal-build I/O and held-publication guard: ok
savectl creation permissions/default/umask: ok
savectl journal prepare/finish/replay/failures/retained generation: ok
savectl full-mailbox completion fallback: ok
savectl_test: ok
```

Timed fuzz runs, clean ASan/UBSan, ASAN_OPTIONS=detect_leaks=0 throughout:

```text
scroll:  Done 780 runs in 62 second(s)
findui:  Done 1958 runs in 61 second(s)
savectl: Done 365 runs in 61 second(s)
```

Stamps: scroll power Charging, load1 (M)[AC] 5.81; final findui power Charging,
load1 (M)[AC] 7.79; final savectl power Charging, load1 (M)[AC] 27.51.
The final findui run includes replay of its enlarged starting corpus followed
by mutation, with the replacement fault code and preserved selection on no-op
option/window updates. Earlier mutating runs also passed.

The first expanded fuzz runs found two **harness oracle mistakes**, both fixed:
raw literals were later toggled into regex grammar beyond the oracle's support;
and a notified save race's subsequent file check legitimately replaced its
file-error diagnostic. Both reproducers were replayed successfully before the
clean runs. No product bug was suppressed or fixed outside this bead.

GCC release make all exited 0. The full sanitizer suite passed before the final
forced rebuild:

```text
check: 59 test binaries passed
test_replay_cli: all passed
```

A system clock jump made existing generated objects appear newer than source
and corrupted one fuzzer elapsed-time line. That run is excluded above. Final
GCC 13 release and Clang 18 sanitizer builds were therefore forced with -B;
all use -Wall -Wextra -Werror -Wshadow -Wconversion. Final forced make all
exited 0 despite the expected clock-skew warning. Final forced make check
also exited 0; the final verification is pasted below.

The initial sandbox make check stopped at CLI X connection checks. Retrying
outside socket isolation on the authorized Xvfb :99 passed. Only DISPLAY=:99 /
EDIT_DISPLAY=:99 were used. Attempts to start an additional Xvfb failed and
left no new server. LeakSanitizer was disabled because it cannot run in this
sandbox; the coordinator must rerun with leaks enabled.

## Decisions and remaining acceptance

Design choices are in docs/decisions/edit-yqu.md. The coordinator still needs
completed scroll/save benchmark rows and full make bench acceptance, the
optional findui count sample increase, the remaining §35/§36/§37 schedules, and a
leaks-enabled suite. The pre-existing scroll bench sliced-index failure belongs
to §5 and was reported, not repaired. HANDOFF.md and docs/worklog are untouched.

## Final verification update

Final commands completed successfully:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make -B -j4 all
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -B -j4 check
```

Final forced sanitizer result, exit 0 (M)[AC]:

```text
check: 59 test binaries passed
test_replay_cli: all passed
make: warning:  Clock skew detected.  Your build may be incomplete.
```

The warning reflects the system clock change; -B rebuilt the objects. Final
release and sanitizer savectl binaries also independently exited 0 and printed:

```text
savectl normal-build I/O and held-publication guard: ok
savectl creation permissions/default/umask: ok
savectl_test: ok
```

The final findui stabilization preserves selection when an option/window update
is a no-op. It was rebuilt with the normal warning/sanitizer/fuzzer flags and
its final run exited 0: (M)[AC] 1958 runs in 61 seconds, power Charging, load1
(M)[AC] 7.79. The final-code savectl fuzzer exited 0: (M)[AC] 365 runs in 61
seconds, power Charging, load1 (M)[AC] 27.51. Scroll's unchanged final fuzzer
exited 0: (M)[AC] 780 runs in 62 seconds, power Charging, load1 (M)[AC] 5.81.
No measurements are offered as performance gate acceptance. Leak checking
remains for the coordinator. git diff --check exited 0. Features stopped before
the scope cutoff; subsequent work stabilized the harness and finished validation.

