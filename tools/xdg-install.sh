#!/bin/sh
# Explicit user action only. Never invoked by tests or the build.
set -eu
if [ "${1:-}" = "--help" ]; then
    printf '%s\n' 'Usage: tools/xdg-install.sh' 'Installs edit.desktop and sets user MIME defaults. Put edit on PATH first.'
    exit 0
fi
if [ "$#" -ne 0 ]; then
    printf '%s\n' 'Usage: tools/xdg-install.sh' >&2
    exit 2
fi
: "${HOME:?HOME is required}"
command -v edit >/dev/null 2>&1 || { printf '%s\n' 'Put the edit executable on PATH before installing.' >&2; exit 1; }
command -v xdg-mime >/dev/null 2>&1 || { printf '%s\n' 'xdg-mime is required.' >&2; exit 1; }
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
data_dir=${XDG_DATA_HOME:-"$HOME/.local/share"}
case "$data_dir" in /*) ;; *) printf '%s\n' 'XDG_DATA_HOME must be absolute.' >&2; exit 1;; esac
mkdir -p "$data_dir/applications"
install -m 644 "$script_dir/edit.desktop" "$data_dir/applications/edit.desktop"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$data_dir/applications"
fi
for mime in text/plain text/x-c text/x-c++src text/x-chdr text/x-python text/x-shellscript application/json application/xml text/markdown; do
    xdg-mime default edit.desktop "$mime"
done
printf '%s\n' "Installed $data_dir/applications/edit.desktop and registered user MIME defaults."
