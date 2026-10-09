#!/bin/sh
# Release artifact and desktop contract; run after make all in a fresh build tree.
set -eu
b=${1:-build}
test -x "$b/sublimite" || { echo 'FAIL: make all did not produce build/sublimite'; exit 1; }
test ! -e "$b/edit" || { echo 'FAIL: legacy build/edit exists'; exit 1; }
test ! -e tools/edit.desktop
test -f tools/sublimite.desktop
grep -q '^Name=sublimité$' tools/sublimite.desktop
grep -q '^Exec=sublimite %F$' tools/sublimite.desktop
grep -q '^StartupWMClass=sublimite$' tools/sublimite.desktop
set +e
"$b/sublimite" 2>"$b/runtime-usage.txt"
rc=$?
set -e
test "$rc" -eq 2
grep -q '^usage: sublimite <file>$' "$b/runtime-usage.txt"
echo 'test_runtime_identity: binary, usage and desktop passed'
# Exercise the installer with isolated XDG storage and stubbed desktop commands.
work=$(mktemp -d "$b/xdg-install-XXXXXX")
work=$(CDPATH= cd "$work" && pwd)
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/bin" "$work/data"
cat > "$work/bin/xdg-mime" <<'STUB'
#!/bin/sh
printf '%s\n' "$*" >> "$SUBLIMITE_TEST_MIME_LOG"
STUB
cat > "$work/bin/update-desktop-database" <<'STUB'
#!/bin/sh
exit 0
STUB
cp "$work/bin/update-desktop-database" "$work/bin/sublimite"
chmod +x "$work/bin/xdg-mime" "$work/bin/update-desktop-database" "$work/bin/sublimite"
XDG_DATA_HOME="$work/data" SUBLIMITE_TEST_MIME_LOG="$work/mime.log" PATH="$work/bin:$PATH" sh tools/xdg-install.sh
cmp tools/sublimite.desktop "$work/data/applications/sublimite.desktop"
grep -q '^default sublimite.desktop text/plain$' "$work/mime.log"
if grep -v '^default sublimite.desktop ' "$work/mime.log"; then
    echo 'FAIL: legacy or unexpected MIME default'; exit 1
fi
echo 'test_runtime_identity: isolated XDG desktop installation passed'
