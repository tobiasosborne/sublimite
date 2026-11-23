"""Self-test for tests/contract/cases/large_undo.py.

ClassifyTests: only the exact A8 signature on the pinned digest is XFAIL; every neighbour of it
reports as itself. StubControlTest: a stub "editor" that saves wrong bytes makes the first
control fail, and the run must report ERROR (exit 2), never PASS or XFAIL.

Usage: python3 -B tests/contract/test_large_undo.py --display :14N   (needs build/xdriver and
build/xdriver-test-client; the stub run owns Xvfb on that display like the case does).
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "cases"))
import large_undo as case  # noqa: E402

BUILD = case.BUILD
CLIENT = BUILD / "xdriver-test-client"
DISPLAY = None
PIN = case.V1_PIN_SHA256
OTHER = "0" * 64
ORIGINAL = {"bytes": 4, "sha256": hashlib.sha256(b"text").hexdigest()}
EMPTY = {"bytes": 0, "sha256": hashlib.sha256(b"").hexdigest()}
UNTOUCHED = {"st_ino": 7, "st_ctime_ns": 1, "sha256": ORIGINAL["sha256"]}


def exited(status=1, stderr="sublimite: error -105; journal: /x\n", after=UNTOUCHED):
    return {"kind": "exited", "exit": status, "stderr": stderr, "before": UNTOUCHED, "after": after}


def saved(output, quit_exit=0):
    return {"kind": "saved", "output": output, "quit_exit": quit_exit}


def verdict(outcome, sha=PIN, assertion="X3.over.roundtrip", input_after=ORIGINAL):
    return case.classify(assertion, sha, ORIGINAL, outcome, input_after)[0]


class ClassifyTests(unittest.TestCase):
    def test_exact_signature_on_pin_is_xfail(self):
        self.assertEqual(verdict(exited()), "XFAIL")

    def test_neighbours_of_the_signature_are_not_xfail(self):
        cases = {
            "other status": (exited(status=2), "ERROR"),
            "signal": (exited(status=-9), "ERROR"),
            "other error": (exited(stderr="sublimite: error -104; journal: /x\n"), "ERROR"),
            "no stderr": (exited(stderr=""), "ERROR"),
            "destination rewritten": (exited(after={**UNTOUCHED, "st_ino": 8}), "ERROR"),
            "destination ctime": (exited(after={**UNTOUCHED, "st_ctime_ns": 2}), "ERROR"),
            "0 B save": (saved(EMPTY), "FAIL"),
            "round trip": (saved(ORIGINAL), "XPASS"),
            "round trip, bad quit": (saved(ORIGINAL, quit_exit=1), "ERROR"),
        }
        for name, (outcome, expected) in cases.items():
            with self.subTest(name):
                self.assertEqual(verdict(outcome), expected)

    def test_signature_is_bound_to_the_digest_and_assertion(self):
        self.assertEqual(verdict(exited(), sha=OTHER), "ERROR")
        self.assertEqual(verdict(exited(), assertion="X3.twin.roundtrip"), "ERROR")
        self.assertEqual(verdict(saved(ORIGINAL), sha=OTHER), "PASS")
        self.assertEqual(verdict(saved(EMPTY), sha=OTHER), "FAIL")

    def test_changed_input_is_error(self):
        self.assertEqual(verdict(exited(), input_after=EMPTY), "ERROR")


STUB = """#!/bin/bash
# Self-test stub editor: maps a window for its own PID and, on every key press, renames
# wrong bytes over the opened path and commits a session journal file, as v1 does after an
# applied edit. A control comparing bytes must therefore fail.
target=$1
journal=$XDG_DATA_HOME/sublimite
/bin/mkdir -p "$journal"
exec > >(while read -r kind _; do
    if [ "$kind" = down ]; then
        printf 'stub bytes\\n' > "$target.stub" && /bin/mv -f "$target.stub" "$target"
        : > "$journal/.commit" && /bin/mv -f "$journal/.commit" "$journal/session-stub"
    fi
done)
exec {client} "$DISPLAY" window
"""


class StubControlTest(unittest.TestCase):
    def test_failing_control_reports_error(self):
        if DISPLAY is None:
            self.fail("--display :140..:149 is required for the stub run")
        for tool in (case.DRIVER, CLIENT):
            self.assertTrue(os.access(tool, os.X_OK), f"build {tool} first (make xdriver)")
        directory = BUILD / "large-undo" / "selftest"
        directory.mkdir(parents=True, exist_ok=True)
        stub = directory / "stub-editor"
        partial = directory / "stub-editor.partial"
        partial.write_text(STUB.format(client=CLIENT), encoding="ascii")
        partial.chmod(0o700)
        os.replace(partial, stub)
        runs = directory / "runs"
        status = case.main(["--target", str(stub), "--display", DISPLAY, "--runs", str(runs)])
        self.assertEqual(status, 2)
        record = json.loads(max(runs.glob("*/record.json"), key=os.path.getmtime).read_text())
        statuses = [(r["assertion"], r["status"]) for r in record["records"]]
        self.assertEqual(statuses, [("X3.twin.ctl.load_save", "ERROR")], statuses)
        self.assertEqual(record["records"][0]["predicate"],
                         "saved bytes differ from the control expectation")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--display")
    args, rest = parser.parse_known_args()
    DISPLAY = args.display
    unittest.main(argv=[sys.argv[0], *rest])
