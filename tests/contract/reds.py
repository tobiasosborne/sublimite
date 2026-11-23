#!/usr/bin/env python3
"""Red receipts that verify their cause (finding 17, docs/decisions/v2-he7.33.md).

A red control's exit status says only that something failed. A red receipt is taken here
instead: the control's command runs once and its RECORD lines are read with the runner's own
reader (records.parse, records.problems). It qualifies only when
  - every record is well formed and belongs to the control's case;
  - the first non-PASS record is the control's ID with the control's status, and nothing of
    that case follows it (first failure final; a later teardown ERROR disqualifies);
  - every ID the manifest lists before it in that case is PASS, in manifest order (the
    preceding controls), unless the control runs alone;
  - its named fields hold the control's own predicate and evidence; and
  - for an expectation swap, the record shows the healthy observation first: `ordinary`
    holds an observation equal to the ordinary expectation, and the contrary expectation
    differs from both it and the observation.
An earlier setup error, or a different mismatch at the same ID, therefore never qualifies.

Usage: python3 -B tests/contract/reds.py --build-dir ABS --oracle ABS --display :N
       --digests JSON [--out DIR] [NAME ...]        (every red when no NAME is given)
"""

import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import records  # noqa: E402

MANIFEST = HERE / "expectations.json"
SWAP = "swap"        # an expectation swap: `ordinary` must show the healthy observation
FIXTURE = "fixture"  # the fixture itself forces the contrary observation (match/equals prove it)
MISMATCH = "Mismatch: {}: observed value differs from expectation$"
NO_REPLY = "^TimeoutError: missing snapshot reply before watchdog$"


def red(script, argv, case, assertion, status="FAIL", kind=SWAP, equals=None, alone=False, **match):
    """One red control: its command, the record it must produce and the fields naming its
    cause (`match`: regex per field, `equals`: value per dotted field)."""
    return {"script": script, "argv": argv, "case": case, "assertion": assertion,
            "status": status, "kind": kind, "match": match, "equals": equals or {}, "alone": alone}


def _table():
    neg = lambda name: ["--negative", name]  # noqa: E731
    reds = {f"{c}.{a}": red("navigation", neg(f"{c}.{a}"), c, a, predicate=f"^{re.escape(p)}$")
            for c, a, p in (
                ("R1", "bottom-setup", "bottom-row caret before Return"),
                ("R1", "wrapped-return", "minimal reveal and neighbouring word fragments"),
                ("R1", "return-caret", "settled caret after Return"),
                ("R1", "saved-bytes", "saved buffer equals inserting one LF into the fixture"),
                ("R6", "first-viewport", "first viewport viewport and target label"),
                ("R6", "tail-navigation", "Ctrl+End viewport and target label"),
                ("R6", "marker", "type marker viewport and target label"),
                ("R6", "occupied-navigation", "full-corpus search completed with both endpoint hits"))}
    reds.update({f"{c}.{a}": red("files", neg(f"{c}.{a}"), c, a, predicate="^" + MISMATCH.format(a))
                 for c, ids in (("R3", "results-between-a-and-b save-b-bytes save-a-bytes "
                                       "results-text-still-visible other-files-unchanged law2-count"),
                                ("R5", "open-alive header-painted adjacent-text-and-sibling-save "
                                       "open-exit reopen-alive settled-binary-tab-revisit reopen-exit "
                                       "bytes-unchanged law2-count"))
                 for a in ids.split()})
    reds.update({f"R31.{c}": red("recovery", ["--case", c, "--wrong-expectation"], "R31", c,
                                 alone=True, step="^recovered-buffer$",
                                 predicate="^recovered-buffer: expected ")
                 for c in ("pre-ack-file", "pre-ack-untitled", "post-ack-file", "post-ack-untitled")})
    reds.update({f"R31.{c}.decoy": red(
        "recovery", ["--case", c, "--decoy-paste"], "R31", c, "ERROR", FIXTURE, alone=True,
        predicate="^submitted-action-frame: Refused: missing acknowledgment witness")
        for c in ("post-ack-file", "post-ack-untitled")})
    reds.update({
        "R1.snapshot-reply": red("navigation", neg("R1.snapshot-reply"), "R1", "snapshot-reply",
                                 "ERROR", FIXTURE, alone=True, predicate="^first viewport snapshot$",
                                 reason=NO_REPLY),
        "R3.search-results": red("files", neg("R3.search-results"), "R3", "results-between-a-and-b",
                                 step="^search-readiness$", predicate="^Mismatch: completed Find "
                                 "Results differ from the fixture results$"),
        "R3.snapshot-reply": red("files", neg("R3.snapshot-reply"), "R3", "results-between-a-and-b",
                                 "ERROR", FIXTURE, step="^search-readiness$", predicate=NO_REPLY),
        "R3.save-signal": red("files", neg("R3.save-signal"), "R3", "save-b-bytes", "ERROR", FIXTURE,
                              step="^save-b-signal$", predicate=r"^TimeoutError: no 0x80 file "
                              r"event for b\.c before deadline$"),
        "R5.nonzero-exit": red("files", neg("R5.nonzero-exit"), "R5", "open-exit", kind=FIXTURE,
                               equals={"observed": -9, "expected": 0}),
        "R31.torn-prefix": red("recovery", ["--case", "torn-prefix"], "R31", "torn-prefix",
                               kind=FIXTURE, alone=True, step="^recovered-buffer$", equals={
                                   "observed_hex": b"durable source bytes [whole".hex()}),
        "R4.clean-survivor-tab-order": red("capacity", neg("clean-survivor-tab-order"), "R4",
                                           "clean-survivor-tab-order", predicate=": " + MISMATCH.format(
                                               "clean-survivor-tab-order")),
        "R4.no-pressure": red("capacity", neg("no-pressure"), "R4", "partial-restore-triggered",
                              kind=FIXTURE, predicate=": " + MISMATCH.format("partial-restore-triggered"),
                              equals={"observed.older_restored_whole": True,
                                      "observed.unclassified": []}),
    })
    return reds


REDS = _table()
SCRIPTS = {"navigation": "cases/navigation.py", "files": "cases/files.py",
           "recovery": "cases/recovery.py", "capacity": "cases/capacity.py"}


def field(record, dotted):
    value = record
    for part in dotted.split("."):
        if not isinstance(value, dict) or part not in value:
            return KeyError
        value = value[part]
    return value


def check(spec, lines, manifest):
    """Why the transcript `lines` does not qualify as `spec`'s red (empty when it does)."""
    problems, found = [], []
    for number, line in enumerate(lines, 1):
        if not line.startswith(records.PREFIX):
            continue
        try:
            record = records.parse(line)
        except ValueError as exc:
            problems.append(f"line {number}: malformed record ({exc})")
            continue
        problems += [f"line {number}: {p}" for p in records.problems(record)]
        found.append(record)
    found = [r for r in found if isinstance(r, dict)]
    others = sorted({r.get("case_id") for r in found} - {spec["case"]})
    if others:
        problems.append(f"records of other cases {others}")
    own = [r for r in found if r.get("case_id") == spec["case"]]
    red = next((i for i, r in enumerate(own) if r.get("status") != "PASS"), None)
    if red is None:
        return problems + ["no non-PASS record: the control did not fail"]
    record = own[red]
    if (record.get("assertion_id"), record.get("status")) != (spec["assertion"], spec["status"]):
        problems.append(f"first non-PASS record is {record.get('assertion_id')} "
                        f"{record.get('status')}, not {spec['assertion']} {spec['status']}")
    if own[red + 1:]:
        problems.append(f"records after the red: {[r.get('assertion_id') for r in own[red + 1:]]}")
    order = list(manifest["cases"][spec["case"]]["assertions"])
    preceding = [] if spec["alone"] else order[:order.index(spec["assertion"])]
    passed = [r.get("assertion_id") for r in own[:red]]
    if passed != preceding:
        problems.append(f"preceding controls {passed}, expected PASS for {preceding}")
    for name, pattern in spec["match"].items():
        value = field(record, name)
        if not isinstance(value, str) or not re.search(pattern, value):
            problems.append(f"{name} {value!r} does not match {pattern!r}")
    for name, expected in spec["equals"].items():
        if field(record, name) != expected:
            problems.append(f"{name} {field(record, name)!r} is not {expected!r}")
    if spec["kind"] == SWAP:
        ordinary = record.get("ordinary")
        # R31 records name its legal alternatives in expected_hex; the others one value.
        if "expected_hex" in record:
            contrary, observed = record["expected_hex"], record.get("observed_hex")
        else:
            contrary, observed = [record.get("expected")], record.get("observed")
        if not isinstance(ordinary, dict) or set(ordinary) != {"expected", "observed"}:
            problems.append("no ordinary observation: the healthy expectation was not shown first")
        elif ordinary["observed"] != ordinary["expected"]:
            problems.append("the ordinary observation differs from the ordinary expectation")
        elif ordinary["expected"] in contrary or observed in contrary:
            problems.append("the contrary expectation is not contrary")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--oracle", required=True, type=Path)
    parser.add_argument("--display", required=True)
    parser.add_argument("--out", type=Path, default=HERE.parents[1] / "build" / "red-receipts")
    parser.add_argument("names", nargs="*", help=f"reds to take: {', '.join(REDS)}")
    records.digests_argument(parser)
    args = parser.parse_args()
    if set(args.names) - set(REDS):
        parser.error(f"unknown reds {sorted(set(args.names) - set(REDS))}")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"),
                          object_pairs_hook=records.unique_object)
    run_dir = args.out / datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_dir.mkdir(parents=True)
    failed = []
    for name in args.names or list(REDS):
        spec = REDS[name]
        script = SCRIPTS[spec["script"]]
        location = ["--oracle", str(args.oracle)] if spec["script"] == "recovery" else []
        argv = [sys.executable, "-B", str(HERE / script), "--build-dir", str(args.build_dir),
                *location, "--display", args.display, "--digests", json.dumps(args.digests),
                *spec["argv"]]
        invocation = next(i for i in manifest["invocations"].values()
                          if i["argv"][0] == f"tests/contract/{script}")
        try:
            result = subprocess.run(argv, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                    timeout=invocation["timeout_s"], check=False)
            stdout, status = result.stdout, result.returncode
            problems = check(spec, stdout.splitlines(), manifest)
            if status != records.EXIT[spec["status"]]:
                problems.append(f"exit {status}, expected {records.EXIT[spec['status']]}")
        except subprocess.TimeoutExpired:
            stdout, status, problems = "", None, [f"no exit within {invocation['timeout_s']} s"]
        receipt = {"red": name, "spec": spec, "argv": argv, "exit": status,
                   "qualifies": not problems, "problems": problems,
                   "bat0": Path("/sys/class/power_supply/BAT0/status").read_text().strip(),
                   "loadavg": os.getloadavg(),
                   "records": [line for line in stdout.splitlines()
                               if line.startswith(records.PREFIX)]}
        (run_dir / f"{name}.json").write_text(json.dumps(receipt, indent=1) + "\n")
        print(f"{name}: {'qualifies' if not problems else 'DOES NOT QUALIFY'}"
              + "".join(f"\n  {p}" for p in problems), flush=True)
        if problems:
            failed.append(name)
    print(f"red receipts in {run_dir}: {len(failed)} of {len(args.names or REDS)} do not qualify")
    return records.EXIT["ERROR"] if failed else records.EXIT["PASS"]


if __name__ == "__main__":
    sys.exit(main())
