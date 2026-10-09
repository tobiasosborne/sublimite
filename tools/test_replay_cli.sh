#!/bin/sh
# test_replay_cli.sh - tools/replay --speed validation at the command line (P0.6c).
# Bad speeds (inf, nan, 0, -1) must exit 2 and print a message naming --speed on
# stderr; a good speed must exit 0. Needs a dump: built by build/san/tests/replay_test.
# Usage: sh tools/test_replay_cli.sh [build_dir]   (default: <repo>/build)
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
b=${1:-$root/build}
tool=$b/tools/replay
gen=$b/san/tests/replay_test
work=$b/replay_cli_tmp
fails=0

mkdir -p "$work" || exit 1
dump=$work/session.dump
if ! "$gen" "$dump" >/dev/null; then
    echo "FAIL: cannot write session dump with $gen"
    exit 1
fi

check_bad() {
    spd=$1
    "$tool" "--speed=$spd" "$dump" >/dev/null 2>"$work/err"
    rc=$?
    if [ "$rc" -ne 2 ]; then
        echo "FAIL: --speed=$spd exit $rc, want 2"; fails=$((fails + 1))
    elif ! grep -q -- '--speed' "$work/err"; then
        echo "FAIL: --speed=$spd gave no clear message (stderr: $(cat "$work/err"))"
        fails=$((fails + 1))
    else
        echo "ok:   --speed=$spd rc=2 $(cat "$work/err")"
    fi
}

for s in inf nan 0 -1; do check_bad "$s"; done

"$tool" --speed=2 "$dump" >/dev/null 2>"$work/err"
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "FAIL: --speed=2 exit $rc, want 0 ($(cat "$work/err"))"; fails=$((fails + 1))
else
    echo "ok:   --speed=2 rc=0"
fi

if [ "$fails" -ne 0 ]; then
    echo "test_replay_cli: $fails failure(s)"
    exit 1
fi
echo "test_replay_cli: all passed"
