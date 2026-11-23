# W0.6i · Contract runner and the Wave 0 gate record

**Decision.** One shared record module (`tests/contract/records.py`), one reviewed manifest (`tests/contract/expectations.json`) and one runner (`tests/contract/run.py`). Cases observe; the runner judges. A case reports PASS, FAIL or ERROR per assertion and, on a FAIL, may name the exact failure mode it saw (`signature`). Whether that mode is a reviewed expected failure of a given binary is decided only by the manifest, bound to the binary's identity. `make check` runs every contract self-test that needs no oracle through the runner; `make contract-gate` runs the v1 oracle gate and writes the gate record.

**Invariant.** For a run, every assertion the manifest requires of the selected target has exactly one record from exactly one invocation of its case script, and the run's verdict is a pure function of those records, the invocations' exit statuses and the manifest. Nothing is invoked twice, no record is synthesised, and anything the runner cannot explain is an ERROR.

**Simplest alternative, rejected.** Each case keeps its own expectations (as `large_undo.py` did with `EXPECTATIONS` keyed by the stock digest) and the gate is a shell loop over case exit statuses. Counter-example: X3 on the pin exits 0 under that scheme because the case itself turned the A8 mode into XFAIL. Rebuild the stock binary with a one-byte change and the case's digest lookup misses, so the same exit-and-crash outcome becomes ERROR. That is fail-closed. But reverse the mistake: a reviewer adds the new digest to the case to "fix" it, and the XFAIL now covers a binary nobody red-witnessed. A second copy of the expectation also lives in the case (as the A8 binding did in `large_undo.py` and `recovery.py`), and a shell loop cannot see a missing assertion (R4), a duplicate record, or a case that exits 0 with a FAIL record. The manifest makes the binding one reviewed line per target. The runner checks the rest.

## Exit statuses

One table for every case script, contract self-test and the runner (`records.EXIT`):

| Status | Exit | Who | Meaning |
|---|---|---|---|
| PASS | 0 | all | every record PASS; for the runner, every required assertion PASS or XFAIL with its reviewed signature |
| FAIL | 3 | all | at least one FAIL, no ERROR |
| ERROR | 4 | all | at least one ERROR, or no records; the runner's own error channel (below) |
| XPASS | 5 | runner only | an assertion expected to XFAIL passed; fails the run pending review |

Python reserves 1 (uncaught exception) and argparse 2 (usage error); the runner treats them, and any other status, as an abnormal exit (ERROR). Precedence for the runner: ERROR > FAIL > XPASS > PASS. An XFAIL-only success needs no separate status: the summary lists it, and the gate record keeps the outcome per assertion. Before this bead `navigation.py` exited 0/1, `large_undo.py` 0/1/2 and the unittest self-tests 0/1; all now use the table.

## Result records

A record is one stdout line, `RECORD ` + a JSON object, printed through `records.emit`, which validates it first. Required keys: `case_id`, `assertion_id` (non-empty strings), `status` (PASS, FAIL or ERROR), `evidence` (`A`, `B`, `C`, `A+B` or `harness`), `first_required_wave` (non-negative int) and `predicate` (string). `signature` is allowed on a FAIL only. Every other key is case evidence (observed, expected, run ID, seed, ticket and serial, child status, artifacts).

`first_required_wave` is the integer of the first v2 wave that runs the assertion's public case. Previously, `files.py` wrote ints and the others wrote "W0". The ints follow the ledger: R1, R5, R6, X3 and the R31 controls are 2; R3 and R4 are 3; harness self-tests are 0. R31 and X3 are W1 in the ledger for their unit/headless tests. Their black-box contract scripts drive the GUI, which exists from W2, so the scripts carry 2. An int is ordered and needs no parser. Per-case changes:

- `navigation.py`: `R1`/`R6` with their four assertion IDs each.
- `files.py`: unchanged IDs. It is now a plain script (it adds `tests/contract` to `sys.path` like the other cases), so the runner and `make check-contract-files` invoke it as `python3 -B tests/contract/cases/files.py`.
- `recovery.py`: one record per control, `case_id` `R31`, the control (`pre-ack-file` …) as `assertion_id`. The failing step moved from `assertion_id` to `step`. Evidence is A for pre-ACK and A+B for post-ACK (submitted-frame witness).
- `large_undo.py`: `case_id` `X3`, assertion IDs without the `X3.` prefix. `classify` no longer sees the target. The exact A8 mode is `FAIL` with `signature` `exit-1-error-105-destination-untouched`. Every neighbour of it stays what it was (ERROR for other exits, FAIL for wrong bytes). The in-script digest table is gone. `make check-contract-large-undo` therefore exits 3 on the pin. It is a diagnostic target; only `contract-gate` turns the signature into the reviewed XFAIL.
- Unittest self-tests (`test_sandbox`, `test_inspect`, `test_xdriver`, `test_large_undo`, `test_oracle_inspector`, `test_run`) end in `records.unittest_main` or `records.run_suite`. That emits one record per test method (`Class.method`). A skip, `expectedFailure`, unexpected success or failing module/class fixture is an ERROR, and an empty suite is an ERROR record `collection`.

The runner caught one real leak while being wired in: `test_large_undo`'s stub run called the X3 case in-process, so the case's own ERROR record landed among the self-test's records (`exit 0 disagrees with its records`). The stub run is now a child process with its own transcript, as the runner invokes cases.

## Manifest format (`expectations.json`, format 1)

- `targets`: name → `{role, identity, note}`. The role is `harness`, `v1` or `v2`. `identity` is an object compared for exact equality with the identity the runner computes, or null (unbound). `harness` has `{}` (no executable hashed). `v1-pin` binds the pin, the patch SHA-256, the SHA-256 of every executable in the inspected build stamp (`oracle_build`: `sublimite`, `sublimite-stock`, `sublimite-drop-submit`) and the stock oracle (`executables.oracle`). `v2` is null until the W2 native inspector exists, so a v2 run is an ERROR. A rebuild with other bits is an unreviewed identity (ERROR) until the binding is re-reviewed. Selection is never by binary name.
- `invocations`: name → `{argv, displays (0 or 1), timeout_s, timeout_derivation, env?}`. `argv[0]` is a repo-relative script run as `python3 -B`. Placeholders are `{display}`, `{exe:NAME}`, `{oracle_build}` and `{artifacts}`; an unknown one is an ERROR.
- `cases`: case ID → `{invocation, evidence, first_required_wave, expect, assertions}`. `expect` maps target name → `"PASS"` or `{"status": "XFAIL", "signature", "witness"}`, exactly those keys, both non-empty. An XFAIL without its signature or its red-run witness is an invalid manifest. Each assertion may override `evidence`, `first_required_wave` and per-target `expect`. A required assertion is one whose merged `expect` names the selected target.
- JSON duplicate keys are rejected while parsing (`object_pairs_hook`), so a duplicate case or assertion ID cannot silently shadow another. Full IDs `case.assertion` must also be unique.

`run.py --validate` checks the checked-in manifest; `test_run.py` runs it in `make check`.

X3's only XFAIL row is `X3.over.roundtrip` on `v1-pin`, witness run `20261123T092647Z-144204` (docs/decisions/v2-he7.15.md, amendment A8). R4 is listed and required with one placeholder assertion `restore-under-pressure` and invocation `capacity` (`tests/contract/cases/capacity.py`, absent). Bead v2-he7.17 replaces the placeholder with its inventory, display needs and derived watchdog. Until then every gate run fails with R4 MISSING. Future ledger rows (R2, R7, …) are not in the manifest; they stay in the ledger and are not counted as run.

## Runner semantics

1. Load and validate the manifest, compute the identity (`--exe NAME=PATH` hashed, `--oracle-build` stamp read and every listed executable re-hashed against it), select the target whose identity matches and check its role against `--role`. Any failure here is an ERROR with zero invocations.
2. For each invocation owning a required assertion, in manifest order:
   - Check that the script exists.
   - Take a fresh display from the `--displays` pool. A display is free when it has no lock or socket file and its abstract name binds (`test_xdriver.available`, reused). It is never reused within a run.
   - Drop `DISPLAY`, `WAYLAND_DISPLAY` and `XAUTHORITY` from the child environment.
   - Run the script once in its own session under `timeout_s`. On expiry, SIGKILL the session and record an ERROR.
   - Keep `stdout.log` and `stderr.log` under `<run>/<invocation>/`.
3. Reconcile each invocation:
   - A malformed RECORD line is an ERROR.
   - A duplicate record is an ERROR.
   - A record for an ID the invocation does not own, or owns but does not require of the selected target, is a problem either way: an ERROR one is reported as a harness error, any other status as foreign (W0.7-B1).
   - Evidence or wave differing from the manifest is an ERROR.
   - Zero records, an exit outside 0/3/4, or an exit that differs from `records.exit_status` of its records is an ERROR.
4. Outcome per required assertion:

   | Expected | Record | Outcome |
   |---|---|---|
   | PASS | PASS / FAIL / ERROR | PASS / FAIL / ERROR |
   | XFAIL | FAIL with the exact signature | XFAIL |
   | XFAIL | FAIL with no or another signature | WRONG-SIGNATURE (ERROR) |
   | XFAIL | PASS | XPASS |
   | any | ERROR | ERROR |

   With no record, the outcome is NOT-RUN only after an earlier stopping failure of the same case, otherwise MISSING (ERROR); the rule is under W0.7-B1 below.
5. No rerun, retry or FLAKY category exists. Each run writes into a new directory `<role>-<UTC>-<pid>`, so a manual diagnostic run keeps its own receipt beside the first.

## Gate record

`<out>/<run_id>/gate.json` holds:

- run ID, role and argv
- power/load stamps at start and end (BAT0 status, `os.getloadavg`)
- manifest path and SHA-256, SHA-256 of `run.py` and `records.py`
- the computed identity and the selected target
- run-level problems
- the selected target's digest binding (`digests`, W0.7-B1)
- per invocation: command, display, invocation count (0 or 1, derived from the attempted launch), exit, wall seconds, transcript paths, every parsed record and its problems
- per required assertion: invocation, evidence, wave, expected, outcome, observed status, predicate, signature and the case's artifact path

`summary.txt` is the short human form, also printed. `make check` writes its harness record under `build/contract-check/`, and `make contract-gate` under `build/contract-gate/`.

## Displays

`sandbox.CONTRACT_DISPLAY` (`:120`–`:199`) is the single rule every case and self-test now checks, replacing six per-bead decade regexes. The bead decades remain as defaults only: the driver's `:12x` for `CONTRACT_CHECK_DISPLAYS` and `:190-:199` for `GATE_DISPLAYS`. Collision safety comes from the owner's probe (lock file, socket path, abstract bind) before Xvfb starts, not from the regex.

## Watchdogs (`timeout_s`, a hang backstop, never a verdict)

Each case's own hang detectors fire first; the runner's watchdog catches a wait that has none.

| Invocation | timeout_s | Derivation |
|---|---|---|
| sandbox | 300 | (E) 17 serial waits × 10 s WATCHDOG (P) = 170 s; green 1.27 s (M) [AC], load 2.0 |
| wire | 60 | (E) no blocking wait; green 0.20 s (M) [AC] |
| xdriver | 900 | (E) below 60 × the 15 s driver WATCHDOG (P); green 1.57 s (M) [AC] |
| x3-selftest | 900 | (P) the stub stops at its first control: Xvfb 15 + map 30 + journal 120 + save 400 + exit 60 = 625 s, rounded up; green 1.32 s (M) [AC] |
| adapter | 1800 | (E) 9 tests × ≤ 10 serial 15 s operations = 1350 s |
| navigation | 2400 | (E) about 60 serial 15 s waits = 900 s, plus writing the 1 GiB R6 fixture |
| files | 1800 | (E) about 60 serial 15 s waits = 900 s, doubled |
| recovery | 1800 | (E) 4 controls × about 15 serial 15 s waits = 900 s, doubled |
| large-undo | 4500 | (P) sum of its serial hang detectors = 4055 s (2 × (3 + 1) saves × 400 + 2 × 2 commits × 120 + 4 maps × 30 + 4 exits × 60 + 15), plus fixture writing (E) |
| capacity | 3600 | (E) placeholder; bead v2-he7.17 derives it |

The (E) wait counts are read from the code, not measured. The gate run below records each invocation's wall time (M) for the next review of these rows.

## Red and green receipts (Law 9)

- **Runner self-test red** (`tests/contract/test_run.py` before `run.py` existed): 20 of 20 FAIL, exit 3 (`build/receipts/runner-red.log`).
- **Fail-once red against a mutant runner** that retries a non-zero invocation once: `RunnerTests.test_fail_once_is_final` FAIL, `AssertionError: 0 != 3` (the retry masked the first failure), exit 3. The mutant was a copy under `build/mutant-rerun/`.
- **Runner self-test green**: 20 of 20 PASS, exit 0. The list covers:
  - missing ID, duplicate ID in the manifest and in records
  - ordinary FAIL under an XFAIL expectation, with no signature and with another one
  - wrong role and an unreviewed identity
  - XFAIL without signature, with an empty signature, or without a witness
  - XPASS, missing display, the fail-once fixture (invocation count 1, first transcript kept, exit 3, a direct second invocation passes)
  - zero records, abnormal and inconsistent exits, malformed records (bad JSON, string wave, case-written XFAIL, evidence drift, unknown passing ID)
  - a harness ERROR on an unlisted step, NOT-RUN after a FAIL, timeout and a missing case script
  - the checked-in manifest
- **Registry slash-row lane fixture**: `tests/lanes/run.py` gains "registered but missing repo-relative tool", which appends a `tests/absent/tool.py` row. Red against a copy of `tools/check_layout.py` that skips the slash-row existence check: 9/10, the new fixture got exit 0. Green in `make check`: 10/10.
- **`make all`** exit 0. **`make check CONTRACT_CHECK_DISPLAYS=:190-:194`** exit 0: the runner self-test passes 20 of 20, and the harness record shows 52 PASS (sandbox 21, wire 19, xdriver 8, X3 self-test 4), one invocation each.

## Gate receipt against the patched v1 oracle

This is the single gate run this bead makes. The coordinator runs the real gate on a quiet box.

```sh
CPATH=/home/tobias/Projects/sublimite-v1-inspect/build/sysroot/usr/include \
make contract-gate ORACLE_TREE=/home/tobias/Projects/sublimite-v1-inspect \
                   ORACLE=/home/tobias/Projects/sublimite-v1-oracle/build/sublimite
```

The run was `v1-20261123T120606Z-231818`, record `build/contract-gate/v1-20261123T120606Z-231818/gate.json`. BAT0 was `Charging` [AC] and load went from 0.68 to 0.89. `build_oracle.py` validated the existing stamp without rebuilding. Identity matched `v1-pin` (pin `6fec2e8c…`, patch `54bcf05f…`, inspected `ed75336c…`, stock-in-tree `b8faeab5…`, drop-submit `5323f7ff…`, stock oracle `64c27b03…`). Runner exit 4 (ERROR), make exit 2.

| Invocation | Display | Invoked | Exit | Records | Wall (M) | Outcome |
|---|---|---|---|---|---|---|
| sandbox | none | 1 | 0 | 21 | 1.27 s | 21 PASS |
| wire | none | 1 | 0 | 19 | 0.27 s | 19 PASS |
| adapter | :190 | 1 | 0 | 9 | 2.32 s | 9 PASS (includes the stock disk/clipboard control) |
| navigation | :191 | 1 | 0 | 8 | 5.74 s | R1 4 PASS, R6 4 PASS (full 1,073,741,824 B corpus, seed `w0-r6-log-v1`) |
| files | :192 | 1 | 0 | 15 | 3.33 s | R3 6 PASS, R5 9 PASS |
| recovery | :193 | 1 | 0 | 4 | 2.27 s | R31 pre-ACK file/untitled PASS (pre-edit), post-ACK file/untitled PASS (whole edit) |
| large-undo | :194 | 1 | 3 | 10 | 10.06 s | 8 controls and `twin.roundtrip` PASS; `over.roundtrip` FAIL with signature `exit-1-error-105-destination-untouched`, which the manifest turns into **XFAIL** |
| capacity | none | 0 | none | 0 | none | case script missing: `tests/contract/cases/capacity.py`; `R4.restore-under-pressure` **MISSING** |

Totals: 85 PASS, 1 XFAIL, 1 MISSING. The gate fails, as intended, only because R4 (bead v2-he7.17) has not landed. Every other required assertion is at its expected outcome. Afterwards no Xvfb was left on `:190`–`:199`.

The measured wall times sit far below the `timeout_s` backstops, which are derived from the cases' own hang detectors (table above). A quiet, uncontended box is the condition the X3 decision showed matters: `ctl.delete` ran 0.6–49 s depending on contention.

## W0.7-B1 · 2026-11-23 · false-PASS fixes (bead v2-he7.27; review `docs/reviews/wave0-code-astra.md` findings 1, 2 runner half, 16, 26)

**Invariant, now enforced in `aggregate()`.** A run is PASS only if every assertion the manifest requires of the selected target has an outcome and each outcome is PASS or XFAIL with its reviewed signature. The check is positive: an outcome the runner does not know, a NOT-RUN without a failure, or a required ID without an entry is ERROR. It used to be "no ERROR, FAIL or XPASS kind present", which let NOT-RUN through.

**Outcomes (finding 1).** Observed records are judged first, then the missing IDs are classified. The manifest's assertion order is each fail-fast case's execution order. It is checked against `ASSERTIONS` in `navigation.py` and `files.py` and the X3 control sequence in `large_undo.py`. A record is *stopping* when its verdict is FAIL, ERROR or WRONG-SIGNATURE. A required ID without a record is NOT-RUN only when a stopping record **of its own case** comes earlier in that order. An unlisted harness ERROR (a `setup` or `assertion-inventory` step) stops its case at the point it was printed: after the case's records that precede it on stdout. Everything else without a record is MISSING (ERROR). Consequences:

- An accepted outcome (PASS, a matched XFAIL, XPASS) never excuses absent evidence. A case that stops on a reviewed XFAIL must therefore have that XFAIL as its last assertion. X3's `over.roundtrip` is.
- A missed control before the failure is MISSING, not NOT-RUN. A failure in R1 does not excuse missing R6 records: `files.py` and `navigation.py` run both cases of an invocation independently.
- Unittest self-tests do not stop at a failure. For them a missing ID after a FAIL is classified NOT-RUN (exit 3) where MISSING (exit 4) would be truer. Both are red. Only the exit code differs, never PASS.
- A record for an owned assertion that is not required of the selected target is rejected like an unowned one. Its ERROR is reported, never ignored.

Simplest alternative, rejected: drop NOT-RUN and make every missing ID MISSING. Counter-example: the fail-fast fixture (`test_fail_once_is_final`, `test_not_run_after_a_failure_keeps_the_failure`) and a real R1 FAIL would report exit 4. A genuine behavioural FAIL would then be indistinguishable from a broken harness.

**Parsing (finding 16).** `records.unique_object` is the one `object_pairs_hook`. It rejects a repeated key on every JSON stream the runner reads: the manifest, each RECORD line (`records.parse`) and the oracle build stamp. Records reconcile by the `(case_id, assertion_id)` pair. The joined `case.assertion` survives only as the gate record's display key, which the manifest already keeps unique. Problems spell the pair as `case / assertion`, and each gate entry carries both IDs. Dotted assertion IDs (`over.ctl.delete`, `Class.method`) stay legal. `case_id "A.x"` + `assertion_id "y"` no longer claims `A / x.y`.

**Digest interface (finding 2, runner half).** Each invocation's environment carries `CONTRACT_DIGESTS` (`records.DIGESTS_ENV`). It holds a JSON object `{absolute path: sha256 hex}` with one entry per executable in the **selected manifest target's identity**: each `executables.NAME` under the `--exe NAME=` path, and each `oracle_build.NAME` under `str(<--oracle-build>/NAME)`. Keys use exactly the spelling of the corresponding placeholder value (`{exe:NAME}`, or `{oracle_build}` joined with the stamp name). A case looks up the path it will execute: `records.expected_digests()[str(path)]`. That covers the adapter's `sublimite-stock` and `sublimite-drop-submit` too.

- The environment was chosen over an argument because it needs no manifest or case argv change and reaches every invocation the same way.
- The harness target binds `{}`.
- An outer value of the variable is always replaced, and a manifest `env` may not set it (or the dropped display variables).
- Outside the runner the variable is absent and `expected_digests()` raises KeyError. Phase 2 decides how a stand-alone diagnostic run, such as the X3 stub, identifies itself.
- The map is also written to `gate.json` as `digests`.

Before each launch the runner re-hashes every path in the map and refuses to launch if any differs (`executable changed since target selection`). That is ERROR, invocation count 0, and the case's IDs MISSING. A new matching stamp does not help, because the stamp is no longer consulted after selection. Identical replacement bytes still launch. This narrows the window to the launch and does not close it. Closing it stays case-side (phase 2): the sandbox checks the fd it executes against `CONTRACT_DIGESTS` instead of re-reading the stamp.

**Invocation count (finding 26).** `count` is no longer a counter. The run loop sets it as `int("command" in receipt)`, and `command` is set immediately before `Popen`. Rerun detection stays with the fixture's external counter.

**Red and green.** Both runs are on the same test file; the red one used the pre-bead `run.py` in a copy under `build/b1-old/`. Logs are under `build/receipts/` as `b1-red.log`, `b1-green.log` and `b1-mutant-rerun.log`.

| Finding | Test (`test_run.py`) | Red against the old runner | Green |
|---|---|---|---|
| 1 | `test_xfail_does_not_excuse_a_missing_control` (proof: XFAIL only, required control absent) | gate PASS exit 0, `A.ctl` NOT-RUN | exit 4, `A.ctl` MISSING |
| 1 | `test_complete_xfail_evidence_passes` | PASS (unchanged behaviour) | exit 0 |
| 1 | `test_a_failure_explains_only_later_work_of_its_case` | exit 3 | exit 4: MISSING before the FAIL, NOT-RUN after, other case MISSING |
| 1 | `test_record_of_an_assertion_not_required_of_the_target` (PASS and ERROR) | exit 0 | exit 4 |
| 1 | `test_green_needs_every_required_id_pass_or_matched_xfail` | TypeError (no `required`); with the old rule NOT-RUN alone was PASS | ERROR for NOT-RUN, unknown outcome, absent ID |
| 1 | `test_fail_once_is_final`, `test_not_run_after_a_failure_keeps_the_failure` | exit 3 kept | exit 3 |
| 16 | `test_duplicate_key_inside_a_record` (FAIL then PASS) | exit 0 | exit 4, `A.a` MISSING |
| 16 | `test_ids_reconcile_as_a_pair` | exit 0 | exit 4, `A.x / y` foreign |
| 16 | `test_dotted_assertion_ids_stay_legal` | no pair in the entry | exit 0 |
| 16 | `test_duplicate_key_in_the_build_stamp` (repeated equal `pin`) | exit 0 | exit 4, zero invocations |
| 2 | `test_expected_digests_reach_every_invocation` (two invocations, outer value replaced) | no binding | exit 0, both carry the selected map |
| 2 | `test_replaced_executable_is_an_error_before_launch` (proof: replaced by B with a matching stamp; identical bytes) | exit 0, second launched | exit 4, second not launched, count 0; identical bytes: exit 0 |
| 2 | `test_reserved_environment_in_the_manifest` | accepted | exit 4, zero invocations |
| 26 | `test_fail_once_is_final` against a mutant runner that relaunches a failing invocation once (`build/b1-mutant/`) | FAIL `4 != 3` | PASS on the real runner; receipt count 1 = external counter |

Old runner: 19 of 32 PASS, exit 4. Two of the red rows are only the new pair spelling in two existing tests' expected text. New runner: 32 of 32 PASS, exit 0. Battery `Charging` [AC], load 8.6. Timing was not measured here.

**Not done here (outside this bead's files):** case-side phase 2 of finding 2, meaning `large_undo.py`, `navigation.py`, `files.py`, `recovery.py` and the adapter stop re-reading stamps and check `CONTRACT_DIGESTS`. Records are also not required to appear in manifest order; the order is assumed from the case sources, not enforced.
