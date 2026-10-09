#!/usr/bin/env python3
"""Read-only CLI rejection tests: never authorize or connect to a display."""
import os
import subprocess
import sys

env = dict(os.environ, DISPLAY=":99", EDIT_DISPLAY=":99")
env.pop("EDIT_ALLOW_REAL_DISPLAY", None)
env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
cases = [
    (["--build", "--real-display"], "--real-display requires EDIT_ALLOW_REAL_DISPLAY=1"),
    (["--build", "--launches", "0"], "--launches must be an integer in 1..10000"),
    (["--build", "--launches", "-1"], "--launches must be an integer in 1..10000"),
    (["--build", "--launches", "10001"], "--launches must be an integer in 1..10000"),
    (["--build", "--launches", "1x"], "--launches must be an integer in 1..10000"),
    (["--build", "--launches"], "--launches requires N"),
    (["--build", "--unknown"], "usage:"),
    (["--build", "--run"], "choose one mode"),
]
for args, message in cases:
    run = subprocess.run(["tools/zygote_bench.sh", *args], env=env,
                         capture_output=True, text=True, check=False)
    assert run.returncode == 2 and message in run.stderr, (
        f"zygote CLI contract: FAIL {args}: exit={run.returncode}; {run.stderr.strip()}")
print("zygote CLI contract: PASS opt-in refusal, launch bounds, missing/unknown/conflicting arguments")
if "--built" in sys.argv:
    for args, message in cases:
        # Driver has no build mode; use the same rejection matrix with --run.
        driver_args = ["--run" if arg == "--build" else arg for arg in args]
        run = subprocess.run(["variants/P4.14/zygote_bench", *driver_args], env=env,
                             capture_output=True, text=True, check=False)
        assert run.returncode == 2 and message in run.stderr, (driver_args, run.stderr)
    for variant in "abcd":
        run = subprocess.run([f"variants/P4.14/{variant}/launch", "--normal", "--real-display"],
                             env=env, capture_output=True, text=True, check=False)
        assert run.returncode == 2 and "display refused" in run.stderr, (variant, run.stderr)
    print("zygote executable guard contract: PASS driver and A/B/C/D independently refuse missing permission")
