#!/usr/bin/env bash
# Checks for tools/hooks/token_budget.py (P0.7). Run: bash tools/hooks/test_token_budget.sh
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HOOK="$ROOT/tools/hooks/token_budget.py"
TD="$ROOT/tools/hooks/testdata"
REAL=/home/tobias/.claude/projects/-home-tobias-Projects-editor/b66f98cd-1196-47bb-b593-d34bba8ac6d6/subagents/agent-a66abaacf007e9cf9.jsonl
MSG_FMT='Token budget reached (%s/%s): stop now. Make the tree build, write STATUS.md in your module, report what is missing.'
FAILS=0
ok()   { echo "PASS $1"; }
bad()  { echo "FAIL $1"; FAILS=$((FAILS+1)); }

dry()  { python3 -I "$HOOK" --dry-run "$1" 2>&1; }
# hook stdin for a PostToolUse event against transcript $1
stdin_for() { printf '{"session_id":"t","hook_event_name":"PostToolUse","tool_name":"Edit","tool_input":{"file_path":"x"},"transcript_path":"%s"}' "$1"; }
run_hook() { # $1 = transcript, extra env passed by caller via env
  stdin_for "$1" | python3 -I "$HOOK" 2>/dev/null; echo "rc=$?"; }
ctx_of() { python3 -I -c 'import json,sys
d=sys.stdin.read().strip()
if not d: print("<none>"); sys.exit()
print(json.loads(d)["hookSpecificOutput"]["additionalContext"])' 2>/dev/null || echo "<bad-json>"; }

# 1. dry run on the real Haiku subagent transcript: model and total, no trigger
if [ -f "$REAL" ]; then
  out=$(dry "$REAL"); echo "$out"
  case "$out" in *"model=claude-haiku-5-5"*"trigger=no"*) ok "real haiku transcript: model + no trigger";; *) bad "real haiku transcript: $out";; esac
  used=$(echo "$out" | sed -n 's/.*used=\([0-9]*\).*/\1/p')
  if [ -n "$used" ] && [ "$used" -ge 53000 ] && [ "$used" -le 58000 ]; then ok "real transcript total ~55,770 (got $used)"; else bad "real transcript total (got '$used', want ~55770)"; fi
else
  echo "SKIP real transcript (not present)"
fi

# 2. synthetic over-budget Haiku triggers
out=$(dry "$TD/haiku_over.jsonl"); echo "$out"
case "$out" in *"model=claude-haiku-5-5 used=86052 limit=80000 trigger=yes"*) ok "haiku over budget triggers (latest message only, not a sum)";; *) bad "haiku over: $out";; esac

# 3. synthetic under-budget Haiku does not trigger
out=$(dry "$TD/haiku_under.jsonl"); echo "$out"
case "$out" in *"trigger=no"*) ok "haiku under budget does not trigger";; *) bad "haiku under: $out";; esac

# 4. Sonnet 160K limit
out=$(dry "$TD/sonnet_over.jsonl"); echo "$out"
case "$out" in *"model=claude-sonnet-4-6 used=167402 limit=160000 trigger=yes"*) ok "sonnet over 160K triggers";; *) bad "sonnet over: $out";; esac

# 5. Opus over 200K never triggers
out=$(dry "$TD/opus_over.jsonl"); echo "$out"
case "$out" in *"model=claude-opus-5-5"*"limit=none trigger=no"*) ok "opus over 200K does not trigger";; *) bad "opus over: $out";; esac

# 6. Normal mode emits the exact stop message for Haiku
out=$(stdin_for "$TD/haiku_over.jsonl" | python3 -I "$HOOK" 2>/dev/null); echo "emitted: $out"
want=$(printf "$MSG_FMT" 86052 80000)
got=$(echo "$out" | ctx_of)
if [ "$got" = "$want" ]; then ok "normal mode haiku emits exact stop message"; else bad "normal mode haiku: got [$got]"; fi

# 7. Normal mode silent for opus, under-budget haiku, sonnet under limit
for f in opus_over.jsonl haiku_under.jsonl; do
  out=$(stdin_for "$TD/$f" | python3 -I "$HOOK" 2>/dev/null; echo "rc=$?")
  case "$out" in "rc=0") ok "silent for $f";; *) bad "not silent for $f: $out";; esac
done

# 8. EDIT_BUDGET_OFF=1 disables everything
out=$(EDIT_BUDGET_OFF=1 bash -c "stdin_for() { printf '{\"transcript_path\":\"%s\"}' \"\$1\"; }; stdin_for '$TD/haiku_over.jsonl' | python3 -I '$HOOK' 2>/dev/null; echo rc=\$?")
case "$out" in "rc=0") ok "EDIT_BUDGET_OFF=1 is a no-op";; *) bad "EDIT_BUDGET_OFF: $out";; esac

# 9. Threshold overrides via env
out=$(EDIT_BUDGET_HAIKU=90000 python3 -I "$HOOK" --dry-run "$TD/haiku_over.jsonl" 2>&1); echo "$out"
case "$out" in *"limit=90000 trigger=no"*) ok "EDIT_BUDGET_HAIKU override honoured";; *) bad "haiku override: $out";; esac

# 10. Fail open: missing file, garbage stdin, empty stdin, missing transcript_path
out=$(stdin_for /nonexistent/x.jsonl | python3 -I "$HOOK" 2>/dev/null; echo "rc=$?")
case "$out" in "rc=0") ok "missing transcript fails open";; *) bad "missing transcript: $out";; esac
out=$(echo 'not json{' | python3 -I "$HOOK" 2>/dev/null; echo "rc=$?")
case "$out" in "rc=0") ok "garbage stdin fails open";; *) bad "garbage stdin: $out";; esac
out=$(printf '' | python3 -I "$HOOK" 2>/dev/null; echo "rc=$?")
case "$out" in "rc=0") ok "empty stdin fails open";; *) bad "empty stdin: $out";; esac
out=$(echo '{"tool_name":"Edit"}' | python3 -I "$HOOK" 2>/dev/null; echo "rc=$?")
case "$out" in "rc=0") ok "no transcript_path fails open";; *) bad "no transcript_path: $out";; esac

echo
if [ "$FAILS" -eq 0 ]; then echo "ALL PASS"; else echo "$FAILS FAILED"; fi
exit "$FAILS"
