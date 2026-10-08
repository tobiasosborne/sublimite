#!/bin/sh
# Full battery. Exits non-zero on any failure.
set -e
cd "$(dirname "$0")/.."
printf 'battery: %s\n' "$(cat /sys/class/power_supply/BAT0/status 2>/dev/null || echo unknown)"
make clean
make -j"$(nproc)" all
make -j"$(nproc)" check
make bench
echo "ci: OK"
