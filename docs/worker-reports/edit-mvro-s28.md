# edit-mvro / edit-5mmh — Linux parallel-load investigation

Status: **done**. Test fixes and all required Linux verification are complete.

No newly demonstrated product bug affecting Tobias's installed Linux build.
The detached recovery and cold-paint failures are **TEST BUGS**. The older Save
As deadline is also a confirmed test polling race. The historical OS suite
failure and ThinkPad two-process open error remain **NOT REPRODUCED**.
Production code, journal format and recovery order are unchanged.

## Verdicts and demonstrated mechanisms

### Detached length assertion — TEST BUG

The child edits a sparse original, flushes the edit, replaces its pathname,
and waits for a self-contained checkpoint. Original size (P) is
`JOURNAL_STARTUP_SCAN_BYTES + 1`, the smallest generation excluded from
proactive retention. The expected edited length adds `strlen("XY")`.

The old loop stopped on checkpoint completion **or** an elapsed idle watchdog,
then unconditionally sent its ready byte. Under contention it reported
`checkpoint=missing`; the parent nevertheless killed it. The previous journal
still depended on the replaced BASE. Recovery displayed the changed disk text
and set the edits aside: the test had killed the checkpoint before completion.

Measured red excerpt, `diagnostic-red/dataloss-3-1.log` (suite exit **1**):

```text
dataloss_test: edit-7cj8 detached checkpoint=missing
dataloss_test: edit-7cj8 detached recovered original=lost modified=0
dataloss_test: recovery len=9 expected=105000003 restore=0 placeholder=0 open=0 deferred=1 journal_error=0 notice=[file changed on disk since this session; its unsaved edits were set aside]
dataloss_test:951: FAIL editor_length(e) == bytes + (sizeof "XY" - 1u)
```

The measured lengths derive from `strlen("external\n")` and
`105000000 + 1 + strlen("XY")` (P). Diagnostic additions shifted the original
assertion's line number; the assertion is identical.

Production path: `editor_retain_tick` → `editor_publish_start` →
`publish_worker` → validate/encode/flush/rotate → `editor_publish_poll`.
The UI clears `base.path` only after successful durable rotation. The test now
requires that condition before sending ready and authorizing the kill. A
watchdog failure fails the child instead. Exact length, prefix, every sparse
hole byte, tail, modified state, aside state and external-file assertions stay.

**Existing product limit:** [edit-7cj8](../decisions/edit-7cj8.md) explicitly
states that a crash before change detection or durable publication still
depends on the original BASE. An original above the retention bound, replaced
externally and followed by a crash during this window, can leave its original
text unavailable; recovery shows the changed file and sets edits aside. This
campaign exercises that documented window by killing a pending checkpoint.
It does not demonstrate dropped edits or failed recovery after a completed
checkpoint. Eliminating this window would require a separate recovery-design
change, not a test-wait fix.

### Detached lazy-replay deadline — TEST BUG

A whole-suite red run reproduced the exact original pair (exit **1**):

```text
dataloss_test:940: FAIL trace_now_ns() < limit
dataloss_test:2071: FAIL detached_rewrite(false) == 0
```

At this assertion the test is pumping `restore_active`, a placeholder, or a
pending open. A journal above the startup scan bound is intentionally deferred
to a closed tab; reopening scans and materializes its text asynchronously in
`restore.c`. The editor's ten-second idle policy was incorrectly used as an
I/O/replay deadline. The stronger campaign produced seven such deadline
failures (M); the unchanged byte/state assertions all pass after waiting for
the restore condition with a suitable hang guard.

Both checkpoint and replay now reuse the OS conformance suite's existing
120-second whole-case guard (E). Derivation: the complete detached scenario
measured up to 18.156 seconds under contention (M); `120 / 18.156 > 6`. This
is a hang guard, not a latency requirement. The original idle policy had no
relationship to journal size or replay work. The condition remains mandatory.

### Cold mapped snapshot — TEST BUG

`editor_open` may return a private prefix preview while a worker acquires the
full mapping. `cold_bracket_paint` cancelled workers and took a snapshot before
adoption. Direct diagnostic red excerpt (exit **1**):

```text
editor_large_test: cold paint pending_open=1 tree=1048576 expected=33554432
editor_large_test:814: FAIL file_snapshot_bytes(snap, NULL, &mapped, &n) == 1 && n == length
```

Those lengths are `FILE_PREFIX_MAX` and `32 * 1024 * 1024` fixture bytes (P).
`file_snapshot_bytes` requires contiguous, fault-free file backing covering
the whole snapshot. The copied prefix has no such backing. No mapped file was
truncated and no edit was dropped.

The test now waits for `open_pending` to clear **before** cancelling workers or
inspecting the mapping. It still evicts the actual mapping with
`MADV_DONTNEED`, measures only the original operation, and asserts the same
fault count, bracket and cursor results.

Full-suite repeats exposed the same assumption deeper in the suite:
`wrapped_byte_jump`, `mapped_find_boundaries`, `index_completes_between_steps`
and the final mapped fixture. Their setup now follows adoption and explicitly
establishes the incomplete index if opening already finished its old index.
`index_arrives_during_frame` gets the same prerequisite. No find offsets,
navigation, frame/index publication, cancellation or allocation assertions
were removed. These are further instances of the same fixture defect.

### Older Save As / original `:1839` — TEST BUG

`UTIN_UNREADABLE` had already observed Save As, waited for its rename and
changed target permissions. It then reset a local `started` observation and
sampled `save_as` only after each following turn. If the first turn adopted
the completed save, the transient was gone forever. Red diagnostics at the
expired guard (exit **1**) showed:

```text
Save As wait started=0 active=0 command=0 moved=1 modified=0 notice=[]
FAIL trace_now_ns() < limit
```

The save succeeded. The fix waits for lasting path adoption or refusal and
drained command/save state. Exact disk bytes, owner/peer state, old peer text,
publication and eventual peer-refresh assertions remain. A longer timeout
would not fix the missed transient.

### OS SIGBUS — NOT REPRODUCED as a suite failure

Every valid pre-fix full OS-suite attempt passed despite the BUS diagnostic.
A separate `OS_TEST_CASE=fault` run also exited **0**:

```text
fault: a child now takes an unregistered fault on purpose (a crash report may follow)
ERROR: AddressSanitizer: BUS on unknown address (...)
fault: stray fault child -> exited 1
fault: ok
os_test: 1 cases, 0 failed
```

`--child=stray-fault` maps the first page of its own executable twice,
registers one view, and injects a native BUS in the other through
`os_fault_test_trigger`. The stray view is deliberately outside fault-service
coverage, so the service chains to ASan; its parent requires the child to die.
No fixture is truncated in this child. The quoted PC has the same libc syscall
offset as the injected reports here, supporting this diagnostic attribution.

The actual truncation `map` case maps its own `shrink` fixture, registers it
before shrinking through another handle, and checks zero-page recovery and
fault epochs. Concurrent service installation and registered-view recovery
also pass under contention. Neither is a shared-fixture mapping.

`check_parallel.first_failure` returns the first `ERROR` or `FAIL`, allowing
this expected ASan report to hide a later actual failure in the one-line
flaky summary. The brief's BUS line alone cannot identify why the historical
suite exited nonzero. No OS/runner fix is justified by the evidence. The
complete original suite log is needed; worker rule 5 prevented reading the
other worktree. No claim of a cause for that exit is made.

### ThinkPad two-process open — NOT REPRODUCED

The unchanged unordered `two_process_save(false)` passed sixteen targeted
attempts (M: four rounds × four concurrent copies, sixteen CPU hogs), plus
whole-suite attempts that reached it. Whole suites also exercise the ordered
case. The new `--two-process-save` selector only selects that existing case.

`file.c` validates the acquired generation, and buffer preparation checks it
again. Replacement during acquisition can refuse a changed source. The
unordered test does not handshake that B finished opening before A saves;
forked tests also inherit the process-wide inotify descriptor, unlike separate
editor launches. These remain possible contributors, not demonstrated causes.
No speculative synchronization or production fix was applied to this case.

## Fixture and sharing audit

- Dataloss uses `mkdtemp("build/dataloss-XXXXXX")`; sources, journals, copies and
  leases are beneath it. Sessions are separately created. Sharing within a
  two-process scenario is intentional; creator-held leases protect its journals.
- Cold paint and other mutable editor fixtures use `mkstemp`/`mkdtemp`.
  `os_test` roots include PID and wall time, with supervised cases serial
  within a root. Mapping fixtures are private to a run.
- The editor suite reads the shared `log_1g.txt` and `sparse_10g.bin` corpus.
  The campaign never regenerated it. Cold paint maps its own generated file;
  save/truncate scenarios also use private generated files.
- Fixed `build/edit-2ty*.trace` and `build/edit-xzw-stale.trace` are diagnostic
  outputs, not mapped inputs. Concurrent runs can overwrite these artifacts;
  they cannot explain the demonstrated snapshot/recovery states.
- The three suites use null renderers; contention unsets display/session-bus
  variables. IPC sockets and explicit XDG fixtures are private. Preferences
  are not enabled for affected cases. Reload backing is anonymous or random
  exclusive-create scratch. Full `make check` uses private, locked Xvfbs.

## Measurements and red/green counts

All counts and times here are **(M) [AC, desktop Spiral]**, with Tobias using
the box. `BAT0/status` was absent; the brief identifies this as an AC desktop.
Initial `uptime` load was `0.25, 1.45, 3.89`; the stronger red campaign reached
`32.89, 18.31, 9.86`; after its green repeat it was `26.30, 20.27, 12.06`.
System load includes other lanes. Every child inherited CPUs 0–15 and nice 10.

Cells are **failures / attempts**, from suite exit codes in each label's
`build/edit-mvro/<label>/results.json`, with separate per-launch combined logs.
The collection driver exits zero after recording results; failed suites exit
one. There were no supervisor timeouts. No automatic retry is used.

| Label / dataloss case | Rounds × copies per suite; hogs | Dataloss | Editor large | OS |
|---|---|---|---|---|
| `red` / detached | 4 × 4; 16 | 0/16 | 14/16 | 0/16 |
| `full-red` / whole suite | 2 × 4; 16 | 3/8 | 7/8 | 0/8 |
| `diagnostic-red` / detached | 4 × 8; 32 | 15/32 | 21/32 | 0/32 |
| `utin-red` / Save As | 2 × 4; 16 | 2/8 | 5/8 | 0/8 |
| `window-red` / copy window, wrapped paint | 4 × 4; 16 | 0/16 | 7/16 | 0/16 |
| `diagnostic-green` / detached | 4 × 8; 32 | 0/32 | 0/32 | 0/32 |
| `utin-green` / Save As | 2 × 4; 16 | 0/8 | 0/8 | 0/8 |
| `window-green` / copy window, wrapped paint | 4 × 4; 16 | 0/16 | 0/16 | 0/16 |
| `two-process` / unchanged unordered save | 4 × 4; 16 | 0/16 | 0/16 | 0/16 |
| `full-green` / intermediate fix | 2 × 4; 16 | 0/8 | 8/8 | 0/8 |
| `full-green2` / final implementation | 2 × 4; 16 | 0/8 | 0/8 | 0/8 |

In `red`, five editor failures were the exact snapshot assertion; nine were
an earlier wrapped-open assertion. The direct diagnostic selector avoids
that masking. The stronger detached failures divide into eight missing
checkpoint/length failures and seven replay deadlines. `full-red` had two
Save As missed-transient failures and one exact detached deadline.
`full-green` revealed four preview/index-length failures and four pending-open
navigation failures deeper in the editor suite. Those are the red evidence
for the additional waits; the final whole-suite repeat passes every attempt.

Recipe sizes are (E) bounded stress choices, not statistical confidence:
the allowed CPU range defines sixteen or thirty-two hogs (one or two per
allowed CPU); concurrent copies per suite use a quarter or half of that range.
Attempts are bounded by the OS suite's existing 120-second hang guard and
fixed rounds; teardown kills/reaps only process groups the harness created.
The larger red run overlapped `utin-red` and the tail of `full-red`; its green
repeat overlapped `utin-green` and `two-process`. Whole-suite green checks also
overlapped the authorized release build, adding load within the same affinity.

Initial sandboxed `before` attempts are excluded: LeakSanitizer process
inspection was denied and altered child exits. Valid runs used an approved
unsandboxed shell with the same affinity/priority. ASan, UBSan and leak
detection stayed enabled. No packages, live desktop, Windows exe, `bd`, commit
or checkout was used.

## Reproduction commands

Build sanitizer prerequisites (exit **0**, clang 18.1.3):

```sh
taskset -c 0-15 nice -n 10 make -j16 build/san/tests/dataloss_test build/san/tests/editor_large_test build/san/tests/os_test
```

Save the appendix as `build/edit-mvro/contention.py`. Run from this worktree
with fresh labels. Red runs used original test semantics, optionally with
selectors/diagnostics; green runs used the fixes. Both used the same harness:

```sh
taskset -c 0-15 nice -n 10 env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS python3 build/edit-mvro/contention.py red --rounds 4 --copies 4 --hogs 16
taskset -c 0-15 nice -n 10 env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS python3 build/edit-mvro/contention.py full-red --full --rounds 2 --copies 4 --hogs 16
taskset -c 0-15 nice -n 10 env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS python3 build/edit-mvro/contention.py diagnostic-red --rounds 4 --copies 8 --hogs 32
taskset -c 0-15 nice -n 10 env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS python3 build/edit-mvro/contention.py utin-red --rounds 2 --copies 4 --hogs 16 --dataloss-selector=--edit-utin
taskset -c 0-15 nice -n 10 env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS python3 build/edit-mvro/contention.py window-red --rounds 4 --copies 4 --hogs 16 --dataloss-selector=--retain-window --cold-mode=P4_COLD_WRAP
```

Identical arguments with `diagnostic-green`, `utin-green`, `window-green`
and `full-green2` produced the final passing counts above. The historical-open
campaign substitutes `--dataloss-selector=--two-process-save`. Child commands
in the appendix inherit `taskset` and nice; e.g. the detached red child was
`build/san/tests/dataloss_test --detached-rewrite`, exit **1**, and the fixed
same child exits **0**. Excerpts above identify its exact failed assertions.

## Final verification and coordinator follow-up

Changed: `tests/dataloss_test.c`, `tests/editor_large_test.c`, this report and
[the decision record](../decisions/edit-mvro.md). No production file changed.

- `taskset -c 0-15 nice -n 10 make -j16 all`: **exit 0**, including after the
  last fixture fixes (`build/edit-mvro/all-final.log`), GCC 13.3.0.
- Sanitizer builds and all final contention recipes: **exit 0** for every
  tested suite, as counted above.
- `git diff --check`: **exit 0** at the latest implementation change.
- Full `taskset -c 0-15 nice -n 10 make -j16 check`: **exit 0**, all 117 binaries / 121 tasks passed at 16 jobs with six private displays (M: runner output). **No FLAKY or rerunning line anywhere in the final log**, and no rerun log for any of the three affected suites. Shell checks also passed. Log: `build/edit-mvro/check-final.log`; individual logs: `build/check-logs/`.

Found, not fixed / decisions: obtain the original complete OS failure log;
retain the ThinkPad unordered-open finding as not reproduced. Eliminating the
already documented external-BASE pre-publication crash window would require
separate product/recovery scope. No new product fix was attempted or obscured
by an isolated retry.

## Appendix: bounded contention harness

```python
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import signal
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('label')
p.add_argument('--rounds', type=int, default=4)
p.add_argument('--copies', type=int, default=4)
p.add_argument('--hogs', type=int, default=16)
p.add_argument('--full', action='store_true')
p.add_argument('--dataloss-selector', default='--detached-rewrite')
p.add_argument('--cold-mode', default='P4_COLD_PAINT')
a = p.parse_args()
root = Path('build/edit-mvro') / a.label
root.mkdir(parents=True, exist_ok=False)
assert set(os.sched_getaffinity(0)) <= set(range(16))
specs = [('dataloss', ['build/san/tests/dataloss_test', a.dataloss_selector], {}),
         ('cold', ['build/san/tests/editor_large_test'], {a.cold_mode: '1'}),
         ('os', ['build/san/tests/os_test'], {})]
if a.full:
    specs = [(name, cmd[:1], {}) for name, cmd, env in specs]
rows = []
def run(spec, round_no, copy):
    name, cmd, extra = spec
    path = root / f'{name}-{round_no}-{copy}.log'
    start = time.monotonic()
    with path.open('w') as out:
        proc = subprocess.Popen(cmd, env={**os.environ, **extra},
                                stdout=out, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            rc = proc.wait(timeout=120)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
            rc = 124
        finally:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    row = dict(suite=name, round=round_no, copy=copy, rc=rc,
               seconds=round(time.monotonic()-start, 3), log=str(path))
    print(json.dumps(row), flush=True)
    return row
hogs = []
try:
    for _ in range(a.hogs):
        hogs.append(subprocess.Popen(['python3', '-c', 'while True: pass'], start_new_session=True))
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.copies * len(specs)) as pool:
        for round_no in range(1, a.rounds+1):
            futures = [pool.submit(run, spec, round_no, copy)
                       for spec in specs for copy in range(1, a.copies+1)]
            rows.extend(f.result() for f in futures)
finally:
    for hog in hogs:
        os.killpg(hog.pid, signal.SIGKILL)
        hog.wait()
    (root / 'results.json').write_text(json.dumps(rows, indent=2)+'\n')
    for name, _, _ in specs:
        group = [r for r in rows if r['suite'] == name]
        print(f'{name}: {sum(r["rc"] != 0 for r in group)}/{len(group)} failures', flush=True)
```
