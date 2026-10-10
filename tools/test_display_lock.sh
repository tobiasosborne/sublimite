#!/bin/sh
# Reproduce shared-Xvfb interference, then exercise the protected lane.
set -eu
cd "$(dirname "$0")/.."
mode=${1:-locked}
binary=${2:-build/san/tests/refwin_test}
out=build/edit-5cv-evidence
mkdir -p "$out"
export DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0
if [ "$mode" = contract ]; then
    # A ready marker synchronizes the holder; no timing-based overlap assertion.
    out=$(mktemp -d "$out/lock-contract-XXXXXX")
    ready="$out/holder-ready"
    release="$out/holder-release"
    sh tools/with_display_lock.sh sh -c '
        echo $$ > "$1"
        while [ ! -f "$2" ]; do sleep 0.05; done
    ' sh "$ready" "$release" > "$out/holder.log" 2>&1 &
    holder=$!
    trap 'touch "$release"; wait "$holder" || true' EXIT HUP INT TERM
    while [ ! -f "$ready" ]; do
        if ! kill -0 "$holder" 2>/dev/null; then
            cat "$out/holder.log"; exit 1
        fi
        sleep 0.05
    done
    owner=$(cat "$ready")
    for target in wrapper refwin make prewake-display prewake-idle zygote gl; do
        rc=0; expected=1
        case "$target" in
            wrapper) set -- sh tools/with_display_lock.sh true ;;
            refwin) set -- "$binary" ;;
            make) set -- make --no-print-directory check TEST_SAN=build/san/tests/x11_identity_test; expected=2 ;;
            prewake-display) set -- sh tools/test_prewake_display.sh ;;
            prewake-idle) set -- sh tools/test_prewake_idle.sh ;;
            zygote) set -- sh tools/zygote_bench.sh --self-check --launches 1 ;;
            gl) set -- sh tools/test_gl_optional_native.sh ;;
        esac
        DISPLAY=unix:99.0 EDIT_DISPLAY_LOCK_TIMEOUT=0.1 "$@" > "$out/timeout-$target.log" 2>&1 || rc=$?
        cat "$out/timeout-$target.log"
        if [ "$rc" != "$expected" ]; then
            echo "display lock contract: FAIL $target expected exit $expected, actual $rc"
            exit 1
        fi
        if ! rg -F "waiting for display :99 (held by $owner)" "$out/timeout-$target.log" > /dev/null ||
           ! rg -F "display lock timeout: display :99 (held by $owner)" "$out/timeout-$target.log" > /dev/null; then
            echo "display lock contract: FAIL $target missing holder wait/timeout diagnostics"
            exit 1
        fi
    done
    touch "$release"; wait "$holder"
    trap - EXIT HUP INT TERM
    # Nested scripts must reuse the inherited descriptor, and preserve exits.
    EDIT_DISPLAY_LOCK_TIMEOUT=0.1 sh tools/with_display_lock.sh sh tools/with_display_lock.sh true
    rc=0
    sh tools/with_display_lock.sh sh -c 'exit 7' || rc=$?
    test "$rc" = 7
    echo 'display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS'
    exit 0
fi
case "$mode" in unlocked|locked|direct) ;; *) exit 2;; esac
failed=0
for round in 1 2 3 4 5; do
    if [ "$mode" = locked ]; then
        sh tools/with_display_lock.sh "$binary" > "$out/$mode-$round-a.log" 2>&1 &
        first=$!
        sh tools/with_display_lock.sh "$binary" > "$out/$mode-$round-b.log" 2>&1 &
    else
        "$binary" > "$out/$mode-$round-a.log" 2>&1 &
        first=$!
        "$binary" > "$out/$mode-$round-b.log" 2>&1 &
    fi
    second=$!
    a=0; b=0
    wait "$first" || a=$?
    wait "$second" || b=$?
    printf '%s round %s: exits %s %s\n' "$mode" "$round" "$a" "$b"
    if [ "$a" != 0 ] || [ "$b" != 0 ]; then
        failed=1
        cat "$out/$mode-$round-a.log" "$out/$mode-$round-b.log"
    fi
done
exit "$failed"
