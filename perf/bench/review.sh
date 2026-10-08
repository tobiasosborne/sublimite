#!/bin/bash
# usage: review.sh <angle-name> <angle-instructions-file>
# Runs one codex exec gpt-6.1-sol xhigh reviewer over the perf-target doc, read-only, writes review to perf/.
set -u
NAME="$1"; ANGLE="$2"
PROJ=/home/tobias/Projects/editor
OUT="$PROJ/perf/02-review-$NAME.md"
PROMPT="$(cat <<P
You are an adversarial technical reviewer of a performance-target document for a new native text editor.
Files (read-only; read them in full first):
  $PROJ/perf/00-hardware-profile.md   -- measured hardware profile of Target A (this laptop) + method notes
  $PROJ/perf/01-perf-target.md        -- the derivation under review (v3.3); also read 02-review-verify3.md (round 3) and the addenda in 00-hardware-profile.md

The author was told: measured on-battery numbers are binding (pessimistic is desired); every formula must be redoable on paper; bounds should be conservative but not fake-precise; each bound must state the hardware resource that binds it and the design decision it forces.

Your review angle: $(cat "$ANGLE")

Method: redo every piece of arithmetic you check with python (you have a shell; do NOT modify any file under $PROJ). For each finding give: location (section/operation), the claim, your recomputation or counter-argument, severity (BLOCKER = the number or conclusion is wrong and would mislead the build; MAJOR = materially loose/tight or missing a binding constraint; MINOR = presentation/precision), and the concrete fix. Then list anything the document got RIGHT that a less careful reviewer might wrongly flag (so the author does not over-correct). End with a one-paragraph verdict: is the set of ship-gate numbers safe to build against, yes/no, and which gates you would change and to what.

Be specific and numeric. No praise padding. Output Markdown only; it will be saved verbatim as the review file.
P
)"
cd "$PROJ" && codex exec -m gpt-6.1-sol -c model_reasoning_effort=xhigh -s read-only --skip-git-repo-check -o "$OUT" "$PROMPT" >"/tmp/claude-1000/-home-tobias-Projects-editor/512dfaf7-9262-485a-8091-e67909fda768/scratchpad/review-$NAME.log" 2>&1
echo "exit=$? -> $OUT"
