#!/usr/bin/env python3
"""Run a codex exec job and carry it across capacity errors by resuming its thread.

codex sol failed twice on 2026-10-28 with "Selected model is at capacity"
(exit 1) part-way through a job; the work done so far stayed in the worktree
and in the thread. This runner records the thread id from `codex exec --json`,
and on a nonzero exit or a missing/empty report waits, then runs
`codex exec resume <thread> "continue"`, which keeps the thread's context and
its sandbox/approval mode (M: a resumed --approve-for-me probe thread still
wrote files, 2026-10-28).

    python3 tools/codex_run.py --dir <worktree> --out <report.md> --log <log> \
        [--effort xhigh] [--model gpt-6.1-sol] [--read-only] [--tries N] \
        [--resume <thread-id>] [--schema S] [--image P ...] <brief.md>

Env: CODEX_BIN (default codex), CODEX_RUN_BACKOFF (first wait in seconds).
Exit: 0 when a report was written, else the last codex exit code (or 1).
"""
import argparse, json, os, subprocess, sys, time

# Backoff: (E) the 2026-10-28 capacity errors cleared within minutes (M: the
# round-3 job logged two and carried on). First wait 60 s, doubling, capped at
# 900 s: with the default 8 tries the waits sum to 60+120+240+480+900+900+900
# = 3600 s (P), so a job is abandoned after about an hour of capacity errors.
FIRST_WAIT = 60
MAX_WAIT = 900
CONTINUE = ("You were interrupted by an error (model capacity or a dropped stream). "
            "Continue the task from where you stopped. Check the working tree first "
            "(git status, git diff) so you do not redo or undo finished work. "
            "The original brief is in {brief}. Finish with the report the brief asks for.")


def thread_from(path, offset):
    try:
        f = open(path, errors="replace"); f.seek(offset)
        for line in f:
            line = line.strip()
            if line.startswith("{"):
                try:
                    o = json.loads(line)
                except ValueError:
                    continue
                if o.get("type") == "thread.started" and o.get("thread_id"):
                    return o["thread_id"]
    except OSError:
        pass
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--model", default="gpt-6.1-sol")
    ap.add_argument("--effort", default="xhigh")
    ap.add_argument("--read-only", action="store_true")
    ap.add_argument("--tries", type=int, default=8)
    ap.add_argument("--resume")
    ap.add_argument("--schema")
    ap.add_argument("--image", action="append", default=[])
    ap.add_argument("brief")
    a = ap.parse_args()
    codex = os.environ.get("CODEX_BIN", "codex")
    wait = float(os.environ.get("CODEX_RUN_BACKOFF", FIRST_WAIT))
    brief = os.path.abspath(a.brief)
    out = os.path.abspath(a.out)
    common = ["-m", a.model, "-c", f"model_reasoning_effort={a.effort}", "--skip-git-repo-check",
              "--json", "-o", out]
    if a.schema: common += ["--output-schema", a.schema]
    for img in a.image: common += ["-i", img]
    thread = a.resume
    rc = 1
    start = os.path.getsize(a.log) if os.path.exists(a.log) else 0
    for attempt in range(1, a.tries + 1):
        if os.path.exists(out): os.remove(out)
        if thread:
            cmd = [codex, "exec", "resume", thread, *common, CONTINUE.format(brief=brief)]
        else:
            mode = ["-s", "read-only"] if a.read_only else ["--approve-for-me"]
            cmd = [codex, "exec", *mode, "-C", a.dir, *common, "--", open(brief).read()]
        with open(a.log, "a") as log:
            log.write(f"\n=== codex_run attempt {attempt}/{a.tries} {'resume ' + thread if thread else 'fresh'} "
                      f"{time.strftime('%H:%M:%S')}\n")
            log.flush()
            rc = subprocess.run(cmd, cwd=a.dir, stdin=subprocess.DEVNULL, stdout=log,
                                stderr=subprocess.STDOUT).returncode
        thread = thread or thread_from(a.log, start)
        if rc == 0 and os.path.exists(out) and os.path.getsize(out) > 0:
            print(f"codex_run: done after {attempt} attempt(s), thread {thread}, report {out}")
            return 0
        print(f"codex_run: attempt {attempt} failed (exit {rc}); thread {thread or 'none'}", file=sys.stderr)
        if attempt < a.tries:
            time.sleep(wait)
            wait = min(wait * 2, MAX_WAIT)
    print(f"codex_run: gave up after {a.tries} attempts; resume with --resume {thread}", file=sys.stderr)
    return rc or 1


if __name__ == "__main__":
    sys.exit(main())
