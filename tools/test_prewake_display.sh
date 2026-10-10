#!/bin/sh
# Exercise coordinator opt-in on :99 only; check mode opens no window.
set -eu
cd "$(dirname "$0")/.."
exe=build/prewake/prewake-a
env DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_ALLOW_REAL_DISPLAY=1 "$exe" --display-check --real-display
if env -u EDIT_ALLOW_REAL_DISPLAY DISPLAY=:99 EDIT_DISPLAY=:99 "$exe" --display-check --real-display; then
    echo 'FAIL: real-display mode accepted without opt-in' >&2; exit 1
fi
if env DISPLAY=:99 EDIT_DISPLAY=:98 EDIT_ALLOW_REAL_DISPLAY=1 "$exe" --display-check --real-display; then
    echo 'FAIL: real-display mode accepted mismatched displays' >&2; exit 1
fi
if env DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_ALLOW_REAL_DISPLAY=1 "$exe" --display-check; then
    echo 'FAIL: default mode accepted real-display opt-in' >&2; exit 1
fi
env -u EDIT_ALLOW_REAL_DISPLAY DISPLAY=:99 EDIT_DISPLAY=:99 "$exe" --display-check
echo 'prewake display: explicit opt-in, matching displays, safe default PASS (no windows)'
