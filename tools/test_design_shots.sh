#!/bin/sh
# tools/test_design_shots.sh [binary]: runs tools/design_shots.sh twice, checks
# the manifest (16 entries = 8 scenes x dark/light), that each PNG exists and
# has more than one colour, and that the two runs are byte-identical.
# Uses display :91 (private Xvfb started by design_shots.sh).
set -eu
cd "$(dirname "$0")/.."
bin=${1:-build/sublimite}
a=$(mktemp -d); b=$(mktemp -d)
trap 'rm -rf "$a" "$b"' EXIT HUP INT TERM
sh tools/design_shots.sh "$bin" "$a"
sh tools/design_shots.sh "$bin" "$b"
fail=0
# (P) 8 scenes x 2 themes, from the scene table in design_shots.sh.
want=16
got=$(grep -c '"scene"' "$a/manifest.json")
[ "$got" -eq "$want" ] || { echo "FAIL manifest has $got entries, want $want"; fail=1; }
for f in "$a"/*-dark.png "$a"/*-light.png; do
    [ -f "$f" ] || { echo "FAIL missing $f"; fail=1; continue; }
    colours=$(identify -format %k "$f")
    [ "$colours" -gt 1 ] || { echo "FAIL single colour: $f"; fail=1; }
done
for f in "$a"/*-dark.png "$a"/*-light.png; do
    cmp -s "$f" "$b/$(basename "$f")" || { echo "FAIL differs between runs: $(basename "$f")"; fail=1; }
done
sha256sum "$a"/*-dark.png "$a"/*-light.png | sed "s|$a/||"
[ $fail -eq 0 ] && echo "test_design_shots: PASS" || { echo "test_design_shots: FAIL"; exit 1; }
