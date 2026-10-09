#!/bin/sh
# P3.6 only. Never changes Makefile/src or runs on the real display.
set -eu
cd "$(dirname "$0")/.."
export DISPLAY=:99 EDIT_DISPLAY=:99
unset EDIT_ALLOW_REAL_DISPLAY
mkdir -p variants/P3.6/common
case "${1:-help}" in
build|selftest)
    gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -Isrc bench/linkorder_bench.c -o variants/P3.6/common/linkorder_bench
    if [ "$1" = selftest ]; then exec variants/P3.6/common/linkorder_bench --selftest; fi
    python3 variants/P3.6/common/build.py profile
    cat /sys/class/power_supply/BAT0/status
    cut -d' ' -f1 /proc/loadavg
    variants/P3.6/common/linkorder_bench --train variants/P3.6/b/profile
    python3 variants/P3.6/common/build.py order
    python3 variants/P3.6/common/build.py release
    python3 variants/P3.6/common/build.py verify
    ;;
run)
    cat /sys/class/power_supply/BAT0/status
    cut -d' ' -f1 /proc/loadavg
    exec variants/P3.6/common/linkorder_bench --run "${2:-200}"
    ;;
*) echo 'usage: tools/linkorder_bench.sh build | selftest | run [launches-per-variant]' >&2; exit 2 ;;
esac
