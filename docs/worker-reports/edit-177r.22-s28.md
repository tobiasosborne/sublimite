# edit-177r.22, session 28 worker report

Done. All requested build/test commands exit successfully, with Linux flakes
recorded below for coordinator triage. Named
NTFS streams now survive atomic save, including `Zone.Identifier`. EFS refusal
remains explicit and covered. Decision: [edit-177r.22](../decisions/edit-177r.22.md).

## Changes

- `src/file/file_win.c`: enumerate the retained destination by native handle;
  bound counted UTF-16 names by returned bytes and the native open's length
  type; copy/flush all named data streams on the temporary, polling cancellation;
  propagate native errors and changed-target results. Defer read-only state until
  after stream creation, then restore it through final metadata application.
- `src/file/file.c`, `file_internal.h`: Windows-only save-worker hook before
  metadata finalization and the existing flush/rename sequence.
- `tests/win/file_win_test.c`: replace stream refusal with preservation;
  allocation/enumeration/open/read/write/stat/flush failures, cancellation,
  short/interrupted/zero transfers, malformed returned records, changed targets,
  process death during a large stream, and enumeration beyond the former cap.
- `tests/file_win_cases.inc`: shared byte-exact stream fixtures, Save As from
  another file onto existing/missing targets, and stream oracles at every
  existing crash boundary in the production `file_test` suite.
- `tests/win/savectl_test.c`: real external reload and named-stream copy failure
  keeps the buffer dirty, target and streams intact, and no temporary; retry
  preserves MOTW and publishes a clean saved state.

No edits to sibling-owned sources, Linux implementation, or `mk/win.mk`.
The concurrent `Makefile` tools-glob fix in this worktree came from outside this
worker and is preserved; it is not part of the stream implementation.

## Red first

Commands (M: captured process statuses):

```sh
taskset -c 48-63 nice -n 10 make TARGET=win -j16 build/win/rel/tests/file_win_test.exe build/win/tools/win_quiet_run.exe
# exit 0
taskset -c 48-63 nice -n 10 tools/winrun.sh build/win/rel/tests/file_win_test.exe streams
# exit 1
tests/win/file_win_test.c:271: FAIL r2_save(path, NULL) == FILE_OK (native 50)
tests/win/../file_win_cases.inc:39: FAIL os_file_stat(&source, &st) == OS_OK && st.size == n (native 50)
file_win_test: streams: FAILED
```

This ran after replacing the refusal test and adding the MOTW, large, Unicode,
multiple, and empty-stream fixtures, before modifying the implementation. The
old default stream remained unchanged. Native `50` is `ERROR_NOT_SUPPORTED`
(P: Windows error definition). Full captured output: `/tmp/edit-177r.22-red.log`.
The sandbox initially blocked WSL interop; all actual Windows test executions
used the required runner with sandbox escalation and private station isolation.

The added read-only fixture also exposed a genuine intermediate failure before
the read-only deferral fix (M, `/tmp/edit-177r.22-red-more.log`):

```text
tests/win/file_win_test.c:354: FAIL r2_save(path, NULL) == FILE_OK (native 5)
file_win_test: streams: FAILED
```

Native `5` is `ERROR_ACCESS_DENIED` (P: Windows error definition). The initial
failure-loop fixture also needed correction: Windows overwrite/truncate removes
its ADS, so each independently reset target now recreates its streams before
the injected save. This was a test-fixture repair, not an implementation result.

## Green Windows verification

Final build and runs (M: captured exits), all with the assigned CPU affinity and
priority. The parallelism is the supplied range size (G: `63 - 48 + 1 = 16`).

```sh
taskset -c 48-63 nice -n 10 make TARGET=win -j16 \
  build/win/rel/tests/file_test.exe build/win/rel/tests/file_win_test.exe build/win/rel/tests/savectl_test.exe \
  build/win/san/tests/file_test.exe build/win/san/tests/file_win_test.exe build/win/san/tests/savectl_test.exe
# exit 0; final file_win_test rebuild after adding changed-target/read-error coverage: exit 0
for config in rel san; do
  for suite in file_test file_win_test savectl_test; do
    taskset -c 48-63 nice -n 10 tools/winrun.sh "build/win/$config/tests/$suite.exe"
  done
done
# each invocation exit 0
```

Relevant green output, present in both configurations:

```text
file_test: Save As preserves destination streams, source streams stay with source: ok
file_test: self-reexec multichunk crash at every atomic-save step, restrictive artifacts: ok
file_test: Windows ok
file_win_test: efs: ok
file_win_test: streams: ok
file_win_test: stream copy failure and cancellation at all 41 observed operations: ok
file_win_test: stream-failures: ok
file_win_test: stream-crash: ok
file_win_test: stream-many: ok
file_win_test: 16 cases, ok
savectl production Windows external reload, ADS failure keeps dirty buffer/target/streams, retry preserves MOTW: ok
savectl_test: ok
```

The operation count is measured from a successful copy, then every observed
allocation/API/transfer step is individually failed and cancelled (M: test
counter, not a fixed coverage budget). Case count is the test's case-array length
(M). The large fixture is `FILE_PREFIX_MAX + 1` bytes (G: exceed one prefix and
leave a non-chunk-aligned tail); the transfer buffer is the existing save chunk
(P: `FILE_PREFIX_MAX / 4`). Stream-count stress derives its count by dividing
the old enumeration capacity by a near-component-limit record size and adding
one, so the complete returned list must exceed that old capacity. Binary fixture
content includes NUL and non-ASCII bytes, and the Unicode name includes a
surrogate pair. Every failure/cancellation checks original default bytes, every
stream, closed handles, and absence of temporaries. The process-death test exits
from a read after part of a large ADS was copied, verifies the untouched target,
and removes the crash artifact like the existing boundary harness.

Logs: `/tmp/edit-177r.22-{rel,san}-{file_test,file_win_test,savectl_test}.log`.
No full Windows `check` was run, per the brief.

## Linux and performance verification

`taskset -c 48-63 nice -n 10 make -j16 all`: **exit 0** (M), log
`/tmp/edit-177r.22-linux-all-final.log`. The first attempt failed at the pre-existing
unguarded Windows runner in the Linux tools glob; the external Makefile fix
arrived before the successful rerun.

`taskset -c 48-63 nice -n 10 make -j16 check`: **exit 0** (M), run outside the
socket-restricted sandbox so its private Xvfbs can start. The harness reported
the following and completed its shell contracts successfully:

```text
check: FLAKY editor_large_test (failed in parallel, passed alone; see build/check-logs/editor_large_test.log and build/check-flaky.log)
check: FLAKY dataloss_test (failed in parallel, passed alone; see build/check-logs/dataloss_test.log and build/check-flaky.log)
check: 117 test binaries passed in 196.7 s (parallel, 16 jobs, 6 displays)
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
```

Counts and runtime are emitted by the existing harness (M), not new budgets.
Full output: `/tmp/edit-177r.22-linux-check-final.log`; individual initial and
serial retry logs: `build/check-logs/`; flakes: `build/check-flaky.log`. The first
sandboxed attempt failed to bind the private X server's socket before suites
could execute. No extra retries were requested after the harness's own retries.

Linux-preprocessed `file.c` is byte-identical to HEAD (M: run GCC preprocessing
on each source through the same stdin invocation, compare bytes and SHA-256):
`4356efd42a8c93f0efbb667952087f59db957205e8a398f97f1c837214f4c137`.
The Windows-only implementation preprocesses to an empty translation unit on
Linux; `file_posix.c` is unchanged. No new allocation is reachable from typing
or save acknowledgement; all stream allocations are in the save worker. Existing
release file tests retain their no-allocation status/dispatch checks.

The existing file benchmark ran with the shared display lock on private
Xvfb, without changing the corpus or benchmark:

```sh
taskset -c 48-63 nice -n 10 env DISPLAY=:99 EDIT_REFUSE_REAL_DISPLAY=1 \
  sh tools/with_display_lock.sh build/bench/file_bench --track
```

Measurements are [AC, desktop Spiral], Tobias using the box. There is no battery
entry under `/sys/class/power_supply`; the benchmark's generic battery helper
prints `[unknown]`. Load immediately before the benchmark run was
`9.45, 7.05, 6.30` (M: `uptime`; moving load averages). Benchmark **exit 0** (M),
full output `/tmp/edit-177r.22-file-bench.log`. Representative existing save rows:

| Existing benchmark row | Samples | p50 / p99 (ms) | load1 |
| --- | ---: | ---: | ---: |
| Fresh fragmented status acknowledgement, 1 MiB, idle bulk lane | 100 | 0.500 / 0.776 | 8.61 |
| Warm-source complete save, 1 MiB | 100 | 47.052 / 60.517 | 8.61 |
| Fresh fragmented status acknowledgement, 1 GiB | 10 | 0.507 / 0.544 | 5.27 |
| Warm-source complete save, 1 GiB | 10 | 1841.956 / 1982.995 | 5.27 |

All figures are (M) [AC, desktop Spiral], directly emitted by the existing
benchmark's sorted-sample percentiles and load reader. Sample counts derive from
the existing `SAMPLES` and TRACK large-save settings. The complete-save rows miss
the existing tracked goals in this shared-box run; no behavior or budget was
changed, and no baseline comparison or Windows ADS throughput claim is made.
Manual cold rows remain unmeasured as the benchmark states.

## Found, not fixed / coordinator decisions

- `docs/reviews/win-parity-2026-11-01.md` is absent in this worktree, so the named
  review row could not be read. The defect and tests in the brief were verified.
- The full Linux check recorded parallel-only failures in `editor_large_test`
  (`cold_bracket_paint`, `file_snapshot_bytes`) and `dataloss_test`
  (`detached_rewrite(false)`, recovered original lost). Both passed the harness's
  serial retry, so overall check exited successfully. Per the worker rules,
  the coordinator must triage these flakes; they concern unchanged Linux/editor
  paths and remain outside this worker's source ownership.
- Save As policy is settled by matching Linux destination xattrs; no additional
  product decision is needed for streams. EFS and previously documented Windows
  rename power-loss/audit-policy limitations remain outside scope.
