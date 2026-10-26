#!/usr/bin/env bash
# winrun.sh <exe> [args...]: run a Windows .exe built in WSL through interop,
# IN PLACE at its build path (Tobias, 2026-10-25: /mnt/c is read-only by
# choice, nothing is copied to Windows %TEMP%). Runtime DLLs sit beside the exe
# (mk/win.mk copies them). Args pass through; stdout/stderr stream; the exe's
# exit code is returned. Repository cwd keeps relative fixture paths valid.
# Writes nothing. A test that needs NTFS scratch makes
# it itself as a Windows process under %TEMP%\sublimite-test-* and deletes it.
# Interop passes only WSLENV-listed variables: the os suite's selectors go through.
set -u
export WSLENV="OS_TEST_CASE:OS_TEST_DATA_REPARSE:SUBLIMITE_TEST_PYTHON:SUBLIMITE_TEST_CORPUS${WSLENV:+:$WSLENV}"
[ $# -ge 1 ] || { echo "usage: winrun.sh <exe> [args...]" >&2; exit 2; }
export SUBLIMITE_TEST_CORPUS="$(wslpath -w "${EDIT_CORPUS:-/tmp/edit-corpus}")"
exe=$(readlink -f "$1"); shift
[ -f "$exe" ] || { echo "winrun: no such file: $exe" >&2; exit 2; }
cd "$(dirname "$0")/.." || exit 2
exec "$exe" "$@"
