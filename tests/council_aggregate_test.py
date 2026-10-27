#!/usr/bin/env python3
"""edit-cxho.23: tests for tools/council_run.py (no model is called).

Checks the aggregate (mean, median, disagreement >= 2, control noise,
regression list), the schema validator (score outside 1-5, missing
criterion), the dry-run call count (one batched call per member: 3) and that
the blind A/B prompt never says which side is which.
Run: python3 tests/council_aggregate_test.py
"""
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools/council_run.py"


def load():
    spec = importlib.util.spec_from_file_location("council_run", TOOL)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def crit(scores, **extra):
    return [dict({"id": f"C{i}", "evidence": "e", "change": "c", "cost": "0 B"},
                 **{k: v[i - 1] for k, v in scores.items()}, **extra)
            for i in range(1, 11)]


def main():
    if not TOOL.exists():
        print(f"FAIL: {TOOL.relative_to(ROOT)} missing")
        return 1
    c = load()

    # Absolute mode: two members.
    a = {"criteria": crit({"score": [1, 2, 3, 4, 5, 1, 1, 1, 1, 1]}),
         "top_gaps": ["x", "y", "z"], "verdict": "v"}
    b = {"criteria": crit({"score": [1, 4, 3, 4, 5, 2, 1, 1, 1, 3]}),
         "top_gaps": ["x", "y", "z"], "verdict": "v"}
    for r in (a, b):
        assert c.validate(r, "absolute") == [], c.validate(r, "absolute")
    agg = c.aggregate_absolute({"m1": a, "m2": b})
    assert agg["C2"]["mean"] == 3.0 and agg["C2"]["median"] == 3.0
    assert agg["C2"]["disagree"] is True       # 2 vs 4
    assert agg["C6"]["disagree"] is False      # 1 vs 2
    assert agg["C10"]["disagree"] is True      # 1 vs 3

    # Schema rejects a score outside 1-5 and a missing criterion.
    bad = json.loads(json.dumps(a))
    bad["criteria"][0]["score"] = 6
    assert c.validate(bad, "absolute"), "score 6 accepted"
    bad = json.loads(json.dumps(a))
    del bad["criteria"][9]
    assert c.validate(bad, "absolute"), "missing C10 accepted"

    # A/B mode: key maps A/B to the two builds; candidate is B for this run.
    key = {"A": "base", "B": "cand", "control_scene": "07-code"}
    m1 = {"criteria": crit({"score_a": [2] * 10, "score_b": [3] * 10}),
          "scene_prefs": [{"scene": "07-code", "prefer": "same"},
                          {"scene": "02-tabs", "prefer": "B"}],
          "verdict": "v"}
    m2 = {"criteria": crit({"score_a": [2] * 10,
                            "score_b": [3, 3, 1, 3, 3, 3, 3, 3, 3, 3]}),
          "scene_prefs": [{"scene": "07-code", "prefer": "A"},
                          {"scene": "02-tabs", "prefer": "B"}],
          "verdict": "v"}
    for r in (m1, m2):
        assert c.validate(r, "ab") == [], c.validate(r, "ab")
    ab = c.aggregate_ab({"m1": m1, "m2": m2}, key)
    assert ab["per"]["C1"]["delta"] == 1.0
    assert ab["per"]["C3"]["delta"] == 0.0     # (+1 + -1) / 2
    assert ab["regressions"] == ["C3"], ab["regressions"]   # one member lower
    assert ab["control_noise"] == 0.5          # m2 saw a difference in a pair of identical images
    assert ab["prefer_cand"]["02-tabs"] == 2

    # Dry run: one batched call per member, three members, both modes.
    with tempfile.TemporaryDirectory() as t:
        shots = Path(t) / "shots"
        shots.mkdir()
        for s in c.SCENES:
            for th in ("dark", "light"):
                (shots / f"{s}-{th}.png").write_bytes(b"")
        for mode, extra in (("absolute", []), ("ab", ["--b", str(shots)])):
            run = Path(t) / f"run-{mode}"
            p = subprocess.run([sys.executable, str(TOOL), "prepare", "--mode",
                                mode, "--a", str(shots), *extra, "--out",
                                str(run), "--seed", "7", "--no-images"],
                               capture_output=True, text=True)
            assert p.returncode == 0, p.stderr
            p = subprocess.run([sys.executable, str(TOOL), "run", str(run),
                                "--dry-run"], capture_output=True, text=True)
            assert p.returncode == 0, p.stderr
            calls = [ln for ln in p.stdout.splitlines() if ln.startswith("CALL ")]
            assert len(calls) == 3, p.stdout
            # The opus member reads images by path: every listed path must be
            # absolute (round 1 listed bare names and Opus saw no pixels).
            listed = re.findall(r"^\d+\. `([^`]+)`", (run / "prompt.md").read_text(), re.M)
            assert listed and all(Path(x).is_absolute() for x in listed), listed[:2]
            if mode == "ab":
                prompt = (run / "prompt.md").read_text().lower()
                for w in ("baseline", "candidate", " new ", " old ",
                          "before", "after"):
                    assert w not in prompt, f"blind prompt leaks {w!r}"
                assert "key.json" not in prompt
                key = json.loads((run / "key.json").read_text())
                assert key["control_scene"] != c.CROP_SCENE, key

    print("council_aggregate_test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
