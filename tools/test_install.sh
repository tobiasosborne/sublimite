#!/bin/sh
# Install contract uses private XDG storage; no user MIME preferences are changed.
# (P) exit statuses and file modes are POSIX conventions, not measured budgets.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
prefix="$work/prefix"
export XDG_DATA_HOME="$prefix/share" XDG_CONFIG_HOME="$work/config"
export XDG_CACHE_HOME="$work/cache" XDG_RUNTIME_DIR="$work/runtime"
export EDIT_REFUSE_REAL_DISPLAY=1
mkdir -p "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export PATH="$prefix/bin:$PATH"
make -j3 install PREFIX="$prefix" # (P) shared-box parallelism from bead brief.
test -x "$prefix/bin/sublimite"
test -x "$prefix/bin/tracedump"
cmp build/sublimite "$prefix/bin/sublimite"
cmp build/tools/tracedump "$prefix/bin/tracedump"
desktop="$prefix/share/applications/sublimite.desktop"
test -f "$desktop"
test -s "$prefix/share/icons/hicolor/scalable/apps/sublimite.svg"
cmp tools/sublimite.desktop "$desktop"
grep -qx 'Exec=sublimite %F' "$desktop"
grep -qx 'TryExec=sublimite' "$desktop"
grep -qx 'Icon=sublimite' "$desktop"
if command -v desktop-file-validate >/dev/null; then
    desktop-file-validate "$desktop"
fi
"$prefix/bin/sublimite" --help
if command -v xdg-mime >/dev/null; then
    test "$(xdg-mime query default text/plain)" = sublimite.desktop
fi
make -j3 uninstall PREFIX="$prefix" # (P) same shared-box parallelism.
test ! -e "$prefix/bin/sublimite"
test ! -e "$prefix/bin/tracedump"
test ! -e "$desktop"
test ! -e "$prefix/share/icons/hicolor/scalable/apps/sublimite.svg"
printf '%s\n' 'test_install: release binaries, desktop validation, icon, help, MIME default and uninstall passed'
