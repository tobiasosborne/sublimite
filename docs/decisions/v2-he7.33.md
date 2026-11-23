# v2-he7.33 · W0.7-B6: Wave 0 review fixes, phase 2 (contract-case plumbing)

Findings 2 (case half), 9, 17, 19, 20, 21, 23, 24 of `docs/reviews/wave0-code-astra.md`, and the
advisor's R6/R1 allocation-count ruling. Measured on the ThinkPad, 12 CPUs, 2026-11-23 21:20–22:10
CET; each number carries its battery stamp and load. Receipts are under `build/b6/` and
`build/b6/reds/` in the worker checkout.

## 1. One owner for private X servers (findings 19, 9)

`tests/contract/xserver.py` replaces six copies (navigation, files, recovery, large_undo, the
adapter, the driver suite). `private_server(display, artifacts, geometry=, label=)` probes the
explicit number (lock, socket, abstract name), writes a fresh FamilyLocal MIT cookie `O_EXCL`
0600, starts Xvfb with `-displayfd -noreset -nolisten tcp -auth`, and reads the answer across
partial writes until newline, EOF or `READY_S`. `stop(process, timeout_s)` is the one
TERM → kill → reap routine. `fresh_display` adds the driver bound to that server and writes the
cookie into the case's sandbox Xauthority. Navigation's `xauth` subprocess is gone.

- **Invariant.** Every server a case observes was started by this process for exactly one case
  or one scored editor launch, answered its own explicit number before first use, and is
  reaped before the owner returns. A server that outlives SIGTERM by `STOP_S` is killed, reaped
  and then raised as `TeardownError`; one that exits before its owner stops it is
  `TeardownError` too. `TeardownError.pending` carries the exception the block was already
  raising, so a case records its own FAIL (or ERROR) and the teardown ERROR beside it.
- **Simplest alternative: one server per invocation, with teardown that raises.** Counter-example
  (advisor, then this bead): v1's R6 count is 4 on a fresh server and 0 after R1 on the same
  server; R4's relaunched editor counts 4 on a server of its own and counted 0 (run
  contract-capacity-v8wtgv9p) after the producers and the restored editor had used it. One
  case's history changes another's scored observation. Now every case (navigation R1, R6) and
  every scored launch (files R3, R5 open and reopen; recovery setup, original and restarted;
  each X3 session; capacity's sacrificial, producer pair, restored and relaunched; each adapter
  launch) has a server no earlier case or launch used. The two capacity producers stay on one
  server: they are one simultaneous fixture.
- **Simplest alternative for teardown: kill, reap, return (the old copies).** Counter-example: a
  stand-in that acknowledges readiness and ignores SIGTERM. The old driver suite under it exits
  0 (red); with the owner it exits 4 with every test PASS and an ERROR `tearDownModule`
  (`test_plumbing.py`).
- **Deadlines** (slowest (M) × 10, rounded up to a whole second, R4 and X3 precedent). `READY_S`
  = 3 s from 0.26 s (X3 runs, load 1–17, v2-he7.15 Deadlines; here 0.068 s [Not charging, load
  1.7] and 0.077 s [Discharging, load 7.0], 20 starts per geometry). `STOP_S` = 1 s from
  0.0154 s [Not charging, load 1.7] (0.0075 s at load 7.0 [Discharging]). X3's `XVFB_READY_S`
  moved here unchanged in value.
- **Noted.** A SIGKILLed real Xvfb leaves its `/tmp/.X11-unix/X<n>` socket, so the display stays
  "occupied" for every later owner. The stand-in wrapper that runs a real Xvfb under a
  TERM-ignoring parent uses `PR_SET_PDEATHSIG = SIGTERM`, so its server exits cleanly. The first
  red run of this bead (before that fix) left `/tmp/.X11-unix/X121`; it is outside the checkout
  and was not removed.

## 2. R6/R1 (and R4) allocation counts (advisor ruling, finding 4)

Each assertion may carry `"count": {target: "recorded" | "zero"}` in the manifest, validated
with the rest of it. The case keeps the instrument's health as its predicate (exactly one
report, guard active, at least one measured scope) and puts `{"allocations", "scopes"}` in the
record's `law2`. The runner applies the target's count predicate: `zero` turns a PASS with a
nonzero count into FAIL; either predicate turns a PASS without a valid count into ERROR. The gate
entry carries `count: {predicate, observed}`.

- **Invariant.** The v2 predicate lives in the reviewed manifest next to the v1 one, so the
  W-conversion bead inherits `count == 0` with the target binding and cannot omit it; no case
  script carries target-specific expectations (wave0-contract-design.md, Decisions).
- **Simplest alternative: the case asserts by target role.** Counter-example: the case would then
  carry the per-target expectation the design forbids, and a v2 run of an unchanged case could
  only assert what the case's author remembered to branch on.
- **Ids.** `R1.law2-count`, `R6.law2-count` (new) and `R4.law2-count` (converted): recorded on
  v1-pin, zero on v2. R4 is converted because on its own server v1 counts 4 in two of two runs:
  the count rule (recorded unless v1 shows 0) applied as written. R3 and R5 follow in the
  follow-up round below; R31 stays asserted 0 in the case (0 in every run).
- **R6 three runs** (step 2b): 4, 4, 4 (R1: 4, 4, 4) [Discharging, load 4.7–5.2], each case on a
  server of its own. The count is the same in every run. Through the runner on a
  navigation-only diagnostic manifest: `count: recorded` PASS with `{allocations 4}`; the same
  records under `zero` FAIL exit 3.
- Not backtraced (ruling c).

## 3. One snapshot transport and one count parser (finding 21)

`Inspector` (`test_oracle_inspector.py`) gains `attach(child)` and a `packet()` that waits on the
reply socket and the editor's pidfd under `deadline` (WATCHDOG when unset). `capacity(width,
height)` sizes the mapping per screen: navigation previously mapped the 640×480 bound on its
1280×1024 screen. `law2_count(log)` returns one validated `Law2(allocations, scopes)`; zero or
nonzero policy stays at each call site.

- **Invariant.** A reply already queued is the editor's answer to our marker whether or not the
  editor exits in the same instant; liveness is a separate fact the case checks where it needs
  it (navigation's clean `stop` expects 0, recovery requires WIFSIGNALED/SIGKILL).
- **Simplest alternative: exit wins (navigation's old copy).** Counter-example: reply queued, then
  exit, both readable at one `select`: navigation reported `Refused` and lost a complete answer;
  files' copy returned it (`build/b6/red21.py`). Now one policy.
- **Count parser.** Exactly one report. Counter-example to first-match: a log with two reports
  (0 then 4) was read as 0 by all four old copies.

## 4. Executable binding travels in argv (findings 2, 20; advisor question)

**Why was it an environment variable?** v2-he7.16 records the reason: "it needs no manifest or
case argv change and reaches every invocation the same way." That is convenience, not
correctness. It is an argument now: the manifest's `{digests}` placeholder expands to the
selected target's JSON `{absolute path: sha256}`, and every case that launches an editor takes
`--digests` (`records.digests_argument`, required). Harness invocations that launch nothing get
no binding.

- **Counter-examples to the variable.** (a) Inheritance: the X3 self-test runs the case as a
  grandchild of the runner; under the variable the stub run silently inherits the harness's
  `{}` (or, nested in a gate, the gate's binding). With an argument the self-test states the
  stub's binding and identifies it (`--diagnostic-fixture`, recorded as
  `binding: diagnostic-fixture` and `diagnostic_fixture: true` on each record). (b) Reach: the
  variable reached sandbox, wire and driver invocations that execute nothing.
- **Why not a file named on the command line.** A file can change between the runner writing it
  and the case reading it, which is the window this finding closes; argv is fixed at exec and
  lands in the receipt's `command`. Size: at most four entries, about 520 bytes.
- **Stand-alone runs** take the same selection: `run.py --binding` prints it (Makefile
  `CONTRACT_BINDING`; diagnostic targets now need `ORACLE` and `ORACLE_TREE`).
- **Removed:** recovery's stock digest literal; the stamp/pin/patch re-reads in navigation,
  files, capacity, recovery and the adapter. **Kept:** the sandbox's hash of the fd it executes;
  the builder's preimage/postimage checks; the runner's re-hash before launch.
- **Proofs.** Select A, replace it by B with a matching new stamp: the old navigation, files and
  capacity launched B (`build/b6/red2_cases.py`: launched True); now all five scripts, adapter
  included, record one ERROR "digest mismatch" and B never runs (`BindingTests`). X3: the old
  case launched a replaced stub and injected keys; now ERROR with no launch or key step
  (`test_large_undo.py` subtest). Pre-ACK under a deliberately changed diagnostic manifest
  (`oracle` re-bound to the inspected tree's `sublimite-stock`): the old literal refused it
  ("digest mismatch"); now selection and launch agree (`b8faeab5…`, both controls PASS).

## 5. Redundant validation and duplicate passes (findings 23, 24)

- `Sandbox._checked` keeps only `env == self._env`: the constructed environment is absolute,
  private and under the pinned root, so the per-variable profile/relative/missing checks before
  it could never decide anything. Caller-supplied paths keep `_reject_profile_path`. The refusal
  matrix is unchanged (21/21).
- `relay` and `restart` walk the tree once: one `_checked` context covers peer verification (or
  the stale-socket checks) and the spawn, with `_verify_endpoint` immediately before the spawn.
  Root walks over the relay/restart and orderly-restart tests: 14 → 11 (`build/b6/walks.py`).
- `tools/clex.py`: a line comment ends at the first newline; `check_isolation` lexes despliced,
  newline-normalized text. New fixtures: a spliced (LF and CRLF) line comment that swallows a
  `"sublimite"` stays clean; a literal on the line after a comment is flagged at line 2. Mutant
  evidence: without the caller's desplice the literal surfaces; a comment-to-end lexer misses
  the next-line literal.
- Recovery writes its source without `fsync`: the runner kills one editor, the kernel stays up.
- R3 no longer waits for v1's first journal rename before Ctrl+S: the settled SNAP that draws the
  edited b is the A9 prerequisite. X3's first-edit fence stays (its stock editor has no SNAP).

## 6. Red receipts verify their cause (finding 17)

`tests/contract/reds.py` (`make contract-reds`) runs each of the 37 red controls once and reads
its records with the runner's reader. A red qualifies only if the first non-PASS record is its
ID and status, nothing of that case follows, every earlier manifest ID of the case passed, its
predicate and step name its own boundary, and, for an expectation swap, the record's `ordinary`
shows the healthy observation equal to the ordinary expectation with a contrary expectation that
differs from both. Navigation, files, recovery and capacity now check the ordinary expectation
first under a red and record it (`ordinary`); a failing ordinary is ERROR, never the red.

- **Simplest alternative: the command's exit status.** Counter-example: an earlier FAIL at
  `bottom-setup` and a different comparison failing at `wrapped-return` both exit 3 and would
  be credited as `R1.wrapped-return`.
- **Retaken on v1-pin** [Discharging, load 3.0–4.6]: 37 of 37 qualify. `R3.law2-count` did not
  qualify on its first attempt: that run's healthy count was 11, not 0, so the checker refused to
  credit it; the retake qualified. Live disqualification: with the display occupied, both
  `R1.wrapped-return` and `R3.save-signal` were refused (setup ERROR, no preceding PASS).

## Invocation bounds

- `plumbing` (new): 115 s = slowest 11.40 s (make check, [Charging, load 8.56]) × 10.
- `x3-selftest`: 126 s = slowest 12.58 s (standalone, four stub runs now; that run's power and
  load were not stamped; stamped: 11.44 s [Charging, load 8.56], 9.49 s [Charging, load 7.33])
  × 10, replacing 116 s.

## Red and green

| Step | Red (before) | Green (after) |
|---|---|---|
| 9 | old `test_xdriver.py` under a TERM-ignoring server: exit 0, 8 PASS | exit 4, 8 PASS + ERROR `tearDownModule` (TeardownError) |
| 19 | (new owner) | `test_plumbing.py` XServerTests 7/7: occupied, wrong/short/split/silent/early-ended answer, TERM ignored, early exit, authenticated client at default, 640×480, 1280×1024 |
| 2b | R6 4 vs 0 by server history (advisor) | R6 4, 4, 4 on own servers |
| 2a | old runner: count 1 under `zero` PASS, missing count PASS | `test_count_predicate_per_target` |
| 21 | old transports disagree; first-of-two report read | TransportTests 4/4, CountTests |
| 2/20 | old cases launch a replaced editor; old pre-ACK refuses a re-reviewed one | BindingTests, X3 subtest, pre-ACK diagnostic PASS |
| 23/24 | mutants as above; walks 14 | isolation 44/44, sandbox 21/21, walks 11 |
| 17 | exit status credits wrong causes | RedTests 4/4; 37/37 receipts qualify |

## Outside this bead (reported, not fixed)

- *Resolved in the follow-up round below:* R3.law2-count on v1-pin was not reliably 0.
- `recovery.py` still refuses a restarted editor whose PID equals the killed one (finding 26).
- `/tmp/.X11-unix/X121` left by this bead's first red run (section 1).

## Follow-up round (advisor rulings, 2026-11-23 22:20)

- **R3 and R5 counts through the manifest predicate.** `files.py` no longer asserts a count. Its
  `law2-count` requires one healthy report per window (`law2_count`), records
  `law2 = {allocations, scopes}` summed over the case's windows, and the runner applies the
  manifest's predicate: R3 recorded on v1-pin (advisor: assert 0 on v1 only where v1 shows 0),
  R5 zero on both targets (the count rule's v1 proof stays, now through the same path). R5 sums
  its open and reopen windows: the sum is 0 exactly when both are. Every count ID now runs
  through one mechanism (R1, R3, R4, R5, R6); R31 still asserts 0 in the case (two counted
  windows in one record; not part of this round).
- **The count reds** keep their IDs. Under `--negative R3.law2-count` or `R5.law2-count` the case
  first requires the healthy report (an unhealthy one is ERROR), then expects one allocation
  more than was counted: the FAIL shows the ID can fail on the case's real count. The
  predicate's own red is the runner's (`test_count_predicate_per_target`; live, the
  navigation-only manifest under `zero`). Retaken on v1-pin [Charging, load 2.6]: both qualify.
- **Manifest evidence (advisor condition b).** Each count note names its v1 runs and values:
  R1 4 in 10 of 10, R6 4 in 9 of 9, R4 4 in 2 of 2, R3 11 in 3 of 13 (this bead) and 1 of 8
  (pre-bead), 0 otherwise; R5 0 and 0 in 20 of 20 (12 and 8). A run counts when its editor
  printed one active report over at least one window; tallied from every log under `build/`.
  `test_run.py` requires a run count in each note.
- **Runner review (wave0-runner-astra.md).** `--validate` rejects a count predicate on a target
  whose expectation is not PASS: a signed FAIL carries no count, so count + reviewed XFAIL let a
  signature without a count pass (red: gate exit 0; green: ERROR, zero invocations). A count
  value is type-checked before membership, so an array or object is an invalid-manifest ERROR
  with a receipt instead of a TypeError (red: no gate record).
