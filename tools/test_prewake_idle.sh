#!/bin/sh
# Regression: the experiment must measure no-hint idle rather than infer it.
set -eu
cd "$(dirname "$0")/.."
export DISPLAY=:99 EDIT_DISPLAY=:99
unset EDIT_ALLOW_REAL_DISPLAY
out=build/prewake/evidence-s8
mkdir -p "$out"
for variant in a b c; do
    printf 'I\n' | "build/prewake/prewake-$variant" --protocol > "$out/idle-$variant.log" 2>&1
    cat "$out/idle-$variant.log"
    rg '^G11 .*wakeups=0 hint_events=0 warmups=0 ' "$out/idle-$variant.log" > /dev/null
done
echo 'prewake idle: all variants zero no-hint UI wakeups'
