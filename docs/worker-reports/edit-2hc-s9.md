# edit-2hc (P2) — scan throughput / load-aware verdicts

Scope: `bench/scan_bench.c`, additive support in `bench/harness.h`, and
`tests/harness_test.c`. Read CLAUDE.md, the supplied bead, scan review findings
in `docs/reviews/work-scan-1.md`, `src/scan/STATUS.md`, the scan decision records,
the hardware profile, and the M0 scan rows. No src/scan regression was confirmed,
so its implementation is unchanged. No new global storage. Git was read-only;
no bd, HANDOFF, worklog, or Makefile edits.

## Finding: loaded scans record misleading gate outcomes — fixed

Write/read red first: the harness test includes the actual scan reporter and
requires start/end load fields and a named verdict. The original implementation
ran and failed these checks before implementation changes. Additional load-policy
tests were written before adding the new enum, qualifier, or reporter API; that
red build failed because those interfaces were absent.

Fix: sample load1 around each kernel's sample loop, print `load1_start` and
`load1_end` on each row, and mark valid timing rows LOADED when either endpoint
is >4 (G). LOADED returns exit 3, never pass or miss. The G6 basis row shares
count's measured interval. Unknown load evidence refuses a verdict. Correctness
and nonfinite/nonpositive throughput remain fatal. Existing throughput gates
and legacy harness callers' policy are unchanged. The design choice is recorded
in `docs/decisions/edit-2hc.md`.

Tests cover both loaded endpoints, suppression of otherwise passing and missing
timings, the exact cutoff, exit merging, missing load evidence, and invalid
samples under load. The existing tail and independent correctness self-checks
remain green.

### Pasted RED

Test-run counts and exit statuses are (M)[AC]; supplied synthetic throughput and
load values are contract inputs, not measured box performance.

```text
COMMAND make build/tests/harness_test
exit=0
COMMAND build/tests/harness_test
FAIL tests/harness_test.c:310: strstr(line, "load1_start=0.00 load1_end=0.00") != NULL
FAIL tests/harness_test.c:311: strstr(line, "verdict=PASS") != NULL
harness_test: 20221 checks, 2 failures
exit=1
```

Load-verdict tests added before implementation, second RED:

```text
COMMAND make build/tests/harness_test
tests/harness_test.c:317:11: error: implicit declaration of function ‘bench_qualify_load’ [-Werror=implicit-function-declaration]
tests/harness_test.c:319:56: error: ‘BENCH_LOADED’ undeclared (first use in this function)
tests/harness_test.c:334:11: error: implicit declaration of function ‘report_gate_fp’; did you mean ‘report_gate’? [-Werror=implicit-function-declaration]
cc1: all warnings being treated as errors
make: *** [Makefile:55: build/rel/tests/harness_test.o] Error 1
exit=2
```

### Pasted GREEN

```text
COMMAND make build/tests/harness_test build/bench/scan_bench
exit=0
COMMAND build/tests/harness_test
PROBE scan load: both endpoints, >4 boundary, pass/miss suppression, exit=3, invalid samples fatal
harness_test: 20247 checks, 0 failures
exit=0
COMMAND build/bench/scan_bench --self-check-tail
scan_bench self-check §12: PASS slow p99 rejected; G6 basis/invalid/boundary checked
exit=0
COMMAND build/bench/scan_bench --self-check-oracle
scan_bench self-check §13: PASS prefix result rejected=1 wrong pointer=1 wrong count=1 correct accepted=1
exit=0
```

## Three consecutive baseline runs

`bench/scan_bench.args` does not exist in this checkout. Used default arguments,
matching Makefile's optional-args behavior. Each invocation completed before the
next started; no builds or test suites from this worker overlapped these runs.
No quiet-box wait. Power was BAT0 `Not charging` [AC] at every start/end.
The fixture is generated anonymous memory, warmed and validated, not a corpus
file mapping. Governor observation: `cpu0 scaling_governor: powersave`.

Every throughput and load entry below is (M)[AC]. Printed baseline verdicts are
retained as historical output, not a regression conclusion from one timing.

| Run | Load1 process start/end | Load1 old row stamp | Count p50/p99 GB/s | Nth p50/p99 GB/s | Count G6 basis p50/p99 GB/s | Old verdicts count/nth/basis | Exit |
|---|---|---|---|---|---|---|---|
| 1 | 2.68 / 3.50 | 3.02 | 6.78 / 6.48 | 8.10 / 7.07 | 6.78 / 6.48 | MISS / MISS / MISS | 1 |
| 2 | 3.50 / 4.20 | 3.78 | 6.98 / 6.63 | 7.99 / 7.76 | 6.98 / 6.63 | MISS / MISS / MISS | 1 |
| 3 | 4.20 / 4.11 | 4.20 | 15.52 / 14.73 | 19.68 / 19.55 | 15.52 / 14.73 | PASS / PASS / MISS | 1 |

Gates: count/nth >=12/12 GB/s p50/p99 (G), G6 basis >=16/12 GB/s (G).
The rebound between consecutive runs supports caution about attributing the
earlier low rates to scan code. Load1 is a coarse, lagging indicator; the cutoff
is the requested qualification policy, not proof of uncontended memory bandwidth.

Pasted original rows, including external load/power stamps:

```text
RUN 1 start load1=2.68 power=Not charging (M)[AC]
TRACK primitive cached anonymous buffer; (M)[AC] load1=3.02 power=Not charging; G6/G7 end-to-end unmeasured
TRACK (M)[AC] load1=3.02 scan_count               p50 6.78 GB/s  p99 6.48 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G)  MISS
TRACK (M)[AC] load1=3.02 scan_find_nth_newline    p50 8.10 GB/s  p99 7.07 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G)  MISS
TRACK (M)[AC] load1=3.02 scan_count_G6_basis      p50 6.78 GB/s  p99 6.48 GB/s  gates p50 >= 16.0 / p99 >= 12.0 (G)  MISS
RUN 1 end load1=3.50 power=Not charging exit=1 (M)[AC]
RUN 2 start load1=3.50 power=Not charging (M)[AC]
TRACK primitive cached anonymous buffer; (M)[AC] load1=3.78 power=Not charging; G6/G7 end-to-end unmeasured
TRACK (M)[AC] load1=3.78 scan_count               p50 6.98 GB/s  p99 6.63 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G)  MISS
TRACK (M)[AC] load1=3.78 scan_find_nth_newline    p50 7.99 GB/s  p99 7.76 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G)  MISS
TRACK (M)[AC] load1=3.78 scan_count_G6_basis      p50 6.98 GB/s  p99 6.63 GB/s  gates p50 >= 16.0 / p99 >= 12.0 (G)  MISS
RUN 2 end load1=4.20 power=Not charging exit=1 (M)[AC]
RUN 3 start load1=4.20 power=Not charging (M)[AC]
TRACK primitive cached anonymous buffer; (M)[AC] load1=4.20 power=Not charging; G6/G7 end-to-end unmeasured
TRACK (M)[AC] load1=4.20 scan_count               p50 15.52 GB/s  p99 14.73 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G)  PASS
TRACK (M)[AC] load1=4.20 scan_find_nth_newline    p50 19.68 GB/s  p99 19.55 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G)  PASS
TRACK (M)[AC] load1=4.20 scan_count_G6_basis      p50 15.52 GB/s  p99 14.73 GB/s  gates p50 >= 16.0 / p99 >= 12.0 (G)  MISS
RUN 3 end load1=4.11 power=Not charging exit=1 (M)[AC]
```

## Gate basis and conditional regression investigation

`perf/00-hardware-profile.md`'s SIMD addendum measured a warmed single-thread
SSE2 scan at 16.7–17.3 GB/s (M)[AC], with powersave governor, and adopts 16 GB/s
as the binding AC basis. It does not specify a load/core-count fraction, so the
bead's explicit >4 (G) alternative was chosen. `docs/decisions/P1.2.md` chose
SSE2. The AC rerun at the end of `P1.2b.md` had no agent workers, load1=1.06
(M)[AC], and count 16.06/15.87 GB/s (M)[AC]. This is the near-idle AC evidence;
it used powersave, not performance governor. `P1.2c.md` supplies the explicit
tail and G6 basis gates; they were not relaxed.

No baseline or post-fix measured interval was under load1=2. Therefore the bead's
conditional low-load regression bisect was not triggered. Read-only source
inspection confirms the selected fixed SSE2 kernel, with no SIMD dispatch in
src/scan. The benchmark's own anonymous buffer generation and whole-range
warm-up exclude corpus page-cache state as a variable for these rows. No scan
code, governor setting, fixture state, or machine workload was changed.

One post-fix invocation verified the real row formatting and unchanged gates:

```text
postfix start load1=2.19 power=Not charging (M)[AC]
TRACK (M)[AC] load1_start=2.19 load1_end=2.26 scan_count               p50 15.89 GB/s  p99 13.91 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G) load1_limit=4.0 (G) verdict=PASS
TRACK (M)[AC] load1_start=2.26 load1_end=2.26 scan_find_nth_newline    p50 20.53 GB/s  p99 17.35 GB/s  gates p50 >= 12.0 / p99 >= 12.0 (G) load1_limit=4.0 (G) verdict=PASS
TRACK (M)[AC] load1_start=2.19 load1_end=2.26 scan_count_G6_basis      p50 15.89 GB/s  p99 13.91 GB/s  gates p50 >= 16.0 / p99 >= 12.0 (G) load1_limit=4.0 (G) verdict=MISS
postfix end load1=2.26 power=Not charging (M)[AC]
exit=1
```

The loaded branch was verified with deterministic synthetic rows in the tests;
the post-fix real measurement was below the refusal cutoff. No claim of a
throughput improvement or complete G6/G7 acceptance is made.

## Acceptance verification

GCC 13.3 release and clang 18.1 sanitizers use C11 with
`-Wall -Wextra -Werror -Wshadow -Wconversion` through the unchanged Makefile.
All display work uses `DISPLAY=:99 EDIT_DISPLAY=:99` only.

```text
COMMAND make -j2 all
make all exit=0 elapsed_s=36.22 (M)[AC]
```

Scan differential fuzz campaign completed cleanly, with these pasted results:

```text
COMMAND make -j2 build/fuzz/scan_fuzz
fuzz build exit=0
COMMAND build/fuzz/scan_fuzz build/edit-2hc-evidence/scan-corpus -max_total_time=60 -max_len=131089 -timeout=10 -artifact_prefix=build/edit-2hc-evidence/
START power=Not charging load1=4.31 (M)[AC]
Done 109478 runs in 61 second(s)
scan_fuzz exit=0 elapsed_s=61.03 (M)[AC]
```

Requested fuzz duration: 60 s (G); cases and actual duration above are (M)[AC].
Seeds cover empty input, SIMD/chunk boundaries, all-byte cycles, extreme ranks,
and guarded left/right ranges. No findings. The first full sanitizer invocation
built successfully but was interrupted
while waiting for the shared display lock after a read-only probe showed that
the sandbox could not connect to Xvfb :99. The same probe succeeded outside the
socket-restricted sandbox. The authorized full suite then passed with that local
display access, keeping both display variables at :99:

```text
COMMAND ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j2 check (outside socket-restricted sandbox)
START power=Not charging load1=0.60 (M)[AC]
harness_test: 20247 checks, 0 failures
check: 61 test binaries passed
== tools/test_replay_cli.sh
test_replay_cli: all passed
== tools/test_display_lock.sh contract
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
make check exit=0 elapsed_s=705.51 (M)[AC]
```

Counts and elapsed durations are (M)[AC]. The display-lock contract deliberately
tests timeouts; its timeout messages are expected and its final verdict is PASS.
`ASAN_OPTIONS=detect_leaks=0` was retained for every sanitizer/fuzz invocation as
requested: LeakSanitizer cannot run inside this sandbox, and the coordinator
must rerun with leaks on. `git diff --check` passed. No source changes followed
these successful validations.

## Missing / outside scope

Implementation and requested build/check/fuzz verification are complete. No
quiet-box run was requested or sought. Low-load regression bisect remains conditional on observing
a miss below load1=2; this worker observed none. End-to-end cached-file/new-mapping
G6/G7 and leak-enabled verification belong to the coordinator/module owners.
No unrelated review findings were fixed.
