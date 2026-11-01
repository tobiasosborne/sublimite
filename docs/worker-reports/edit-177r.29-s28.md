# edit-177r.29 — Windows timeouts under concurrent load

Verdict: **all three reported timeout sites are (a). No product hang or lost
wakeup (b) was demonstrated.** A further crash-fixture race was reproduced and
fixed without changing production. Scoped fixes and verification runs are complete. Full Windows exits were
**2 and 0**: the first had one unrelated unchanged platform-test failure.
All affected release/sanitizer passes succeeded in both full runs.

All measurements are **(M) [AC, desktop Spiral]**, with Tobias using the box.
BAT0 is absent. Every build/test uses `taskset -c 48-63 nice -n 10`, make
`-j16`; Windows executables enter only through `tools/winrun.sh`, private
stations, below-normal priority. No quiet-box wait, desktop switch, live input,
clipboard sharing, package installation, state-changing git or other worktree
access was used. Initial uptime load: `0.07/0.44/2.34`; red native campaign:
`0.63/0.79/2.22` to `1.01/3.08/3.06`; overlapping Linux build/check reached
`9.41/3.98/3.20`. Linux uptime does not measure Windows scheduler or I/O load.

## Diagnosis and unchanged assertions

**Seeded journal writer.** The brief's `reached && st.how==OS_PROC_ABNORMAL`
assertion is in `tests/win/journal_kill_test.c`; the included portable test
supplies its diagnostic macro/name. It is distinct from the portable
boundary-ready wait. Every reproduced seeded timeout had `issued=0`,
`cached=0`, `ack=0`, `error=0`. Child samples included zero CPU milliseconds
and zero native read/write operations. This was before any issued edit,
not an append/replay stall after progress. The longer guard has already
observed the exact target after 24.608 seconds in the same contention recipe.

Red excerpt, `observe/journal-rel-1-1.log`, suite exit **1**:

```text
journal random trial=0 target=1 issued=0 cached=0 ack=0 error=0 previous_cached=0 elapsed_ms=10000 reached=0
timeout pid=92416 exit=259 priority=16384 cpu_ms=0
timeout pid=92416 read_ops=0 write_ops=0 read_bytes=0 write_bytes=0
journal_hot_exit_test:122 FAIL reached && st.how==OS_PROC_ABNORMAL native=18
```

Native error 258 was overwritten by diagnostic calls in these instrumented
assertion lines; the sampled live child and expired condition establish the
timeout. The portable boundary wait received the same Windows-only native
phase guard and diagnostics. Its Linux deadline stays unchanged. Phase enum
`DELETE` became `DELETE_BOUNDARY` because including Windows diagnostics makes
the native `DELETE` access-right macro visible; enum values are unchanged.

The parent still waits for `cached >= seeded_target` before authorizing the
kill, then requires abnormal death, exact metadata, sequence bounds
`cached <= replayed <= issued`, the acknowledgement bound, and both the piece
tree and decoded model equal to the independent seeded byte oracle. Counts,
seeds, edit script and random targets are unchanged.

**Executable / `other_window`.** All four release copies in the first red
round failed this exact readiness condition. `wait_window` already tests
`SublimiteTestReady`, published after successful editor open and argv admission.
It now allows the derived native phase guard. Observation copies changed only
that startup guard and then ran the original save/crash/recovery/exit checks:
all sixteen passed, with readiness as late as 17.134 seconds. Final stronger
contention has also observed readiness near 25 seconds.

An intermediate green sanitizer copy hit the existing five-second save wait.
Extending that wait did **not** fix all failures: `final-exe` had two sanitizer
copies still showing the old bytes after 120 seconds. Those were retained as
red evidence, not relabeled slow passes. Native exit/death waits also exposed
an independent five-second guard (`byte-fast/exe-san-1-2.log`, `native=258`).
All executable native completion waits now use the derived guard; the deliberate
five-second occlusion observation sleep is unchanged.

The permanent byte mismatch exposed a second **test bug**: scanning for an
insert with PAINT clear and then killing the child is not atomic. The UI may
arm its first-paint guard between the scan and `TerminateProcess`. Recovery
intentionally restores a tab killed inside guarded composition as closed.
The test then opens the argv path fresh and expects the closed tab's edit in
that fresh buffer. A longer save wait cannot repair that fixture mistake.

The fixture now suspends only its own child's UI thread, synchronizes the
suspension with `GetThreadContext`, and rescans the exact edit/no-PAINT condition.
It kills while that thread remains stopped, or resumes/retries if the condition
changed. Worker I/O may finish; UI composition cannot cross the boundary.
Every original byte/undo/save/recovery assertion stays exact.

**Measured race witness**, `freeze-probe/exe-rel-1-5.log`: the parent saw the
`!` marker with 26 valid records; the last PAINT was sequence 26 / buffer 0.
After stopping the UI, the same journal had sequence 27 / PAINT buffer 1.
The fixture resumed, retried at 30 records, killed outside PAINT and passed
byte-exact recovery. `type=11` is `JOURNAL_PAINT` from the enum, not a new schema.

```text
editor_exe: marker=! records=26 journal=...session-C9CgyS
journal state seq=17 type=2 buffer=1 size=9 inserted=[!]
journal state seq=26 type=11 buffer=0 size=0
journal state seq=27 type=11 buffer=1 size=0
editor_exe: kill boundary raced PAINT marker=!; resume/retry
editor_exe: marker=! records=30 journal=...session-C9CgyS
editor_exe: TerminateProcess mid-edit -> same-profile relaunch -> byte-exact recovery PASS
```

The final ordinary contention caught the boundary change in two sanitizer
copies and both passed after retry. All sixteen release and sixteen sanitizer
copies passed after the fixture fix. The probe is the same test plus a read-only
journal dump while paused; its source is `build/edit-177r.29/editor_exe_freeze_probe.c`.
This evidence identifies an admitted crash inside a deliberately guarded phase,
not an unbounded recovery or lost wakeup.

**Editor / `--close-queue`.** The diagnostic full Windows check reproduced
`wait=258` on this selector in sanitizer mode. In the stronger campaign every
one of sixteen close children exceeded the original parent deadline. On the
same children, continuing observation completed startup, clipboard publication,
queued typing, close, renderer teardown, reopen, and exact recovery. All sixteen
passed. An example at the old deadline had zero CPU milliseconds and only four
write operations; the child had not reached the logged editor-open phase.
The whole close campaign took up to 39.536 seconds including its relay/parent.
There was no demonstrated lost close wakeup.

Excerpt, `observe/close-rel-1-1.log`, same child, exit **0**:

```text
observe original child deadline mode=--close-queue
timeout pid=98212 exit=259 priority=16384 cpu_ms=0
editor_win: opening mode=--close-queue
editor_win: opened
round4: close boundary cut accepted typing exact, late input excluded, prompt=0 PASS
editor_win: closed
editor_win: recovery opened
round4: close-boundary recovery bytes exact PASS
observe continued child wait=0
```

The close FIFO order, accepted `q`, excluded late `r`, clipboard deletion order,
exact in-memory bytes and exact recovered bytes remain mandatory. Editor-local
settle/close-loop guards remain intact. The parent guard now accounts for the
whole child scenario rather than three five-second settle loops. The nested
crash campaign allows its two child guards plus its own phase.

Native wait chains showed blocked/running threads with no reported cycle.
Those snapshots alone cannot exclude a lost wakeup. Continuing the same
children to successful original byte/ordering checks is the decisive evidence.
The data does not distinguish CPU scheduling delays from native loader,
driver or filesystem waits; it does not justify a speculative production fix.

## Guard derivation and hung-child control

`tests/win/timeout_diag.h` documents the derivation beside the constant.
(E) Reuse the existing OS conformance whole-case guard, 120 seconds.
(M) Eight copies per affected suite measured up to 39.536 seconds for the
entire close/recovery scenario: `120/39.536 > 3`. Readiness alone exceeded
five seconds. Below-normal priority and native image/driver/file I/O supply no
five-second scheduling guarantee. This is a hang guard, not a latency gate.

(P) A seeded writer does at most 100000 edits, each with two progress writes
and a flush every 4096. (M) The slowest completed red target was 83865 edits
in 3.718 seconds. (E) Scaling gives
`3.718 * 100000 / 83865 < 4.434 seconds`; the guard has more than 27 times
that work allowance and also covers native startup. An enlarged number with
no relation to the workload was not used.

The negative control at `build/edit-177r.29/hang_control.c` launches its own
child parked forever, waits with the actual shared guard, then kills/reaps it.
Command (exit **0**):

```sh
taskset -c 48-63 nice -n 10 tools/winrun.sh build/win/rel/tests/hang_control.exe
```

```text
hang control wait=14 expected=14 elapsed_ms=120010
hang control: hung child timed out, killed and reaped PASS
```

The measured 120010 ms includes scheduling around the 120-second wait. A
permanent hang does not become a pass or authorize recovery assertions.

## Contention counts and reproduction

Cells are **failures / attempts**, by suite exit code, without automatic retry.
(E) Four copies are the requested suite concurrency; eight doubles it for the
stronger test. Two rounds repeat each configuration. Release/sanitizer stay
serial per copy. The three selected suites run concurrently, as do other lanes
and the required full checks. This is stress evidence, not a confidence bound.

| Campaign | Copies × rounds | Journal rel | Journal san | Exe rel | Exe san | Close rel | Close san |
|---|---:|---:|---:|---:|---:|---:|---:|
| `red-native`, original waits + diagnostics | 4 × 2 | 4/8 | 8/8 | 8/8 | 4/8 | 4/8 | 0/8 |
| `observe`, longer startup/parent observation only | 8 × 1 | 8/8 | 8/8 | 0/8 | 0/8 | 0/8 | 0/8 |
| `green`, native guards; old kill fixture | 8 × 2 | 0/16 | 0/16 | 0/16 | 2/16 | 0/16 | 0/16 |
| `final-exe`, save guard; old kill fixture | 8 × 2 | — | — | 0/16 | 2/16 | — | — |
| `byte-diag`, old kill fixture + record dumps | 8 × 1 | — | — | 0/8 | 0/8 | — | — |
| `byte-fast`, original short native waits + dumps | 8 × 2 | — | — | 0/16 | 1/16 | — | — |
| `freeze-final`, final executable fixture | 8 × 2 | — | — | 0/16 | 0/16 | — | — |
| `freeze-probe`, paused journal dump | 8 × 1 | — | — | 0/8 | 0/8 | — | — |
| **Final per-case evidence** | 8 × 2 | **0/16** | **0/16** | **0/16** | **0/16** | **0/16** | **0/16** |

`observe` deliberately left journal waits at ten seconds; all sixteen journal
failures were before the first issued edit. All sixteen close observation
copies crossed the original parent deadline and then passed. Exe observation
had eight release and sixteen sanitizer readiness waits over five seconds;
all original assertions passed afterward.

The final per-case row takes journal/close counts from `green` (those final
implementations were unchanged) and executable counts from `freeze-final`.
The intermediate `green` executable launches include the old short save guard
in round 1 and the longer save guard in round 2; both still used the racy kill.
`freeze-probe` adds eight further passing copies per configuration and one
explicit paused-PAINT journal witness.

Driver and raw evidence: `build/edit-177r.29/contention.py`,
`<label>-counts.log`, `<label>/results.json`, and one combined log per launch.
`--random-only` selects the full existing fifty-trial seeded campaign;
`--close-campaign` supervises the unchanged `--close-queue` selector. Exe runs
the complete suite. Driver exit is nonzero if any child fails. Its outer
supervisor is a distinct emergency stop; no reported run reached it.

Commands, each with the indicated source/wait semantics above:

```sh
taskset -c 48-63 nice -n 10 python3 build/edit-177r.29/contention.py red-native 2 4
taskset -c 48-63 nice -n 10 python3 build/edit-177r.29/contention.py observe 1 8
taskset -c 48-63 nice -n 10 python3 build/edit-177r.29/contention.py green 2 8
taskset -c 48-63 nice -n 10 python3 build/edit-177r.29/contention.py final-exe 2 8 exe
taskset -c 48-63 nice -n 10 python3 build/edit-177r.29/contention.py freeze-final 2 8 exe
taskset -c 48-63 nice -n 10 python3 build/edit-177r.29/contention.py freeze-probe 1 8 exe
```

The initial `red` label is INVALID evidence: sandbox WSL interop socket
creation failed before any Windows test ran. Valid runs used approved
unsandboxed execution, retaining affinity/nice and the private-station runner.
Diagnostic compile errors are retained in `build-diag.log` and `build-green.log`;
corrected builds exit zero. They are not runtime test failures.

## Verification and files

- `taskset -c 48-63 nice -n 10 make -j16 all`: **0**, GCC 13.3.0,
  `build/edit-177r.29/all-final.log`.
- `taskset -c 48-63 nice -n 10 env DEBUGINFOD_URLS= make -j16 check`: **0**,
  all 117 binaries / 121 tasks and shell checks, no FLAKY;
  `build/edit-177r.29/check-linux-final.log`.
- Diagnostic `WIN_CHECK_JOBS=4 make -j16 TARGET=win check`: **2**,
  130 binaries, three failing passes; `check-win-red.log`. Affected logs were
  preserved in `check-logs-red/` before the final checks overwrote runner logs.
- Intermediate Windows check (before atomic kill fixture): **0**, 130 binaries,
  no failing passes; `check-win-final-1.log`.
- Final Windows check 1: **2**, 136 passing configurations and one failing
  configuration (130 binaries plus six desktop passes and the expected ASan
  negative control). The only failure is the unchanged release/station
  `win32_plat_test` handle balance; `check-win-final-2.log`. All six affected
  release/sanitizer passes exited zero. Runner wall time was 3894.751 seconds.
- Final Windows check 2: **0**, all 137 configurations passed (including the
  expected ASan negative control), 130 binaries, six desktop passes, 162 SKIP
  lines; `check-win-final-3.log`. All six affected release/sanitizer passes
  exited zero. Runner wall time was 3872.843 seconds.
  Both have `WIN_CHECK_JOBS=4`; the second uses a temporary Python wrapper that
  changes only the runner log directory to `build/edit-177r.29/check3-logs`.
  `cmp` verified the copied runner differs only in that path. This allows both
  full checks to supply concurrent load without overwriting evidence. Tracked
  `tools/win_check.py` is unchanged.
The two final complete commands were:

```sh
taskset -c 48-63 nice -n 10 env WIN_CHECK_JOBS=4 make -j16 TARGET=win check
taskset -c 48-63 nice -n 10 env WIN_CHECK_JOBS=4 PATH="$PWD/build/edit-177r.29/check3-tools:$PATH" make -j16 TARGET=win check
```

The long full-check wall times are retained: sanitizer journal in check 1
reported 3358.613 seconds and release journal in check 2 reported 3286.081
seconds. Their fifty measured target waits sum to only 27.936 and 21.148
seconds respectively, with maxima 0.953 and 0.768 seconds. The delay is outside
those measured target-wait intervals (spawn/death/replay/other parent work or
relay); no narrower attribution is claimed without an external trace. These
were loaded checks, not quiet reruns, and they completed the original oracles.

- Hung-child control: **0**. `git diff --check`: **0**.

Changed: `tests/journal_hot_exit_test.c`, `tests/win/journal_kill_test.c`,
`tests/win/editor_exe_test.c`, `tests/win/editor_win_test.c`, new
`tests/win/timeout_diag.h`, this report, and
`docs/decisions/edit-177r.29.md`. No production file changed.

Found, not fixed: `tests/win/win32_plat_test.c:916`, unchanged release station
pass, asserted `handles_after <= handles_before` but measured 130 before and
132 after. It failed in the first post-fixture full check. Preserved log:
`build/edit-177r.29/win32-handle-balance-red.log`. No claim is made about whether
that is a persistent product handle leak or asynchronous teardown; it is outside
the three timeout cases, and binding rule 7 requires reporting it rather than
fixing another scope. Coordinator triage is needed. Existing live-only SKIPs
stay explicit. The coordinator must triage that separate handle-balance
failure; it was not silently retried or included in a claim of two green runs.
No decision about the three scoped cases remains open.
