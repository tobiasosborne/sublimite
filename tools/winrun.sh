#!/usr/bin/env bash
# winrun.sh <exe> [args...]: run a Windows .exe built in WSL through interop,
# IN PLACE at its build path (Tobias, 2026-10-25: /mnt/c is read-only by
# choice, nothing is copied to Windows %TEMP%). Runtime DLLs sit beside the exe
# (mk/win.mk copies them). Args pass through; stdout/stderr stream; the exe's
# exit code is returned. Repository cwd keeps relative fixture paths valid.
# Writes nothing. A test that needs NTFS scratch makes
# it itself as a Windows process under %TEMP%\sublimite-test-* and deletes it.
# Interop passes only WSLENV-listed variables: the os suite's selectors go through.
#
# The exe runs through build/win/tools/win_quiet_run.exe on a private window
# station: invisible, no focus, its own clipboard, below-normal priority, children
# killed with the run (tools/win_quiet_run.c; Tobias, 2026-11-01, he uses this box).
# WINRUN_ISOLATION=station (default) | desktop (private desktop, shared clipboard).
# WINRUN_ISOLATION=none runs on the live desktop: only by hand, with Tobias's word.
set -u
export WINRUN_ISOLATION="${WINRUN_ISOLATION:-station}"
export WSLENV="WINRUN_ISOLATION:OS_TEST_CASE:OS_TEST_DATA_REPARSE:SUBLIMITE_TEST_PYTHON:SUBLIMITE_TEST_CORPUS${WSLENV:+:$WSLENV}"
[ $# -ge 1 ] || { echo "usage: winrun.sh <exe> [args...]" >&2; exit 2; }
export SUBLIMITE_TEST_CORPUS="$(wslpath -w "${EDIT_CORPUS:-/tmp/edit-corpus}")"
exe=$(readlink -f "$1"); shift
[ -f "$exe" ] || { echo "winrun: no such file: $exe" >&2; exit 2; }
cd "$(dirname "$0")/.." || exit 2
[ "$WINRUN_ISOLATION" = none ] && exec "$exe" "$@"
quiet=$(readlink -f build/win/tools/win_quiet_run.exe)
[ -f "$quiet" ] || { echo "winrun: $quiet missing (make TARGET=win all); refusing to run on the live desktop" >&2; exit 2; }
exec "$quiet" "$(wslpath -w "$exe")" "$@"
