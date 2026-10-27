#!/bin/sh
# Run from any directory: sh /absolute/WT/tools/test_bench_variant.sh
# Requires make -C WT build/bench/piece_bench. Tests the row contract and cleanup.
set -u
wt=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd) || exit 2
tmp=$(mktemp -d /tmp/piece_bench_contract.XXXXXX) || exit 2
foreign=$wt/build/variants/intree/foreign-test-$$
trap 'rm -rf "$tmp" "$foreign"' EXIT HUP INT TERM
mkdir -p "$foreign" || exit 2
printf 'worker output\n' > "$foreign/log"
mkdir "$tmp/empty-last"
printf 'x\n' > "$tmp/empty-last/log_1g.txt"
printf 'x\nx\nx\nx\nx\nx\nx\nx\nx\nx\n' > "$tmp/log_1g.txt"
fails=0
for mode in full quick empty-last; do
    target=9
    lines=11
    corpus=$tmp
    arg=
    if [ "$mode" = quick ]; then target=0; arg=--quick; fi
    if [ "$mode" = empty-last ]; then target=1; lines=2; corpus=$tmp/empty-last; fi
    (cd "$wt" && "$wt/build/bench/piece_bench" $arg --corpus="$corpus" --rows=line_jump_1e7) > "$tmp/$mode.log" 2>&1
    rc=$?
    if [ "$rc" -eq 0 ] && awk -v target="$target" -v lines="$lines" '
        /^BENCH / {
            delete f
            for (i=2; i<=NF; i++) { split($i, a, "="); f[a[1]]=substr($i, length(a[1])+2) }
            if (f["col"] == "line_count" && f["p50"] == lines) count=1
            if (f["col"] == "target_line" && f["p50"] == target && target < lines) exists=1
            if (f["col"] == "line_to_byte_cold" && f["gate"] == "none" && f["status"] == "TRACK") cold=1
            if (f["col"] == "line_to_byte_indexed" && f["gate"] == "p50<=30ms,p99<=50ms" && f["status"] == "PASS") warm=1
            if (f["col"] == "byte_to_line" && f["gate"] == "none" && f["status"] == "TRACK") bytes=1
        }
        END { exit !(count && exists && cold && warm && bytes) }
    ' "$tmp/$mode.log"; then
        echo "PASS: $mode line-jump target exists, cold TRACK, indexed G7j, byte_to_line TRACK"
    else
        echo "FAIL: $mode line-jump target/cell contract"
        fails=$((fails + 1))
    fi
done
if sh "$wt/tools/bench_variant.sh" src/piece --selftest > "$tmp/variant.log" 2>&1; then
    if [ -f "$foreign/log" ] && [ "$(cat "$foreign/log")" = 'worker output' ]; then
        echo 'PASS: bench_variant preserves foreign worker output'
    else
        echo 'FAIL: bench_variant deleted foreign worker output'
        fails=$((fails + 1))
    fi
else
    cat "$tmp/variant.log"
    echo 'FAIL: bench_variant build/selftest'
    fails=$((fails + 1))
fi
echo "test_bench_variant: $fails failure(s)"
[ "$fails" -eq 0 ]
