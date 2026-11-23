"""Self-test for tests/contract/cases/large_undo.py and large_undo_fixture.py.

ClassifyTests: only the exact A8 failure mode carries its signature (status 1, v1's terminal
diagnostic for this session, destination untouched, after the scored Undo was acknowledged);
every neighbour of it reports as itself. Classification never looks at the target: whether the
signature is a reviewed XFAIL is the runner's decision (tests/contract/test_run.py). The
changed-input test also holds the fixture writer to the reviewed bytes, under concurrent writers
too. StubControlTest: a stub "editor" that saves wrong bytes makes the first control fail, and
the run must report ERROR (exit 4), never PASS, keeping the wrong bytes; it is judged by its own
printed record and its own receipt, never by another run's. The test IDs are the ones
tests/contract/expectations.json owns for X3-SELFTEST, so new checks are subtests.

Usage: python3 -B tests/contract/test_large_undo.py --display :1NN   (a contract display; needs
build/xdriver and build/xdriver-test-client; the stub run owns Xvfb on that display like the
case does).
"""

import argparse
import contextlib
import hashlib
import json
import os
import subprocess
from pathlib import Path
import sys
import tempfile
import time
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "cases"))
import large_undo as case  # noqa: E402
import large_undo_fixture as fixture  # noqa: E402
import records  # noqa: E402
from xdriver import XDriverError  # noqa: E402

BUILD = case.BUILD
CLIENT = BUILD / "xdriver-test-client"
DISPLAY = None
ORIGINAL = {"bytes": 4, "sha256": hashlib.sha256(b"text").hexdigest()}
EMPTY = {"bytes": 0, "sha256": hashlib.sha256(b"").hexdigest()}
UNTOUCHED = {"st_ino": 7, "st_ctime_ns": 1, "sha256": ORIGINAL["sha256"]}
JOURNAL = "/p/data/sublimite"
# The measured A8 stderr (run 20261123T120621Z-232247, over-scored.stderr), paths shortened.
A8_STDERR = ("sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry\n"
             f"sublimite: error -105; journal: {JOURNAL}/session-jbUeKD\n")
STUB_BYTES = b"stub bytes\n"
STUB_SAVED = {"bytes": len(STUB_BYTES), "sha256": hashlib.sha256(STUB_BYTES).hexdigest()}


def exited(status=1, stderr=A8_STDERR, after=UNTOUCHED, undo_injected=True):
    return {"kind": "exited", "undo_injected": undo_injected, "exit": status, "stderr": stderr,
            "journal_dir": JOURNAL, "before": UNTOUCHED, "after": after}


def saved(output, quit_exit=0):
    return {"kind": "saved", "output": output, "quit_exit": quit_exit}


def verdict(outcome, input_after=ORIGINAL):
    status, _, _, signature = case.classify(ORIGINAL, outcome, input_after)
    return status if signature is None else (status, signature)


def private_dir(prefix):
    """A directory no other self-test, gate or concurrent run writes (finding 13)."""
    parent = BUILD / "large-undo" / "selftest"
    parent.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=prefix + "-", dir=parent))


class FakeDriver:
    """Acknowledges every action except `refuse`, which it reports as a lost target."""

    def __init__(self, refuse=None):
        self.sent, self.refuse = [], refuse

    def key(self, keysym):
        self._inject(keysym)

    def chord(self, *keys):
        self._inject(keys)

    def _inject(self, action):
        if action == self.refuse:
            raise XDriverError("FOCUS_LOST")
        self.sent.append(action)


def drive(driver, alive):
    """The scored keys through case.inject: (receipts, verdict of the exit that follows)."""
    sent, _error = case.inject(driver, case.SCORED_KEYS, alive)
    if driver.sent != list(case.SCORED_KEYS[:sent]):
        raise AssertionError(f"receipts {sent} but the driver saw {driver.sent}")
    return sent, verdict(exited(undo_injected=sent == len(case.SCORED_KEYS)))


def fixture_dir(test):
    """A private fixture directory; the published names are removed after the test, and a
    leftover writer temporary makes the removal fail."""
    directory = private_dir("fixture")

    def remove():
        for name in ("large_undo_twin.txt", "large_undo_twin.json"):
            with contextlib.suppress(FileNotFoundError):
                os.unlink(directory / name)
        os.rmdir(directory)

    test.addCleanup(remove)
    return directory


def bad_generator_is_rejected(test):
    directory = fixture_dir(test)
    original = fixture.line

    def changed(number):  # one record differs, every length is unchanged
        text = original(number)
        return text.replace(b" ", b"_", 1) if number == 1000 else text

    fixture.line = changed
    try:
        with test.assertRaises(RuntimeError):
            fixture.write(directory, "twin")
    finally:
        fixture.line = original
    test.assertEqual(sorted(os.listdir(directory)), [])


def damaged_cache_is_regenerated(test):
    directory = fixture_dir(test)
    path = directory / "large_undo_twin.txt"
    fixture.write(directory, "twin")
    with open(path, "r+b") as stream:
        stream.seek(fixture.TWIN_BYTES // 2)
        byte = stream.read(1)
        stream.seek(-1, os.SEEK_CUR)
        stream.write(bytes([byte[0] ^ 1]))
    damaged = os.stat(path).st_ino
    manifest = fixture.write(directory, "twin")
    size, digest = fixture.REVIEWED["twin"]
    test.assertEqual((manifest["bytes"], manifest["sha256"]), (size, digest))
    test.assertEqual(fixture.file_digest(path), digest)
    test.assertNotEqual(os.stat(path).st_ino, damaged)


def concurrent_writers_publish_complete_bytes(test):
    directory = fixture_dir(test)
    writers = [subprocess.Popen([sys.executable, "-B", fixture.__file__, "--out", str(directory), "twin"],
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
               for _ in range(2)]
    size, digest = fixture.REVIEWED["twin"]
    for writer in writers:
        out, err = writer.communicate()
        test.assertEqual(writer.returncode, 0, err.decode(errors="replace"))
        manifest = json.loads(out)
        test.assertEqual((manifest["bytes"], manifest["sha256"]), (size, digest))
    test.assertEqual(fixture.file_digest(directory / "large_undo_twin.txt"), digest)
    test.assertEqual(sorted(os.listdir(directory)), ["large_undo_twin.json", "large_undo_twin.txt"])


class ClassifyTests(unittest.TestCase):
    def test_exact_failure_mode_carries_its_signature(self):
        signed = ("FAIL", case.SIGNATURE_EXIT_105)
        self.assertEqual(verdict(exited()), signed)
        with self.subTest("A8 sequence: every scored key acknowledged"):
            self.assertEqual(drive(FakeDriver(), lambda: True), (3, signed))

    def test_neighbours_of_the_failure_mode_carry_no_signature(self):
        other_bytes = {"bytes": 4, "sha256": hashlib.sha256(b"txet").hexdigest()}
        cases = {
            "other status": (exited(status=2), "ERROR"),
            "signal": (exited(status=-9), "ERROR"),
            "other error": (exited(stderr=A8_STDERR.replace("-105", "-104")), "ERROR"),
            "longer numeric token": (exited(stderr=A8_STDERR.replace("-105", "-1050")), "ERROR"),
            "error line not terminal": (exited(stderr=A8_STDERR + "sublimite: later\n"), "ERROR"),
            "unterminated line": (exited(stderr=A8_STDERR[:-1]), "ERROR"),
            "another profile's journal": (exited(stderr=A8_STDERR.replace(JOURNAL, "/q/sublimite")),
                                          "ERROR"),
            "no stderr": (exited(stderr=""), "ERROR"),
            "undo not injected": (exited(undo_injected=False), "ERROR"),
            "destination rewritten": (exited(after={**UNTOUCHED, "st_ino": 8}), "ERROR"),
            "destination ctime": (exited(after={**UNTOUCHED, "st_ctime_ns": 2}), "ERROR"),
            "0 B save": (saved(EMPTY), "FAIL"),
            "same size, other bytes": (saved(other_bytes), "FAIL"),
            "round trip": (saved(ORIGINAL), "PASS"),
            "round trip, bad quit": (saved(ORIGINAL, quit_exit=1), "ERROR"),
        }
        for name, (outcome, expected) in cases.items():
            with self.subTest(name):
                self.assertEqual(verdict(outcome), expected)
        dies_on_delete = FakeDriver()
        with self.subTest("editor dies on Delete"):
            self.assertEqual(drive(dies_on_delete, lambda: dies_on_delete.sent[-1:] != ["Delete"]),
                             (2, "ERROR"))
        with self.subTest("final chord refused"):
            self.assertEqual(drive(FakeDriver(refuse=case.SCORED_KEYS[-1]), lambda: True), (2, "ERROR"))

    def test_changed_input_is_error(self):
        """A changed input fixture is ERROR; the fixture writer publishes only the reviewed
        bytes (each check generates the 33,554,432 B twin, under 1 s [AC] (M))."""
        self.assertEqual(verdict(exited(), input_after=EMPTY), "ERROR")
        for check in (bad_generator_is_rejected, damaged_cache_is_regenerated,
                      concurrent_writers_publish_complete_bytes):
            with self.subTest(check.__name__):
                check(self)


def check_stub_run(test, status, transcript, runs):
    """The stub run's verdict from its single printed record and that run's own receipt."""
    test.assertEqual(status, records.EXIT["ERROR"], transcript[-2000:])
    printed = [json.loads(line[len(records.PREFIX):]) for line in transcript.splitlines()
               if line.startswith(records.PREFIX)]
    test.assertEqual(len(printed), 1, printed)
    record = printed[0]
    test.assertEqual({key: record.get(key) for key in ("case_id", "assertion_id", "status",
                                                       "predicate", "observed")},
                     {"case_id": "X3", "assertion_id": "twin.ctl.load_save", "status": "ERROR",
                      "predicate": "saved bytes differ from the control expectation",
                      "observed": STUB_SAVED})
    test.assertEqual(record["expected"]["bytes"], fixture.TWIN_BYTES + 1)
    artifacts = Path(record["artifacts"])
    test.assertEqual(artifacts.parent, runs)
    receipt = json.loads((artifacts / "record.json").read_text())
    test.assertEqual(receipt["records"], printed)
    return artifacts


def foreign_receipt_cannot_vouch(test):
    """A journal timeout at the stub's ID fails check_stub_run even beside a planted
    future-dated correct receipt; the correct run itself passes it."""
    runs = private_dir("check") / "runs"
    stub = {"case_id": "X3", "assertion_id": "twin.ctl.load_save", "status": "ERROR",
            "evidence": "A", "first_required_wave": 2,
            "predicate": "saved bytes differ from the control expectation",
            "observed": STUB_SAVED, "expected": {"bytes": fixture.TWIN_BYTES + 1}}
    timeout = {**stub, "predicate": "hang detector expired waiting for journal commit",
               "observed": {"deadline_s": case.JOURNAL_COMMIT_S}}
    for name, entry in (("own", timeout), ("planted", stub)):
        (runs / name).mkdir(parents=True)
        entry["artifacts"] = str(runs / name)
        (runs / name / "record.json").write_text(json.dumps({"records": [entry]}))
    future = time.time() + 86_400
    os.utime(runs / "planted" / "record.json", (future, future))
    with test.assertRaises(AssertionError):
        check_stub_run(test, records.EXIT["ERROR"], records.PREFIX + json.dumps(timeout) + "\n", runs)
    check_stub_run(test, records.EXIT["ERROR"], records.PREFIX + json.dumps(stub) + "\n", runs)


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
        with self.subTest("a foreign receipt cannot vouch"):
            foreign_receipt_cannot_vouch(self)
        if DISPLAY is None:
            self.fail("--display (a contract display, :120..:199) is required for the stub run")
        for tool in (case.DRIVER, CLIENT):
            self.assertTrue(os.access(tool, os.X_OK), f"build {tool} first (make xdriver)")
        directory = private_dir("stub")
        stub = directory / "stub-editor"
        stub.write_text(STUB.format(client=CLIENT), encoding="ascii")
        stub.chmod(0o700)
        runs = directory / "runs"
        # A child process, as the runner invokes it: the case's own RECORD lines and exit status
        # stay in its transcript instead of joining this self-test's records.
        transcript = directory / "stub-run.stdout"
        with transcript.open("wb") as out:
            status = subprocess.run([sys.executable, "-B", case.__file__, "--target", str(stub),
                                     "--display", DISPLAY, "--runs", str(runs)],
                                    stdin=subprocess.DEVNULL, stdout=out, check=False).returncode
        artifacts = check_stub_run(self, status, transcript.read_text(), runs)
        # The failing destination stays for diagnosis (finding 27).
        self.assertEqual((artifacts / "twin-control.destination").read_bytes(), STUB_BYTES)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--display")
    args, rest = parser.parse_known_args()
    DISPLAY = args.display
    sys.exit(records.unittest_main("X3-SELFTEST", argv=rest))
