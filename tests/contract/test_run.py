"""Self-test for the contract runner tests/contract/run.py (no editor, no display).

Each test writes a fixture manifest and plan under build/, runs the runner once as a child
process and reads its exit status, gate record and the fixture's invocation counter.
"""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import records  # noqa: E402

ROOT = HERE.parents[1]
BUILD = ROOT / "build"
RUN = HERE / "run.py"
FIXTURE = "tests/contract/runner_fixture.py"
SIGNATURE = "exit-1-error-105-destination-untouched"
WITNESS = "docs/decisions/v2-he7.15.md: red run 20261123T000000Z"
# (G) fixture only: the runner's watchdog for fixture invocations that do not hang, and
# the deliberately short one the hang test expects to expire.
TIMEOUT_S = 60
HANG_TIMEOUT_S = 1


def record(assertion, status, case="A", **extra):
    return {"case_id": case, "assertion_id": assertion, "status": status, "evidence": "A",
            "first_required_wave": 2, "predicate": f"{assertion} {status}", **extra}


class RunnerTests(unittest.TestCase):
    def setUp(self):
        BUILD.mkdir(exist_ok=True)
        self.dir = Path(tempfile.mkdtemp(prefix="runner-test-", dir=BUILD))
        self.counter = self.dir / "invocations"
        self.out = self.dir / "out"

    def manifest(self, steps, *, assertions=("a", "b"), expect=None, role="harness",
                 identity=None, displays=0, timeout=TIMEOUT_S, script=FIXTURE, raw=None,
                 cases=None, overrides=None, targets=None, digests=False):
        """`cases` maps case ID -> (invocation, assertion IDs); every invocation plays the same
        plan, so step n is the n-th launch overall. `overrides` maps "case.assertion" to its
        manifest override object; `targets` adds targets beside "fixture"."""
        plan = self.dir / "plan.json"
        plan.write_text(json.dumps({"counter": str(self.counter), "steps": steps}))
        argv = [script, "--plan", str(plan)] + (["--display", "{display}"] if displays else [])
        argv += ["--digests", "{digests}"] if digests else []
        cases = cases or {"A": ("fixture", assertions)}
        overrides = overrides or {}

        def override(case_id, name):
            if f"{case_id}.{name}" in overrides:
                return overrides[f"{case_id}.{name}"]
            return {"expect": {"fixture": expect}} if expect else {}
        manifest = {
            "format": 1,
            "targets": {"fixture": {"role": role, "identity": identity or {}}, **(targets or {})},
            "invocations": {name: {"argv": argv, "displays": displays, "timeout_s": timeout,
                                   "timeout_derivation": "(G) fixture"}
                            for name in dict.fromkeys(inv for inv, _ in cases.values())},
            "cases": {case_id: {"invocation": inv, "evidence": "A", "first_required_wave": 2,
                                "expect": {"fixture": "PASS"},
                                "assertions": {name: override(case_id, name) for name in names}}
                      for case_id, (inv, names) in cases.items()},
        }
        path = self.dir / "expectations.json"
        path.write_text(raw if raw is not None else json.dumps(manifest, indent=1))
        return path

    def run_gate(self, manifest, *args, role="harness", displays="none", env=None):
        result = subprocess.run(
            [sys.executable, "-B", str(RUN), "--manifest", str(manifest), "--role", role,
             "--displays", displays, "--out", str(self.out), *args],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False,
            env=env)
        runs = sorted(self.out.glob("*/gate.json")) if self.out.is_dir() else []
        self.assertEqual(len(runs), 1, f"one gate record expected\n{result.stdout}")
        gate = json.loads(runs[0].read_text())
        self.assertEqual(gate["exit"], result.returncode, result.stdout)
        self.assertTrue((runs[0].parent / "summary.txt").is_file())
        if gate["status"] == "PASS":
            # The green invariant, checked on every green fixture run.
            self.assertTrue(gate["assertions"], result.stdout)
            for full, entry in gate["assertions"].items():
                self.assertIn(entry["outcome"], ("PASS", "XFAIL"), f"{full}\n{result.stdout}")
        return result.returncode, gate, result.stdout

    def invocations(self):
        return len(self.counter.read_text().splitlines()) if self.counter.exists() else 0

    def outcome(self, gate, assertion):
        return gate["assertions"][assertion]["outcome"]

    # ---- green baseline ----

    def test_all_pass(self):
        code, gate, out = self.run_gate(self.manifest(
            [{"records": [record("a", "PASS"), record("b", "PASS")], "exit": 0}]))
        self.assertEqual(code, records.EXIT["PASS"], out)
        self.assertEqual(gate["status"], "PASS")
        self.assertEqual([self.outcome(gate, "A.a"), self.outcome(gate, "A.b")], ["PASS", "PASS"])
        self.assertEqual(self.invocations(), 1)

    def test_reviewed_xfail_with_exact_signature_passes_the_run(self):
        expect = {"status": "XFAIL", "signature": SIGNATURE, "witness": WITNESS}
        code, gate, out = self.run_gate(self.manifest(
            [{"records": [record("a", "FAIL", signature=SIGNATURE)], "exit": 3}],
            assertions=("a",), expect=expect))
        self.assertEqual(code, records.EXIT["PASS"], out)
        self.assertEqual(self.outcome(gate, "A.a"), "XFAIL")

    # ---- the bead's red list ----

    def test_missing_required_id(self):
        code, gate, out = self.run_gate(self.manifest([{"records": [record("a", "PASS")], "exit": 0}]))
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertEqual(self.outcome(gate, "A.b"), "MISSING")

    def test_duplicate_id_in_manifest(self):
        path = self.manifest([{"records": [], "exit": 0}])
        text = path.read_text().replace('"a": {}', '"a": {}, "a": {}', 1)
        self.assertIn('"a": {}, "a": {}', text)
        code, gate, out = self.run_gate(self.manifest([], raw=text))
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertIn("duplicate key", " ".join(gate["problems"]))
        self.assertEqual(self.invocations(), 0)

    def test_duplicate_id_in_records(self):
        code, gate, out = self.run_gate(self.manifest(
            [{"records": [record("a", "PASS"), record("a", "PASS"), record("b", "PASS")], "exit": 0}]))
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertIn("duplicate record A / a", " ".join(gate["invocations"]["fixture"]["problems"]))

    def test_ordinary_fail_mislabelled_xfail(self):
        expect = {"status": "XFAIL", "signature": SIGNATURE, "witness": WITNESS}
        for name, extra in (("no signature", {}), ("other signature", {"signature": "exit-2"})):
            with self.subTest(name):
                self.counter.unlink(missing_ok=True)
                self.out = self.dir / f"out-{len(extra)}"
                code, gate, out = self.run_gate(self.manifest(
                    [{"records": [record("a", "FAIL", **extra)], "exit": 3}],
                    assertions=("a",), expect=expect))
                self.assertEqual(code, records.EXIT["ERROR"], out)
                self.assertEqual(self.outcome(gate, "A.a"), "WRONG-SIGNATURE")

    def test_wrong_target_role(self):
        stub = self.dir / "stub"
        stub.write_bytes(b"stub editor")
        identity = {"executables": {"stub": hashlib.sha256(b"stub editor").hexdigest()}}
        path = self.manifest([{"records": [record("a", "PASS"), record("b", "PASS")], "exit": 0}],
                             role="v1", identity=identity)
        code, gate, out = self.run_gate(path, "--exe", f"stub={stub}", role="v2")
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertIn("role v1", " ".join(gate["problems"]))
        self.assertEqual(self.invocations(), 0)
        # The same identity under its reviewed role runs.
        self.out = self.dir / "out-v1"
        code, gate, out = self.run_gate(path, "--exe", f"stub={stub}", role="v1")
        self.assertEqual(code, records.EXIT["PASS"], out)

    def test_unreviewed_identity(self):
        stub = self.dir / "stub"
        stub.write_bytes(b"rebuilt stub editor")
        identity = {"executables": {"stub": hashlib.sha256(b"stub editor").hexdigest()}}
        path = self.manifest([{"records": [], "exit": 0}], role="v1", identity=identity)
        code, gate, out = self.run_gate(path, "--exe", f"stub={stub}", role="v1")
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertIn("no reviewed target", " ".join(gate["problems"]))
        self.assertEqual(self.invocations(), 0)

    def test_xfail_without_exact_signature_in_manifest(self):
        for expect in ({"status": "XFAIL", "witness": WITNESS},
                       {"status": "XFAIL", "signature": "", "witness": WITNESS},
                       {"status": "XFAIL", "signature": SIGNATURE}):
            with self.subTest(expect=expect):
                self.out = self.dir / f"out-{len(json.dumps(expect))}"
                code, gate, out = self.run_gate(self.manifest(
                    [{"records": [record("a", "FAIL", signature=SIGNATURE)], "exit": 3}],
                    assertions=("a",), expect=expect))
                self.assertEqual(code, records.EXIT["ERROR"], out)
                self.assertIn("XFAIL", " ".join(gate["problems"]))
                self.assertEqual(self.invocations(), 0)

    def test_xpass_fails_the_run(self):
        expect = {"status": "XFAIL", "signature": SIGNATURE, "witness": WITNESS}
        code, gate, out = self.run_gate(self.manifest(
            [{"records": [record("a", "PASS")], "exit": 0}], assertions=("a",), expect=expect))
        self.assertEqual(code, records.EXIT["XPASS"], out)
        self.assertEqual(self.outcome(gate, "A.a"), "XPASS")

    def test_missing_display(self):
        code, gate, out = self.run_gate(self.manifest(
            [{"records": [record("a", "PASS"), record("b", "PASS")], "exit": 0}], displays=1),
            displays="none")
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertIn("no free display", " ".join(gate["invocations"]["fixture"]["problems"]))
        self.assertEqual(self.invocations(), 0)
        self.assertEqual(gate["invocations"]["fixture"]["count"], 0)
        self.assertEqual(self.outcome(gate, "A.a"), "MISSING")

    def test_fail_once_is_final(self):
        fail = [record("a", "FAIL"), record("b", "PASS")]
        green = [record("a", "PASS"), record("b", "PASS")]
        path = self.manifest([{"records": fail, "exit": 3}, {"records": green, "exit": 0}])
        code, gate, out = self.run_gate(path)
        self.assertEqual(code, records.EXIT["FAIL"], out)
        self.assertEqual(self.invocations(), 1)
        self.assertEqual(gate["invocations"]["fixture"]["count"], 1)
        transcript = Path(gate["invocations"]["fixture"]["stdout"]).read_text()
        self.assertIn('"status": "FAIL"', transcript)
        self.assertEqual(self.outcome(gate, "A.a"), "FAIL")
        # The fixture is a genuine fail-once: a second invocation would have passed.
        second = subprocess.run([sys.executable, "-B", str(ROOT / FIXTURE), "--plan",
                                 str(self.dir / "plan.json")], stdout=subprocess.PIPE, check=False)
        self.assertEqual(second.returncode, 0)
        self.assertEqual(self.invocations(), 2)

    # ---- the remaining abort conditions ----

    def test_zero_collected(self):
        code, gate, out = self.run_gate(self.manifest([{"records": [], "exit": 0}]))
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertIn("zero records", " ".join(gate["invocations"]["fixture"]["problems"]))

    def test_abnormal_and_inconsistent_exit(self):
        both = [record("a", "PASS"), record("b", "PASS")]
        for status, steps in (("uncaught exception", [{"records": both, "exit": 1}]),
                              ("PASS records, FAIL exit", [{"records": both, "exit": 3}]),
                              ("FAIL record, exit 0",
                               [{"records": [record("a", "FAIL"), record("b", "PASS")], "exit": 0}])):
            with self.subTest(status):
                self.out = self.dir / f"out-{len(status)}"
                code, gate, out = self.run_gate(self.manifest(steps))
                self.assertEqual(code, records.EXIT["ERROR"], out)
                self.assertIn("exit", " ".join(gate["invocations"]["fixture"]["problems"]))

    def test_malformed_records(self):
        good = record("b", "PASS")
        for name, step in (
                ("not JSON", {"lines": [records.PREFIX + "{"], "records": [good], "exit": 0}),
                ("wave as string", {"records": [record("a", "PASS", first_required_wave="W2"), good],
                                    "exit": 0}),
                ("runner verdict from a case", {"records": [record("a", "XFAIL"), good], "exit": 0}),
                ("evidence differs from manifest", {"records": [{**record("a", "PASS"),
                                                                 "evidence": "B"}, good], "exit": 0}),
                ("unknown id that passed", {"records": [record("a", "PASS"), good,
                                                        record("z", "PASS")], "exit": 0})):
            with self.subTest(name):
                self.out = self.dir / f"out-{len(name)}"
                code, gate, out = self.run_gate(self.manifest([step]))
                self.assertEqual(code, records.EXIT["ERROR"], out)
                self.assertTrue(gate["invocations"]["fixture"]["problems"], out)

    def test_harness_error_record_on_an_unlisted_step(self):
        code, gate, out = self.run_gate(self.manifest(
            [{"records": [record("setup", "ERROR")], "exit": 4}]))
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertEqual(self.outcome(gate, "A.a"), "NOT-RUN")
        self.assertIn("A / setup", " ".join(gate["invocations"]["fixture"]["problems"]))

    def test_not_run_after_a_failure_keeps_the_failure(self):
        code, gate, out = self.run_gate(self.manifest([{"records": [record("a", "FAIL")], "exit": 3}]))
        self.assertEqual(code, records.EXIT["FAIL"], out)
        self.assertEqual([self.outcome(gate, "A.a"), self.outcome(gate, "A.b")], ["FAIL", "NOT-RUN"])

    def test_timeout(self):
        code, gate, out = self.run_gate(self.manifest([{"records": [], "exit": 0, "hang": True}],
                                                      timeout=HANG_TIMEOUT_S))
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertIn("timeout", " ".join(gate["invocations"]["fixture"]["problems"]))
        self.assertEqual(self.invocations(), 1)

    def test_missing_case_script(self):
        code, gate, out = self.run_gate(self.manifest([], script="tests/contract/cases/absent.py"))
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertIn("script missing", " ".join(gate["invocations"]["fixture"]["problems"]))
        self.assertEqual([self.outcome(gate, "A.a"), self.outcome(gate, "A.b")], ["MISSING", "MISSING"])


    # ---- W0.7-B1: review findings 1, 2 (runner half), 16 and 26 ----

    def xfail_after_control(self):
        expect = {"status": "XFAIL", "signature": SIGNATURE, "witness": WITNESS}
        return {"assertions": ("ctl", "a"), "overrides": {"A.a": {"expect": {"fixture": expect}}}}

    def test_xfail_does_not_excuse_a_missing_control(self):
        # Finding 1: only the reviewed X3-style failure, every control absent.
        code, gate, out = self.run_gate(self.manifest(
            [{"records": [record("a", "FAIL", signature=SIGNATURE)], "exit": 3}],
            **self.xfail_after_control()))
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertEqual(self.outcome(gate, "A.ctl"), "MISSING")
        self.assertEqual(self.outcome(gate, "A.a"), "XFAIL")

    def test_complete_xfail_evidence_passes(self):
        code, gate, out = self.run_gate(self.manifest(
            [{"records": [record("ctl", "PASS"), record("a", "FAIL", signature=SIGNATURE)],
              "exit": 3}], **self.xfail_after_control()))
        self.assertEqual(code, records.EXIT["PASS"], out)
        self.assertEqual([self.outcome(gate, "A.ctl"), self.outcome(gate, "A.a")], ["PASS", "XFAIL"])

    def test_a_failure_explains_only_later_work_of_its_case(self):
        # Finding 1: a missed step before the failure, and another case of the same invocation.
        path = self.manifest([{"records": [record("b", "FAIL")], "exit": 3}],
                             cases={"A": ("fixture", ("a", "b", "c")), "B": ("fixture", ("d",))})
        code, gate, out = self.run_gate(path)
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertEqual([self.outcome(gate, f"A.{name}") for name in "abc"],
                         ["MISSING", "FAIL", "NOT-RUN"])
        self.assertEqual(self.outcome(gate, "B.d"), "MISSING")

    def test_record_of_an_assertion_not_required_of_the_target(self):
        # Finding 1: an owned case whose manifest row names only another target.
        for status in ("PASS", "ERROR"):
            with self.subTest(status):
                self.out = self.dir / f"out-{status}"
                steps = [{"records": [record("a", "PASS"), record("b", "PASS"),
                                      record("z", status, case="Z")],
                          "exit": records.exit_status(["PASS", status])}]
                path = self.manifest(steps, cases={"A": ("fixture", ("a", "b")),
                                                   "Z": ("fixture", ("z",))},
                                     targets={"other": {"role": "v2", "identity": None}})
                manifest = json.loads(path.read_text())
                manifest["cases"]["Z"]["expect"] = {"other": "PASS"}
                code, gate, out = self.run_gate(self.manifest(steps, raw=json.dumps(manifest)))
                self.assertEqual(code, records.EXIT["ERROR"], out)
                self.assertNotIn("Z.z", gate["assertions"])
                self.assertIn("Z / z", " ".join(gate["invocations"]["fixture"]["problems"]))

    def test_green_needs_every_required_id_pass_or_matched_xfail(self):
        # Finding 1: the final invariant, enforced in aggregate() itself.
        import run  # noqa: E402 (the runner module, imported for its pure verdict function)
        accepted = {"A.a": {"outcome": "PASS"}, "A.b": {"outcome": "XFAIL"}}
        self.assertEqual(run.aggregate([], [], accepted, ["A.a", "A.b"]), "PASS")
        for outcomes, required in (({"A.a": {"outcome": "NOT-RUN"}}, ["A.a"]),
                                   ({"A.a": {"outcome": "UNKNOWN"}}, ["A.a"]),
                                   ({"A.a": {"outcome": "PASS"}}, ["A.a", "A.b"])):
            with self.subTest(outcomes=outcomes, required=required):
                self.assertEqual(run.aggregate([], [], outcomes, required), "ERROR")

    def test_duplicate_key_inside_a_record(self):
        # Finding 16: FAIL then PASS as duplicate status fields.
        line = json.dumps(record("a", "FAIL"))[:-1] + ', "status": "PASS"}'
        self.assertEqual(json.loads(line)["status"], "PASS")
        code, gate, out = self.run_gate(self.manifest(
            [{"lines": [records.PREFIX + line], "records": [record("b", "PASS")], "exit": 0}]))
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertIn("duplicate key", " ".join(gate["invocations"]["fixture"]["problems"]))
        self.assertEqual(self.outcome(gate, "A.a"), "MISSING")

    def test_ids_reconcile_as_a_pair(self):
        # Finding 16: case_id "A.x" + assertion_id "y" must not claim the slot A / x.y.
        code, gate, out = self.run_gate(self.manifest(
            [{"records": [record("y", "PASS", case="A.x"), record("b", "PASS")], "exit": 0}],
            assertions=("x.y", "b")))
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertEqual(self.outcome(gate, "A.x.y"), "MISSING")
        self.assertIn("A.x / y", " ".join(gate["invocations"]["fixture"]["problems"]))

    def test_dotted_assertion_ids_stay_legal(self):
        code, gate, out = self.run_gate(self.manifest(
            [{"records": [record("x.y", "PASS"), record("Tests.test_b", "PASS")], "exit": 0}],
            assertions=("x.y", "Tests.test_b")))
        self.assertEqual(code, records.EXIT["PASS"], out)
        self.assertEqual(gate["assertions"]["A.x.y"]["assertion_id"], "x.y")

    def oracle_build(self, editor=b"inspected editor"):
        """A stand-in inspected build directory: one executable and its stamp."""
        build = self.dir / "oracle-build"
        build.mkdir(exist_ok=True)
        (build / "sublimite").write_bytes(editor)
        stamp = {"pin": "0" * 40, "patch_sha256": "1" * 64,
                 "executables": {"sublimite": hashlib.sha256(editor).hexdigest()}}
        (build / "manifest.json").write_text(json.dumps(stamp))
        identity = {"executables": {"stub": hashlib.sha256(b"stub editor").hexdigest()},
                    "pin": stamp["pin"], "patch_sha256": stamp["patch_sha256"],
                    "oracle_build": stamp["executables"]}
        stub = self.dir / "stub"
        stub.write_bytes(b"stub editor")
        return build, stub, identity

    def test_expected_digests_reach_every_invocation(self):
        # Finding 2: the selected target's digests, keyed by executed path, travel in the
        # invocation's own argv ({digests}); no inherited variable carries a binding.
        build, stub, identity = self.oracle_build()
        steps = [{"records": [record("a", "PASS")], "exit": 0, "digests": True},
                 {"records": [record("a", "PASS", case="B")], "exit": 0, "digests": True}]
        path = self.manifest(steps, cases={"A": ("first", ("a",)), "B": ("second", ("a",))},
                             role="v1", identity=identity, digests=True)
        outer = dict(os.environ, CONTRACT_DIGESTS=json.dumps({str(stub): "0" * 64}))
        code, gate, out = self.run_gate(path, "--exe", f"stub={stub}", "--oracle-build", str(build),
                                        role="v1", env=outer)
        self.assertEqual(code, records.EXIT["PASS"], out)
        expected = {str(stub): identity["executables"]["stub"],
                    str(build / "sublimite"): identity["oracle_build"]["sublimite"]}
        self.assertEqual(gate["digests"], expected)
        for name in ("first", "second"):
            self.assertEqual(gate["invocations"][name]["records"][0]["digests"], expected)
        # A stand-alone diagnostic run takes the same binding from the same selection.
        printed = subprocess.run(
            [sys.executable, "-B", str(RUN), "--manifest", str(path), "--role", "v1", "--binding",
             "--exe", f"stub={stub}", "--oracle-build", str(build)],
            capture_output=True, text=True, check=False)
        self.assertEqual((printed.returncode, records.binding(printed.stdout)), (0, expected),
                         printed.stderr)

    def test_count_predicate_per_target(self):
        # Advisor ruling (R6/R1): the manifest names the count predicate per target; "zero"
        # asserts allocations == 0, "recorded" only requires a valid count in the record.
        def gate_for(count, law2, status="PASS"):
            self.out = self.dir / f"out-{count}-{json.dumps(law2)}-{status}"
            extra = {} if law2 is None else {"law2": law2}
            path = self.manifest([{"records": [record("a", status, **extra)],
                                   "exit": records.EXIT[status]}], assertions=("a",),
                                 overrides={"A.a": {"count": {"fixture": count}}})
            return self.run_gate(path)
        for count, law2, want in (("zero", {"allocations": 0, "scopes": 3}, "PASS"),
                                  ("zero", {"allocations": 1, "scopes": 3}, "FAIL"),
                                  ("recorded", {"allocations": 4, "scopes": 3}, "PASS"),
                                  ("recorded", None, "ERROR"),
                                  ("recorded", {"allocations": 4, "scopes": 0}, "ERROR"),
                                  ("zero", {"allocations": -1, "scopes": 3}, "ERROR"),
                                  ("sometimes", {"allocations": 0, "scopes": 3}, "ERROR")):
            with self.subTest(count=count, law2=law2):
                code, gate, out = gate_for(count, law2)
                self.assertEqual(code, records.EXIT[want], out)
                if count != "sometimes":
                    self.assertEqual(gate["assertions"]["A.a"]["count"],
                                     {"predicate": count, "observed": law2})
        with self.subTest("checked-in manifest: R1, R6 and R4 assert zero on v2 only"):
            manifest = json.loads((HERE / "expectations.json").read_text())
            for case in ("R1", "R6", "R4"):
                self.assertEqual(manifest["cases"][case]["assertions"]["law2-count"]["count"],
                                 {"v1-pin": "recorded", "v2": "zero"})

    def test_replaced_executable_is_an_error_before_launch(self):
        # Finding 2 proof: select A, replace it by B with a matching new stamp before a launch.
        for name, editor in (("rebuilt", b"rebuilt editor"), ("identical", b"inspected editor")):
            with self.subTest(name):
                self.counter.unlink(missing_ok=True)
                self.out = self.dir / f"out-{name}"
                build, stub, identity = self.oracle_build()
                stamp = {"pin": identity["pin"], "patch_sha256": identity["patch_sha256"],
                         "executables": {"sublimite": hashlib.sha256(editor).hexdigest()}}
                write = {str(build / "sublimite"): editor.decode(),
                         str(build / "manifest.json"): json.dumps(stamp)}
                steps = [{"records": [record("a", "PASS")], "exit": 0, "write": write},
                         {"records": [record("a", "PASS", case="B")], "exit": 0}]
                path = self.manifest(steps, cases={"A": ("first", ("a",)), "B": ("second", ("a",))},
                                     role="v1", identity=identity)
                code, gate, out = self.run_gate(path, "--exe", f"stub={stub}",
                                                "--oracle-build", str(build), role="v1")
                second = gate["invocations"]["second"]
                if name == "identical":
                    self.assertEqual(code, records.EXIT["PASS"], out)
                    self.assertEqual(self.invocations(), 2)
                    continue
                self.assertEqual(code, records.EXIT["ERROR"], out)
                self.assertEqual(self.invocations(), 1)
                self.assertEqual(second["count"], 0)
                self.assertIn(str(build / "sublimite"), " ".join(second["problems"]))
                self.assertEqual(self.outcome(gate, "B.a"), "MISSING")

    def test_duplicate_key_in_the_build_stamp(self):
        # Finding 16 for the third JSON input the runner reads.
        build, stub, identity = self.oracle_build()
        text = (build / "manifest.json").read_text()
        # The repeated key even agrees with the first: last-wins parsing would accept it.
        (build / "manifest.json").write_text(text[:-1] + ', "pin": "' + identity["pin"] + '"}')
        path = self.manifest([{"records": [record("a", "PASS")], "exit": 0}], assertions=("a",),
                             role="v1", identity=identity)
        code, gate, out = self.run_gate(path, "--exe", f"stub={stub}", "--oracle-build", str(build),
                                        role="v1")
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertIn("duplicate key", " ".join(gate["problems"]))
        self.assertEqual(self.invocations(), 0)

    def test_reserved_environment_in_the_manifest(self):
        path = self.manifest([{"records": [], "exit": 0}])
        manifest = json.loads(path.read_text())
        manifest["invocations"]["fixture"]["env"] = {"DISPLAY": ":0"}
        code, gate, out = self.run_gate(self.manifest([], raw=json.dumps(manifest)))
        self.assertEqual(code, records.EXIT["ERROR"], out)
        self.assertIn("DISPLAY", " ".join(gate["problems"]))
        self.assertEqual(self.invocations(), 0)


def red_record(case, assertion, status, **extra):
    return {"case_id": case, "assertion_id": assertion, "status": status, "evidence": "B",
            "first_required_wave": 2, "predicate": assertion, **extra}


class RedTests(unittest.TestCase):
    """Finding 17: tests/contract/reds.py credits a red only for its own cause."""

    def setUp(self):
        import reds
        self.reds = reds
        self.manifest = json.loads((HERE / "expectations.json").read_text())

    def problems(self, name, *found):
        return self.reds.check(self.reds.REDS[name],
                               [records.PREFIX + json.dumps(r) for r in found], self.manifest)

    def swap(self, **changes):
        fields = {"predicate": "minimal reveal and neighbouring word fragments",
                  "observed": ["row 1"], "expected": ["row 2"],
                  "ordinary": {"expected": ["row 1"], "observed": ["row 1"]}}
        fields.update(changes)
        return red_record("R1", "wrapped-return", "FAIL", **fields)

    def test_the_deliberate_red_qualifies(self):
        self.assertEqual(self.problems("R1.wrapped-return",
                                       red_record("R1", "bottom-setup", "PASS"), self.swap()), [])
        timeout = red_record("R3", "save-b-bytes", "ERROR", step="save-b-signal",
                             predicate="TimeoutError: no 0x80 file event for b.c before deadline")
        self.assertEqual(self.problems("R3.save-signal",
                                       red_record("R3", "results-between-a-and-b", "PASS"),
                                       timeout), [])
        recovered = red_record("R31", "post-ack-file", "FAIL", step="recovered-buffer",
                               predicate="recovered-buffer: expected (b'x',), got b'y'",
                               observed_hex="79", expected_hex=["78"],
                               ordinary={"expected": "79", "observed": "79"})
        self.assertEqual(self.problems("R31.post-ack-file", recovered), [])

    def test_an_earlier_setup_error_does_not_qualify(self):
        found = self.problems("R1.wrapped-return",
                              red_record("R1", "setup", "ERROR", predicate="Refused: occupied"))
        self.assertTrue(any("first non-PASS record is setup ERROR" in p for p in found), found)

    def test_a_different_mismatch_at_the_same_id_does_not_qualify(self):
        setup = red_record("R1", "bottom-setup", "PASS")
        for name, red in (
                ("another comparison of the group", self.swap(predicate="click preserves setup viewport",
                                                              ordinary=None)),
                ("the healthy observation was not shown", self.swap(ordinary=None)),
                ("an unhealthy observation", self.swap(ordinary={"expected": ["row 1"],
                                                                 "observed": ["row 3"]})),
                ("a contrary expectation equal to the observation", self.swap(expected=["row 1"]))):
            with self.subTest(name):
                self.assertNotEqual(self.problems("R1.wrapped-return", setup, red), [])
        exited = red_record("R3", "save-b-bytes", "ERROR", step="save-b-signal",
                            predicate="Refused: editor exited while waiting for b.c: 1")
        with self.subTest("an editor exit in place of the withheld save's timeout"):
            self.assertNotEqual(self.problems("R3.save-signal",
                                              red_record("R3", "results-between-a-and-b", "PASS"),
                                              exited), [])

    def test_preceding_controls_and_first_failure_final(self):
        found = self.problems("R1.wrapped-return", self.swap())
        self.assertTrue(any("preceding controls" in p for p in found), found)
        found = self.problems("R1.wrapped-return", red_record("R1", "bottom-setup", "PASS"),
                              self.swap(), red_record("R1", "teardown", "ERROR"))
        self.assertTrue(any("records after the red" in p for p in found), found)
        found = self.problems("R1.wrapped-return", red_record("R1", "bottom-setup", "PASS"))
        self.assertEqual(found, ["no non-PASS record: the control did not fail"])


class ManifestTests(unittest.TestCase):
    def test_checked_in_manifest_is_valid(self):
        result = subprocess.run([sys.executable, "-B", str(RUN), "--validate"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                check=False)
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    sys.exit(records.unittest_main("RUNNER"))
