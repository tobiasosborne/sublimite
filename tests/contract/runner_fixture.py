"""Scripted stand-in for a contract case, driven by tests/contract/test_run.py.

--plan names a JSON file {"counter": PATH, "steps": [STEP, ...]}. Every invocation appends
one line to the counter file first, then plays step min(n, len - 1), where n counts earlier
invocations: STEP = {"records": [...], "lines": [...], "exit": N, "hang": bool}. A fail-once
plan is therefore two steps, FAIL then PASS: a rerun would pass.
"""

import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import records  # noqa: E402

HANG_S = 60  # (G) fixture only: far beyond the 1 s timeout the hang test gives the runner.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--display")
    args = parser.parse_args()
    plan = json.loads(args.plan.read_text(encoding="utf-8"))
    counter = Path(plan["counter"])
    with counter.open("a", encoding="ascii") as log:
        print(args.display or "-", file=log)
    count = len(counter.read_text(encoding="ascii").splitlines())
    step = plan["steps"][min(count - 1, len(plan["steps"]) - 1)]
    for line in step.get("lines", ()):
        print(line, flush=True)
    for record in step.get("records", ()):
        # Bypass records.emit's validation: the runner must catch malformed records itself.
        print(records.PREFIX + json.dumps(record), flush=True)
    if step.get("hang"):
        time.sleep(HANG_S)
    return step["exit"]


if __name__ == "__main__":
    sys.exit(main())
