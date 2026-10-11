#!/bin/sh
# Full battery. Exits non-zero on any failure.
# Benches are tracked numbers, not gates (CLAUDE.md law 1): `make bench` is run
# on demand, never here.
set -e
cd "$(dirname "$0")/.."
printf 'battery: %s\n' "$(cat /sys/class/power_supply/BAT0/status 2>/dev/null || echo unknown)"
make clean
make -j"$(nproc)" all
sh tools/test_runtime_identity.sh build
make -j"$(nproc)" check
echo "ci: OK"
