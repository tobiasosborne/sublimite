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
                 identity=None, displays=0, timeout=TIMEOUT_S, script=FIXTURE, raw=None):
        plan = self.dir / "plan.json"
        plan.write_text(json.dumps({"counter": str(self.counter), "steps": steps}))
        argv = [script, "--plan", str(plan)] + (["--display", "{display}"] if displays else [])
        manifest = {
            "format": 1,
            "targets": {"fixture": {"role": role, "identity": identity or {}}},
            "invocations": {"fixture": {"argv": argv, "displays": displays, "timeout_s": timeout,
                                        "timeout_derivation": "(G) fixture"}},
            "cases": {"A": {"invocation": "fixture", "evidence": "A", "first_required_wave": 2,
                            "expect": {"fixture": "PASS"},
                            "assertions": {name: ({"expect": {"fixture": expect}} if expect else {})
                                           for name in assertions}}},
        }
        path = self.dir / "expectations.json"
        path.write_text(raw if raw is not None else json.dumps(manifest, indent=1))
        return path

    def run_gate(self, manifest, *args, role="harness", displays="none"):
        result = subprocess.run(
            [sys.executable, "-B", str(RUN), "--manifest", str(manifest), "--role", role,
             "--displays", displays, "--out", str(self.out), *args],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
        runs = sorted(self.out.glob("*/gate.json")) if self.out.is_dir() else []
        self.assertEqual(len(runs), 1, f"one gate record expected\n{result.stdout}")
        gate = json.loads(runs[0].read_text())
        self.assertEqual(gate["exit"], result.returncode, result.stdout)
        self.assertTrue((runs[0].parent / "summary.txt").is_file())
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
        self.assertIn("duplicate record A.a", " ".join(gate["invocations"]["fixture"]["problems"]))

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
        self.assertEqual(self.outcome(gate, "A.a"), "MISSING")

    def test_fail_once_is_final(self):
        fail = [record("a", "FAIL"), record("b", "PASS")]
        green = [record("a", "PASS"), record("b", "PASS")]
        path = self.manifest([{"records": fail, "exit": 3}, {"records": green, "exit": 0}])
        code, gate, out = self.run_gate(path)
        self.assertEqual(code, records.EXIT["FAIL"], out)
        self.assertEqual(self.invocations(), 1)
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
        self.assertIn("A.setup", " ".join(gate["invocations"]["fixture"]["problems"]))

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


class ManifestTests(unittest.TestCase):
    def test_checked_in_manifest_is_valid(self):
        result = subprocess.run([sys.executable, "-B", str(RUN), "--validate"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                check=False)
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    sys.exit(records.unittest_main("RUNNER"))
