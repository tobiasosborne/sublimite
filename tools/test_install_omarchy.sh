#!/bin/sh
# test_install_omarchy.sh - contract of `make install-omarchy` / `uninstall-omarchy`
# (edit-b6ai.11). Everything runs in a temporary HOME against a fake Omarchy tree
# whose omarchy-launch-editor / omarchy-launch-tui copy the logic of Omarchy 4's
# scripts (see docs/decisions/edit-b6ai.11.md); the real launcher, the real HOME
# and the real Omarchy state files are never touched. sublimite is a stub that
# records argv and blocks until released, so no display is needed.
# Usage: sh tools/test_install_omarchy.sh
# (P) timeouts are test-only caps against a hung stub: 100 polls x 0.05 s = 5 s to
# observe a stub start (a fork+exec chain of a few shell scripts is ms-scale, M not
# taken); the stub gives up after 200 x 0.05 s = 10 s so a failed test cannot leak.
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d) || exit 1
stub_pids=
cleanup() { touch "$work/stub/release" 2>/dev/null; rm -rf "$work"; }
trap cleanup EXIT HUP INT TERM
fails=0
pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; fails=$((fails + 1)); }
check() { # check "name" command...
    name=$1; shift
    if "$@" >/dev/null 2>&1; then pass "$name"; else fail "$name"; fi
}

home=$work/home; prefix=$work/prefix; om=$work/omarchy; fx=$work/fx; stubdir=$work/stub
mkdir -p "$home/.config" "$prefix/bin" "$om/bin" "$fx" "$stubdir"
chmod 700 "$work"

# ---- environment: nothing leaks to the real desktop ----
export HOME=$home XDG_CONFIG_HOME=$home/.config XDG_DATA_HOME=$home/.local/share
export XDG_STATE_HOME=$home/.local/state XDG_CACHE_HOME=$home/.cache
export XDG_RUNTIME_DIR=$work/run; mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
unset XDG_CURRENT_DESKTOP DESKTOP_SESSION KDE_FULL_SESSION GNOME_DESKTOP_SESSION_ID \
    DISPLAY WAYLAND_DISPLAY SUBLIMITE_OMARCHY_WAIT STUB_EXIT
export OMARCHY_PATH=$om PREFIX=$prefix STUB_DIR=$stubdir
export EDIT_REFUSE_REAL_DISPLAY=1 FX_DIR=$fx
# Fixtures call uwsm-app / the terminal by absolute path, and every stock TUI editor
# name is a stub, so a mistake here can never reach the real desktop launcher.

# ---- fake Omarchy: the launcher logic copied from Omarchy 4 alpha ----
cat >"$om/bin/omarchy-launch-editor" <<'EOF'
#!/bin/bash
default_editor="$HOME/.local/state/omarchy/defaults/editor"
if [[ ${1:-} == "--inline" ]]; then inline=true; shift; else inline=false; fi
if [[ -f $default_editor ]]; then read -r editor <"$default_editor"; else editor="nvim"; fi
omarchy-cmd-present "$editor" || editor="nvim"
case "${editor##*/}" in
nvim | vim | nano | micro | hx | helix | fresh)
  if [[ $inline == "true" ]]; then
    exec "$editor" -- "$@"
  else
    exec omarchy-launch-tui "$editor" -- "$@"
  fi
  ;;
*)
  exec setsid "$FX_DIR/uwsm-app" -- "$editor" -- "$@"
  ;;
esac
EOF
cat >"$om/bin/omarchy-launch-tui" <<'EOF'
#!/bin/bash
if [[ ${1:-} == --app-id=* ]]; then APP_ID="${1#--app-id=}"; shift
else APP_ID="org.omarchy.$(basename $1)"; fi
exec setsid "$FX_DIR/uwsm-app" -- "$FX_DIR/xdg-terminal-exec" --app-id=$APP_ID -e "$1" "${@:2}"
EOF
cat >"$om/bin/omarchy-cmd-present" <<'EOF'
#!/bin/bash
for cmd in "$@"; do command -v -- "$cmd" &>/dev/null || exit 1; done
exit 0
EOF
# uwsm-app: the real one hands the command to a daemon and returns at once.
cat >"$fx/uwsm-app" <<'EOF'
#!/bin/sh
shift   # --
"$@" </dev/null >/dev/null 2>&1 &
exit 0
EOF
# xdg-terminal-exec: stands in for the terminal emulator (foot -e): it keeps the
# --app-id on its own command line, runs the command as a child and waits.
cat >"$fx/xdg-terminal-exec" <<'EOF'
#!/bin/sh
while [ $# -gt 0 ]; do case $1 in -e) shift; break;; esac; shift; done
"$@"
EOF
cat >"$fx/omarchy-notification-send" <<'EOF'
#!/bin/sh
exit 0
EOF
# sublimite stub: records argv, appends "edited" to the last argument when released.
cat >"$prefix/bin/sublimite" <<'EOF'
#!/bin/sh
n=$(( $(ls "$STUB_DIR" | grep -c '^call\.') + 1 ))
f=$STUB_DIR/call.$n
printf '%s\n' "$*" >"$f.tmp"; mv "$f.tmp" "$f"
i=0
while [ ! -e "$STUB_DIR/release" ] && [ $i -lt 200 ]; do sleep 0.05; i=$((i + 1)); done
for a; do last=$a; done
[ -f "$last" ] && printf 'edited\n' >>"$last"
exit "${STUB_EXIT:-0}"
EOF
for e in nvim vim nano micro hx helix; do printf '#!/bin/sh\necho "fixture %s was run" >&2\nexit 97\n' "$e" >"$fx/$e"; done
chmod 755 "$om"/bin/* "$fx"/* "$prefix/bin/sublimite"
PATH=$om/bin:$fx:$PATH; export PATH
case $(command -v omarchy-launch-editor) in "$work"/*) ;; *) echo "test_install_omarchy: fixture launcher not first on PATH"; exit 1;; esac
script=$root/tools/install-omarchy.sh
helper=$prefix/libexec/sublimite/omarchy/fresh
editor_file=$home/.local/state/omarchy/defaults/editor
menu_file=$home/.config/omarchy/extensions/omarchy-menu.jsonc
mime_file=$home/.config/mimeapps.list

next_call() { # wait for call.N to appear; echo its contents
    i=0
    while [ ! -e "$stubdir/call.$1" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
    cat "$stubdir/call.$1" 2>/dev/null
}
reset_stub() { touch "$stubdir/release"; sleep 0.3; rm -rf "$stubdir"; mkdir -p "$stubdir"; }
snapshot() { # snapshot FILE -> content + cksum, or "absent"
    if [ -e "$1" ]; then cat "$1"; else echo ABSENT; fi
}

# ---- 0. plain install is untouched, the new targets exist ----
check "plain 'make install' names no omarchy step" sh -c "! make -C '$root' -n install PREFIX=/x | grep -qi omarchy"
check "plain 'make uninstall' names no omarchy step" sh -c "! make -C '$root' -n uninstall PREFIX=/x | grep -qi omarchy"
check "make install-omarchy is a target" sh -c "make -C '$root' -n install-omarchy PREFIX=/x | grep -q install-omarchy.sh"
check "make uninstall-omarchy is a target" sh -c "make -C '$root' -n uninstall-omarchy PREFIX=/x | grep -q install-omarchy.sh"
check "install-omarchy not a prerequisite of install" sh -c "! grep -E '^(install|uninstall):.*omarchy' '$root/Makefile'"

# ---- 1. no Omarchy: clear no-op ----
mkdir -p "$work/empty-om"
out=$(OMARCHY_PATH=$work/empty-om sh "$script" install 2>&1); rc=$?
[ $rc -eq 0 ] && pass "no Omarchy: exit 0" || fail "no Omarchy: exit $rc"
case $out in *"Omarchy not found"*) pass "no Omarchy: clear message";; *) fail "no Omarchy: message was: $out";; esac
check "no Omarchy: nothing written" sh -c "[ ! -e '$home/.local' ] && [ ! -e '$prefix/libexec' ]"
out=$(OMARCHY_PATH=$work/empty-om sh "$script" uninstall 2>&1); rc=$?
[ $rc -eq 0 ] && pass "no Omarchy: uninstall is a no-op (exit 0)" || fail "no Omarchy: uninstall exit $rc"

# ---- 2. pre-existing user state, then install ----
mkdir -p "$home/.local/state/omarchy/defaults" "$home/.config/omarchy/extensions"
printf 'nvim\n' >"$editor_file"
cp /usr/share/omarchy/config/omarchy/extensions/omarchy-menu.jsonc "$menu_file" 2>/dev/null || printf '{\n  // user extension\n}\n' >"$menu_file"
printf '[Default Applications]\ntext/plain=nvim.desktop;\ntext/x-c=nvim.desktop;\ninode/directory=nautilus.desktop;\n\n[Added Associations]\ntext/plain=nvim.desktop;\n' >"$mime_file"
mkdir -p "$prefix/share/applications"
cp "$root/tools/sublimite.desktop" "$prefix/share/applications/"
before_editor=$(snapshot "$editor_file"); before_menu=$(snapshot "$menu_file"); before_mime=$(snapshot "$mime_file")

out=$(sh "$script" install 2>&1); rc=$?
[ $rc -eq 0 ] && pass "install: exit 0" || { fail "install: exit $rc"; echo "$out"; echo "test_install_omarchy: install failed; behaviour sections skipped"; exit 1; }
check "install: wrapper is executable" test -x "$helper"
check "install: wrapper basename is a name the launcher runs inline" sh -c "grep -qw '${helper##*/}' '$om/bin/omarchy-launch-editor'"
check "install: defaults/editor names the wrapper" sh -c "[ \"\$(cat '$editor_file')\" = '$helper' ]"
check "install: wrapper points at \$PREFIX/bin/sublimite" grep -q "$prefix/bin/sublimite" "$helper"
check "install: menu row added" grep -q '"setup.default.editor.sublimite"' "$menu_file"
check "install: menu is valid JSON the way the shell strips it" python3 -I - "$menu_file" <<'EOF'
import json, re, sys
raw = open(sys.argv[1], encoding="utf-8").read()
raw = re.sub(r"^\s*//[^\n]*(\n|$)", "", raw, flags=re.M)
raw = re.sub(r",(\s*[}\]])", r"\1", raw)
d = json.loads(raw)
row = d["setup.default.editor.sublimite"]
assert row["label"] == "sublimité" and "action" in row and "checked" in row and "when" in row
assert row["action"].count("defaults/editor") >= 1
EOF
# The contract is the user's mimeapps.list (xdg-mime query also reads system files).
check "install: text/plain default is sublimite.desktop" grep -qx 'text/plain=sublimite.desktop' "$mime_file"
check "install: text/x-c default is sublimite.desktop" grep -qx 'text/x-c=sublimite.desktop' "$mime_file"
check "install: [Added Associations] left alone" grep -qx 'text/plain=nvim.desktop;' "$mime_file"
check "install: unrelated MIME entry untouched" grep -qx 'inode/directory=nautilus.desktop;' "$mime_file"
check "install: Omarchy's own state tree not otherwise changed" test "$(ls "$home/.local/state/omarchy/defaults")" = editor
out2=$(sh "$script" install 2>&1); rc=$?
[ $rc -eq 0 ] && pass "install twice: exit 0" || fail "install twice: exit $rc"
check "install twice: one menu row only" sh -c "[ \$(grep -c '\"setup.default.editor.sublimite\"' '$menu_file') -eq 1 ]"

# ---- 3. behaviour through the (fixture) Omarchy launcher ----
# 3a. $EDITOR users: inline blocks until the editor exits, then the file is edited.
rm -f "$work/msg"; printf 'subject\n' >"$work/msg"
( EDITOR='omarchy-launch-editor --inline'; sh -c '$EDITOR "$1"' sh "$work/msg"; echo $? >"$work/inline.rc" ) &
caller=$!
call=$(next_call 1)
[ "$call" = "--wait -- $work/msg" ] && pass "inline: sublimite got --wait -- FILE" || fail "inline: argv was '$call'"
sleep 0.3
if kill -0 "$caller" 2>/dev/null; then pass "inline: caller still blocked while the editor is open"; else fail "inline: caller returned early"; fi
[ ! -e "$work/inline.rc" ] && pass "inline: no exit status yet" || fail "inline: finished early"
touch "$stubdir/release"; wait "$caller"
[ "$(cat "$work/inline.rc" 2>/dev/null)" = 0 ] && pass "inline: returns 0 after the editor exits" || fail "inline: rc=$(cat "$work/inline.rc" 2>/dev/null)"
[ "$(cat "$work/msg")" = "subject
edited" ] && pass "inline: caller sees the edited file" || fail "inline: file is '$(cat "$work/msg")'"
reset_stub

# 3b. failure propagates.
( STUB_EXIT=3 omarchy-launch-editor --inline "$work/msg" >/dev/null 2>&1; echo $? >"$work/inline.rc" ) &
caller=$!; next_call 1 >/dev/null; touch "$stubdir/release"; wait "$caller"
[ "$(cat "$work/inline.rc")" = 3 ] && pass "inline: editor failure status propagates" || fail "inline: failure rc=$(cat "$work/inline.rc")"
reset_stub

# 3c. Omarchy menu / bindings: no --inline -> returns at once, editor opens without --wait.
start=$(date +%s)
timeout 5 omarchy-launch-editor "$work/msg" >/dev/null 2>&1; rc=$?
[ $rc -eq 0 ] && pass "non-inline: launcher returns 0 while the editor stays open" || fail "non-inline: launcher rc=$rc"
call=$(next_call 1)
[ "$call" = "-- $work/msg" ] && pass "non-inline: sublimite got -- FILE (no --wait)" || fail "non-inline: argv was '$call'"
reset_stub

# 3d. escape hatches.
SUBLIMITE_OMARCHY_WAIT=0 timeout 5 omarchy-launch-editor --inline "$work/msg" >/dev/null 2>&1; rc=$?
[ $rc -eq 0 ] && pass "SUBLIMITE_OMARCHY_WAIT=0: inline does not wait" || fail "SUBLIMITE_OMARCHY_WAIT=0: rc=$rc"
call=$(next_call 1); [ "$call" = "-- $work/msg" ] && pass "SUBLIMITE_OMARCHY_WAIT=0: no --wait" || fail "SUBLIMITE_OMARCHY_WAIT=0: argv '$call'"
reset_stub
( SUBLIMITE_OMARCHY_WAIT=1 omarchy-launch-editor "$work/msg" >/dev/null 2>&1; echo $? >"$work/inline.rc" ) &
caller=$!; call=$(next_call 1); sleep 0.3
case $call in "--wait "*) pass "SUBLIMITE_OMARCHY_WAIT=1: non-inline waits";; *) fail "SUBLIMITE_OMARCHY_WAIT=1: argv '$call'";; esac
touch "$stubdir/release"; wait "$caller"; reset_stub; rm -f "$work/inline.rc"

# 3e. the wrapper's own contract.
( "$helper" "$work/msg" >/dev/null 2>&1; echo $? >"$work/inline.rc" ) &
caller=$!; call=$(next_call 1)
[ "$call" = "--wait -- $work/msg" ] && pass "wrapper without leading --: argv normalised" || fail "wrapper: argv '$call'"
touch "$stubdir/release"; wait "$caller"; reset_stub

# ---- 4. uninstall restores the previous state exactly ----
out=$(sh "$script" uninstall 2>&1); rc=$?
[ $rc -eq 0 ] && pass "uninstall: exit 0" || { fail "uninstall: exit $rc"; echo "$out"; }
[ "$(snapshot "$editor_file")" = "$before_editor" ] && pass "uninstall: defaults/editor restored" || fail "uninstall: defaults/editor is '$(snapshot "$editor_file")'"
[ "$(snapshot "$menu_file")" = "$before_menu" ] && pass "uninstall: menu extension restored" || fail "uninstall: menu extension differs"
[ "$(snapshot "$mime_file")" = "$before_mime" ] && pass "uninstall: mimeapps.list restored" || fail "uninstall: mimeapps.list differs"
check "uninstall: wrapper and its directories gone" sh -c "[ ! -e '$prefix/libexec' ]"
check "uninstall: state directory gone" sh -c "[ ! -e '$home/.local/state/sublimite' ]"
check "uninstall: sublimite itself untouched" test -x "$prefix/bin/sublimite"
check "uninstall: desktop file untouched" test -f "$prefix/share/applications/sublimite.desktop"
out=$(sh "$script" uninstall 2>&1); rc=$?
[ $rc -eq 0 ] && pass "uninstall twice: exit 0" || fail "uninstall twice: exit $rc"

# ---- 5. nothing existed before: uninstall removes what install created ----
rm -rf "$home/.local" "$home/.config/omarchy" "$mime_file"
sh "$script" install >/dev/null 2>&1 || fail "fresh install failed"
check "fresh: defaults/editor written" test -f "$editor_file"
check "fresh: menu extension created" grep -q '"setup.default.editor.sublimite"' "$menu_file"
check "fresh: created menu file is plain JSON" python3 -I -c "
import json,re,sys
r=open('$menu_file',encoding='utf-8').read()
r=re.sub(r'^\s*//[^\n]*(\n|\$)','',r,flags=re.M)
json.loads(r)"
sh "$script" uninstall >/dev/null 2>&1 || fail "fresh uninstall failed"
check "fresh: everything removed again" sh -c "[ ! -e '$home/.local' ] && [ ! -e '$home/.config/omarchy' ] && [ ! -e '$mime_file' ] && [ ! -e '$prefix/libexec' ]"

# ---- 6. user changed things after install: uninstall leaves their choice ----
mkdir -p "$home/.local/state/omarchy/defaults" "$home/.config/omarchy/extensions"
printf 'nvim\n' >"$editor_file"
printf '{\n  // user\n  "personal": {"icon":"x","label":"P"}\n}\n' >"$menu_file"
printf '[Default Applications]\ntext/plain=nvim.desktop;\n' >"$mime_file"
sh "$script" install >/dev/null 2>&1 || fail "install before user edits failed"
printf 'code\n' >"$editor_file"                                   # Defaults > Editor > VSCode
printf '  // added later\n' >>"$menu_file"                        # user edits the menu file
printf 'application/pdf=evince.desktop;\n' >>"$mime_file"          # something else edits mimeapps
out=$(sh "$script" uninstall 2>&1); rc=$?
[ $rc -eq 0 ] && pass "edited: uninstall exit 0" || fail "edited: exit $rc"
[ "$(cat "$editor_file")" = code ] && pass "edited: a newer default editor choice is kept" || fail "edited: defaults/editor is '$(cat "$editor_file")'"
case $out in *"$editor_file"*"left"*|*"left"*"$editor_file"*) pass "edited: says what it left";; *) fail "edited: no note about defaults/editor: $out";; esac
check "edited: menu block removed, user lines kept" sh -c "! grep -q sublimite '$menu_file' && grep -q 'added later' '$menu_file' && grep -q personal '$menu_file'"
check "edited: MIME defaults rolled back, later entry kept" sh -c "grep -qx 'text/plain=nvim.desktop;' '$mime_file' && grep -qx 'application/pdf=evince.desktop;' '$mime_file' && ! grep -q sublimite '$mime_file'"
check "edited: wrapper removed" sh -c "[ ! -e '$prefix/libexec' ]"

# ---- 7. guards ----
rm -rf "$home/.local" "$home/.config/omarchy" "$mime_file"
mv "$prefix/bin/sublimite" "$prefix/bin/sublimite.off"
out=$(sh "$script" install 2>&1); rc=$?
[ $rc -ne 0 ] && pass "no sublimite under PREFIX: refuses" || fail "no sublimite under PREFIX: exit 0"
case $out in *"make install"*) pass "no sublimite under PREFIX: says to run make install";; *) fail "message: $out";; esac
mv "$prefix/bin/sublimite.off" "$prefix/bin/sublimite"
check "refused install changed nothing" sh -c "[ ! -e '$home/.local' ] && [ ! -e '$prefix/libexec' ]"
cp "$om/bin/omarchy-launch-editor" "$work/launcher.keep"
sed -i 's/ | fresh)/)/' "$om/bin/omarchy-launch-editor"
out=$(sh "$script" install 2>&1); rc=$?
[ $rc -ne 0 ] && pass "launcher without the inline name: refuses" || fail "launcher without the inline name: exit 0"
check "launcher without the inline name: nothing changed" sh -c "[ ! -e '$home/.local' ] && [ ! -e '$prefix/libexec' ]"
cp "$work/launcher.keep" "$om/bin/omarchy-launch-editor"

# ---- 8. optional: real sublimite --wait round trip on a private Xvfb ----
# Opt in with TEST_OMARCHY_REAL_DISPLAY=<explicit display number, never 0>.
if [ -n "${TEST_OMARCHY_REAL_DISPLAY:-}" ]; then
    sh "$root/tools/test_install_omarchy_real.sh" "$TEST_OMARCHY_REAL_DISPLAY" || fails=$((fails + 1))
fi

if [ $fails -eq 0 ]; then printf '%s\n' 'test_install_omarchy: install, wait semantics, uninstall and guards passed'; exit 0; fi
printf 'test_install_omarchy: %d failure(s)\n' "$fails"; exit 1
