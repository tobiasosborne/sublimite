#!/usr/bin/env python3
"""PostToolUse hook: stop a Haiku/Sonnet worker when its token budget is spent (P0.7).

Run as `python3 -I tools/hooks/token_budget.py` (stdin = hook JSON), or
`python3 -I tools/hooks/token_budget.py --dry-run <transcript.jsonl>`.

Measure: the context size of the LATEST assistant message in the agent's transcript,
input + cache_creation + cache_read + output (see docs/decisions/P0.7.md).
Fails open: any error, or below threshold, prints nothing and exits 0.
"""
import json
import os
import sys

MSG = ("Token budget reached ({used}/{limit}): stop now. Make the tree build, "
       "write STATUS.md in your module, report what is missing.")
DEFAULTS = {"haiku": 80000, "sonnet": 160000}
ENV = {"haiku": "EDIT_BUDGET_HAIKU", "sonnet": "EDIT_BUDGET_SONNET"}
TAIL_START = 256 * 1024
MAX_SCAN = 64 * 1024 * 1024


def family(model):
    m = (model or "").lower()
    if "haiku" in m:
        return "haiku"
    if "sonnet" in m:
        return "sonnet"
    return None  # opus, fable, unknown: never triggers


def limit_for(fam):
    try:
        return int(os.environ.get(ENV[fam], DEFAULTS[fam]))
    except (ValueError, KeyError):
        return DEFAULTS[fam]


def latest_assistant(path):
    """Scan the file backwards, reading growing tail chunks, for the last assistant line with usage."""
    with open(path, "rb") as fh:
        fh.seek(0, os.SEEK_END)
        size = fh.tell()
        chunk = TAIL_START
        while True:
            start = max(0, size - chunk)
            fh.seek(start)
            data = fh.read(size - start)
            lines = data.split(b"\n")
            if start > 0:
                lines = lines[1:]  # first line may be partial
            for raw in reversed(lines):
                if b'"assistant"' not in raw:
                    continue
                try:
                    row = json.loads(raw)
                except ValueError:
                    continue
                if row.get("type") != "assistant":
                    continue
                msg = row.get("message") or {}
                if isinstance(msg.get("usage"), dict):
                    return msg.get("model"), msg["usage"]
            if start == 0 or chunk >= MAX_SCAN:
                return None
            chunk *= 4


def context_tokens(usage):
    total = 0
    for key in ("input_tokens", "cache_creation_input_tokens",
                "cache_read_input_tokens", "output_tokens"):
        val = usage.get(key, 0)
        if not isinstance(val, int) or isinstance(val, bool):
            raise ValueError(key)
        total += val
    return total


def measure(path):
    """Return (family, used) or None when unknown."""
    found = latest_assistant(path)
    if found is None:
        return None
    model, usage = found
    return model, context_tokens(usage)


def resolve_transcript(payload):
    """Pick the transcript of the agent making the call (see docs/decisions/P0.7.md)."""
    agent_path = payload.get("agent_transcript_path")
    if isinstance(agent_path, str) and os.path.exists(agent_path):
        return agent_path
    path = payload.get("transcript_path")
    agent_id = payload.get("agent_id")
    if isinstance(path, str) and isinstance(agent_id, str) and agent_id:
        base = os.path.splitext(path)[0]
        cand = os.path.join(base, "subagents", "agent-" + agent_id + ".jsonl")
        if os.path.exists(cand):
            return cand
    return path if isinstance(path, str) else None


def main(argv):
    if os.environ.get("EDIT_BUDGET_OFF") == "1":
        return 0
    if len(argv) >= 2 and argv[1] == "--dry-run":
        path = argv[2]
        res = measure(path)
        if res is None:
            print("model=none used=0 limit=none trigger=no")
            return 0
        model, used = res
        fam = family(model)
        if fam is None:
            print("model=%s used=%d limit=none trigger=no" % (model, used))
            return 0
        lim = limit_for(fam)
        print("model=%s used=%d limit=%d trigger=%s" %
              (model, used, lim, "yes" if used >= lim else "no"))
        return 0
    # normal hook mode
    payload = json.loads(sys.stdin.read() or "null")
    if not isinstance(payload, dict):
        return 0
    path = resolve_transcript(payload)
    if not path:
        return 0
    res = measure(path)
    if res is None:
        return 0
    model, used = res
    fam = family(model)
    if fam is None:
        return 0
    lim = limit_for(fam)
    if used < lim:
        return 0
    out = {"hookSpecificOutput": {
        "hookEventName": "PostToolUse",
        "additionalContext": MSG.format(used=used, limit=lim)}}
    sys.stdout.write(json.dumps(out))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except Exception:
        sys.exit(0)  # fail open
