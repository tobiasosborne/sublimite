#!/usr/bin/env python3
"""tools/codex_run.py: retry and resume a codex exec job across capacity errors.

A stub codex (written below) fails the first two calls the way codex sol did on
2026-10-28 ("Selected model is at capacity", exit 1), then succeeds. The runner
must resume the same thread instead of starting over, and the report must exist.
Run: python3 tests/codex_run_test.py
"""
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNNER = os.path.join(ROOT, "tools", "codex_run.py")

STUB = r'''#!/usr/bin/env python3
import json, os, sys
state = os.environ["STUB_STATE"]
n = int(open(state).read()) if os.path.exists(state) else 0
open(state, "w").write(str(n + 1))
open(state + ".argv", "a").write(json.dumps(sys.argv[1:]) + "\n")
args = sys.argv[1:]
out = args[args.index("-o") + 1] if "-o" in args else None
fail_first = int(os.environ.get("STUB_FAILS", "2"))
if "resume" not in args:
    print(json.dumps({"type": "thread.started", "thread_id": "t-123"}))
if n < fail_first:
    print("ERROR: Selected model is at capacity. Please try a different model.", file=sys.stderr)
    sys.exit(1)
print(json.dumps({"type": "turn.completed"}))
if out: open(out, "w").write("final report\n")
'''


def run(fails, extra=()):
    d = tempfile.mkdtemp()
    stub = os.path.join(d, "codex")
    open(stub, "w").write(STUB); os.chmod(stub, 0o755)
    brief = os.path.join(d, "brief.md"); open(brief, "w").write("do the thing\n")
    report = os.path.join(d, "report.md")
    env = dict(os.environ, CODEX_BIN=stub, STUB_STATE=os.path.join(d, "n"),
               STUB_FAILS=str(fails), CODEX_RUN_BACKOFF="0")
    r = subprocess.run([sys.executable, RUNNER, "--dir", d, "--out", report, "--log", os.path.join(d, "log"),
                        *extra, brief], env=env, capture_output=True, text=True)
    calls = [json.loads(l) for l in open(os.path.join(d, "n.argv"))] if os.path.exists(os.path.join(d, "n.argv")) else []
    return r, calls, report


def main():
    failed = 0
    def check(cond, what):
        nonlocal failed
        print(("PASS " if cond else "FAIL ") + what)
        failed += not cond

    r, calls, report = run(2)
    check(r.returncode == 0, "two capacity errors then success exits 0")
    check(len(calls) == 3, "three codex calls (1 fresh + 2 resumes)")
    check(calls and "resume" not in calls[0], "first call is a fresh exec")
    check(all("resume" in c and "t-123" in c for c in calls[1:]), "retries resume the same thread")
    check(os.path.exists(report) and open(report).read() == "final report\n", "report written")

    r, calls, _ = run(99, ["--tries", "3"])
    check(r.returncode != 0, "gives up after --tries with a nonzero exit")
    check(len(calls) == 3, "exactly --tries calls")

    r, calls, _ = run(0, ["--resume", "t-777"])
    check(r.returncode == 0 and calls and calls[0][:2] == ["exec", "resume"] and "t-777" in calls[0],
          "--resume adopts an existing thread")
    print("codex_run_test:", "FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
