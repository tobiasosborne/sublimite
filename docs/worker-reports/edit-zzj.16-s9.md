# edit-zzj.16 session 9 worker report

Scope: P1-1 section 1 (capacity split) and section 6 (post-edit exact index
completion). Git was read-only; no bd, HANDOFF.md or docs/worklog edits.
Display-backed commands use DISPLAY=:99 EDIT_DISPLAY=:99 exclusively.
Power readback was Not charging, on AC. All measured results below are (M)[AC].
The box remains shared; benchmark samples are diagnostics, never gate verdicts.

## Section 1 — implemented

The editor checks complete ordinary replacements and undo/redo groups before
mutating, and checks each actual insertion/deletion before its undo call.
lineidx_edit_check and lineidx_edit share their capacity planner. Adjacent short
chunks are repacked, preventing one reserved entry per ordinary typed character.
Refusal returns EDITOR_ERR_CAPACITY with bytes, index length, revision and history
unchanged in the replacement/undo regressions. Existing indentation prefix
semantics are preserved; the separate Enter/brace worker owns that algorithm.

The tests were written and executed against the original implementation before
production changes. Actual file-backed typing reproduced the reported split:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_test --index-capacity
capacity typing: key=65 rc=-104 tree=65601 index=65600
editor_test:1254: FAIL lineidx_len(e->buffer->index) == editor_length(e)
exit 1

$ DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_test --index-refusal
editor_test:1270: FAIL rc == EDITOR_ERR_CAPACITY
exit 1
```

Green (M)[AC]:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_test --index-capacity
P1-1 section 1: file-backed successive typing keeps bounded index geometry passed
exit 0

$ DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_test --index-refusal
P1-1 section 1: replacement and undo capacity refuse before buffer/history mutation passed
exit 0
```

The typing regression performs 256 consecutive inserts (M)[AC] and requires at
most three chunks (G). The refusal regression uses an explicitly exhausted
reservation for replacement and restored undo content.

## Section 6 — implemented

The inherited tree already refreshed edited chunks on the active buffer, but
its fallback stopped at the estimated total and ignored inactive dirty buffers.
Maintenance now services eligible buffers in round-robin order. It retains dirty
state across cancellation/physical retirement/admission retry and eventually
publishes exact totals. A completed index again supports editor_jump_line.
Initial staged results also continue to drain. Active sliced replay excludes
index maintenance until its checkpoint commits.

A prepaid lineidx restart API was needed: existing build_start allocated and
copied whole geometry immediately. The editor reserves reusable job/scratch at
open; restart takes a snapshot from its existing piece allocator and prepares
geometry in bounded poll steps. Existing lineidx_test now covers preparation
bounds, cancellation during preparation, active-source retirement/refusal,
repeated slot reuse, exact counts, constant retained storage and zero allocations.
These API tests supplement the editor red test written before the implementation.

Original red (M)[AC], before the repair implementation:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_test --index-repair
editor_test:1309: FAIL lineidx_complete(edited->index)
exit 1
```

Final focused green (M)[AC]:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_test --index-repair
P1-1 section 6: inactive exact counts/jump, bulk cancellation/retry, mallocs=0 guard=active passed (M)[AC]
exit 0

$ build/tests/lineidx_test --prepaid
prepaid restart: bounded preparation, cancellation/reuse, mallocs=0 guard=active passed (M)[AC]
lineidx_test: ok
exit 0
```

The file-backed fixture has a low-density prefix and a dense suffix. It requires
exact completion while inactive, exact jump after activation, and another edit
while a real bulk job holds the service and a restart snapshot is queued.
A second edit cancels that snapshot; releasing the unrelated job must permit
retirement and exact rebuilding. Its final total is 24,577 lines (M)[AC].

## Decisions and limitations

Design choices are recorded in [edit-zzj.16.md](../decisions/edit-zzj.16.md).
No new globals. No production changes to Enter/brace logic, IPC draining, large.c,
work scheduling, file invalidation or other named P1-1 findings. Capacity refusal
retains the existing stopped-loop policy. Large replacements beyond the bounded
index edit limit still refuse before replay mutation; allocating index recreation
is not added. Large-file estimated view numbering retains large.c's policy.

During verification, the first sandbox make check stopped at XCB connection
creation because the sandbox hides the Xvfb socket. The authorized elevated
:99 run reached the actual suites. Two intermediate failures exposed maintenance
regressions: initial staged-result adoption timed out in editor_large_test, and
an intentionally unreserved replacement index stopped recovering in editor_p4_test.
Both were corrected locally: drain initial results, and retain bounded full-target
seek fallback for unreserved indexes. The supplemental lifecycle test also exposed
its own missing retirement poll; the test now observes the documented ownership
boundary and uses independent accepted-source storage. No dependency fixture was
weakened or out-of-scope production issue fixed.

## Acceptance verification

Final release build exits zero using gcc 13 with C11, -Wall -Wextra -Werror
-Wshadow -Wconversion. Final release editor_test exits zero. Focused ASan/UBSan
repair also exits zero using clang 18. Final-source editor and lineidx fuzz each
run for at least 60 seconds (G), clean; actual duration is 61 seconds (M)[AC].

```text
$ make -j4 all
exit 0

$ DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
editor_test: raster 10000 keys mallocs=0 guard=active
editor_test: all passed
exit 0

$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --index-repair
P1-1 section 6: inactive exact counts/jump, bulk cancellation/retry, mallocs=0 guard=ASan-inert passed (M)[AC]
exit 0

$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/editor_fuzz -max_total_time=60 -max_len=1024 -artifact_prefix=build/
Done 10134 runs in 61 second(s)
exit 0

$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/lineidx_fuzz -max_total_time=60 -max_len=1024 -artifact_prefix=build/
Done 8699 runs in 61 second(s)
exit 0
```

Final full ASan/UBSan make check exits zero (M)[AC]:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check
check: 60 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
exit 0
```

The raster native conformance test serializes 10,000 frames (G), so its silent
wait takes minutes on Xvfb. I initially mistook that wait for a hang, terminated
an earlier check, and tried two insufficient 45-second timeouts (M)[AC]. Those
interrupted runs do not establish a raster defect. The final unmodified check
was allowed to finish normally and passed, including raster. All 27 sanitizer
binaries after raster (M)[AC] plus replay CLI also passed in the independent
verification performed while the final full check was running.

LeakSanitizer is disabled with
ASAN_OPTIONS=detect_leaks=0 because it cannot run in the sandbox; the coordinator
must rerun with leaks enabled. No native EGL or real-display acceptance is claimed.


## Paired hot-path diagnostics and remaining verification

The existing editor benchmark was run baseline then current back to back on the
shared AC box, using the same corpus, flags and sample count. Baseline objects
were compiled from read-only git show HEAD sources into build/s9-baseline,
leaving production and Git unchanged. Both runs use the existing
/tmp/edit-corpus/log_1g.txt and:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_BACKEND=raster <baseline-or-current-editor_bench> --track --no-idle --no-p4 --keys=256 --serial-only
```

All figures in this table are (M)[AC]; timings are ingress to T4 in milliseconds.
The sample count is 256 per row (M)[AC], below the 10,000 gate sample requirement
(G). Baseline load1 was 4.13 and current load1 was 6.08 (M)[AC].

| Backend | Baseline p50 / p99 | Current p50 / p99 | Baseline / current allocations |
|---|---:|---:|---:|
| null | 0.409777 / 0.464861 | 0.218346 / 0.360062 | 0 / 0 |
| raster | 32.346489 / 51.066319 | 31.608543 / 59.857760 | 82 / 21 |

Both benchmark commands exit 1 (M)[AC] because their native raster allocation
structural row fails in baseline and current code. This inherited benchmark
attribution/native allocation issue is outside scope. The release editor suite
and the whole focused null repair interval independently record zero allocations
with the real guard active (M)[AC]. Shared-box timing variation does not prove a
performance improvement or regression, and no G1 certification is claimed.

No scoped implementation or required in-sandbox verification is missing.
The coordinator must rerun LeakSanitizer enabled. Native EGL/real-display
performance acceptance and the inherited raster benchmark allocation result
remain outside this bead; no claim is made that unrelated findings are fixed.
The report and decision record were finalized within the worker time budget.
