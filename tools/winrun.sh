#!/usr/bin/env bash
# winrun.sh <exe> [args...]: run a Windows .exe built in WSL through interop,
# IN PLACE from its build directory (Tobias, 2026-10-25: /mnt/c is read-only by
# choice, nothing is copied to Windows %TEMP%). Runtime DLLs sit beside the exe
# (mk/win.mk copies them). Args pass through; stdout/stderr stream; the exe's
# exit code is returned. Writes nothing. A test that needs NTFS scratch makes
# it itself as a Windows process under %TEMP%\sublimite-test-* and deletes it.
set -u
[ $# -ge 1 ] || { echo "usage: winrun.sh <exe> [args...]" >&2; exit 2; }
exe=$(readlink -f "$1"); shift
[ -f "$exe" ] || { echo "winrun: no such file: $exe" >&2; exit 2; }
cd "$(dirname "$exe")" || exit 2
exec "./$(basename "$exe")" "$@"
