"""Result records and exit statuses shared by every contract case, self-test and the runner.

A record is one line on stdout: the prefix "RECORD " and a JSON object. Required keys:
case_id and assertion_id (non-empty strings), status (PASS, FAIL or ERROR), evidence (one of
EVIDENCE), first_required_wave (int, the first v2 wave that runs the assertion; 0 for
harness self-tests) and predicate (string). A FAIL may carry "signature", a string that
names the observed failure mode exactly; the runner's manifest decides whether that mode is
a reviewed expected failure. Cases never write XFAIL or XPASS: those are verdicts of the
runner against expectations.json, never observations. Every other key is case evidence.

Exit statuses, one each, the same for every case script, self-test and the runner
(docs/decisions/v2-he7.16.md): PASS 0, FAIL 3, ERROR 4; the runner alone adds XPASS 5.
Python reserves 1 for an uncaught exception and argparse 2 for a usage error, so neither
can be read as a result.
"""

import json
import sys
import unittest

PREFIX = "RECORD "
EXIT = {"PASS": 0, "FAIL": 3, "ERROR": 4, "XPASS": 5}
STATUSES = ("PASS", "FAIL", "ERROR")
EVIDENCE = ("A", "B", "C", "A+B", "harness")
REQUIRED = {"case_id": str, "assertion_id": str, "status": str, "evidence": str,
            "first_required_wave": int, "predicate": str}


def problems(record):
    """Why `record` is not a well-formed result record (empty list when it is)."""
    if not isinstance(record, dict):
        return ["record is not a JSON object"]
    found = []
    for key, kind in REQUIRED.items():
        value = record.get(key)
        # bool is an int subclass; a wave is never true/false.
        if not isinstance(value, kind) or isinstance(value, bool):
            found.append(f"{key} must be {kind.__name__}, got {value!r}")
        elif kind is str and key != "predicate" and not value:
            found.append(f"{key} is empty")
    if record.get("status") not in STATUSES:
        found.append(f"status must be one of {STATUSES}, got {record.get('status')!r}")
    if record.get("evidence") not in EVIDENCE:
        found.append(f"evidence must be one of {EVIDENCE}, got {record.get('evidence')!r}")
    wave = record.get("first_required_wave")
    if isinstance(wave, int) and wave < 0:
        found.append("first_required_wave is negative")
    if "signature" in record and (record.get("status") != "FAIL"
                                  or not isinstance(record["signature"], str)
                                  or not record["signature"]):
        found.append("signature is a non-empty string on a FAIL record only")
    return found


def emit(record, default=str):
    """Validate and print one record; a malformed record is a harness bug."""
    found = problems(record)
    if found:
        raise ValueError("malformed contract record: " + "; ".join(found))
    print(PREFIX + json.dumps(record, default=default, sort_keys=True), flush=True)


def exit_status(statuses):
    """ERROR dominates FAIL dominates PASS; no records at all is an ERROR."""
    statuses = list(statuses)
    for status in ("ERROR", "FAIL"):
        if status in statuses:
            return EXIT[status]
    return EXIT["PASS"] if statuses else EXIT["ERROR"]


def _first_line(err):
    """'Type: first line of the message'; the full traceback stays in the stderr transcript."""
    message = str(err[1]).strip().splitlines()
    return f"{err[0].__name__}: {message[0] if message else ''}"


class _Result(unittest.TextTestResult):
    """Collect one outcome per test method; subtests fold into their method."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.outcomes = {}

    def _note(self, test, status, predicate):
        if isinstance(test, unittest.TestCase):
            key = f"{type(test).__name__}.{test._testMethodName}"
        else:
            # unittest's _ErrorHolder for a failing setUpModule/setUpClass hook.
            key = getattr(test, "description", str(test)).split(" ", 1)[0]
        previous = self.outcomes.get(key)
        rank = {"PASS": 0, "FAIL": 1, "ERROR": 2}
        if previous is None or rank[status] > rank[previous[0]]:
            self.outcomes[key] = (status, predicate)

    def addSuccess(self, test):
        super().addSuccess(test)
        self._note(test, "PASS", "passed")

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self._note(test, "FAIL", _first_line(err))

    def addError(self, test, err):
        super().addError(test, err)
        # Module and class fixtures report through an _ErrorHolder named after the hook.
        self._note(test, "ERROR", _first_line(err))

    def addSubTest(self, test, subtest, err):
        super().addSubTest(test, subtest, err)
        if err is None:
            self._note(test, "PASS", "passed")
        elif issubclass(err[0], test.failureException):
            self._note(test, "FAIL", f"{subtest._subDescription()}: {_first_line(err)}")
        else:
            self._note(test, "ERROR", f"{subtest._subDescription()}: {_first_line(err)}")

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self._note(test, "ERROR", f"skipped ({reason}); a contract self-test never skips")

    def addExpectedFailure(self, test, err):
        super().addExpectedFailure(test, err)
        self._note(test, "ERROR", "expectedFailure is not a contract outcome")

    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self._note(test, "ERROR", "unexpectedSuccess is not a contract outcome")


def run_suite(suite, case_id, *, evidence="harness", first_required_wave=0):
    """Run a unittest suite once, emit one record per test method, return the exit status.

    A test that skips, or a module/class fixture that raises, is an ERROR; an empty suite is
    an ERROR (zero collected)."""
    runner = unittest.TextTestRunner(stream=sys.stderr, verbosity=2, resultclass=_Result)
    result = runner.run(suite)
    statuses = []
    for assertion, (status, predicate) in result.outcomes.items():
        emit({"case_id": case_id, "assertion_id": assertion, "status": status,
              "evidence": evidence, "first_required_wave": first_required_wave,
              "predicate": predicate})
        statuses.append(status)
    if not statuses:
        emit({"case_id": case_id, "assertion_id": "collection", "status": "ERROR",
              "evidence": evidence, "first_required_wave": first_required_wave,
              "predicate": "zero tests collected"})
        statuses.append("ERROR")
    return exit_status(statuses)


def unittest_main(case_id, module="__main__", argv=None, **keys):
    """`unittest.main` replacement: load the module's tests (or the named ones), run once."""
    names = list(sys.argv[1:] if argv is None else argv)
    loader = unittest.defaultTestLoader
    if names:
        suite = loader.loadTestsFromNames(names, sys.modules[module])
    else:
        suite = loader.loadTestsFromModule(sys.modules[module])
    return run_suite(suite, case_id, **keys)
