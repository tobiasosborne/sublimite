#!/usr/bin/env bash
# Explicit, share-dependent measurement. Deliberately outside make check.
set -euo pipefail
cd "$(dirname "$0")/.."
exec taskset -c 48-63 nice -n 10 python3 tools/wsl_share_probe.py "$@"
