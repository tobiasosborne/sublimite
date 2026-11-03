#!/bin/sh
# Real-binary round trip for edit-b6ai.11: the installed Omarchy wrapper runs the
# built sublimite --wait on a private Xvfb, blocks while the tab is open and
# returns 0 after Ctrl+W. Opt-in (needs build/sublimite, Xvfb, xdotool):
#   TEST_OMARCHY_REAL_DISPLAY=96 sh tools/test_install_omarchy.sh
# or directly: sh tools/test_install_omarchy_real.sh 96
# The display number is explicit and never 0 (a bare Xvfb would take the live :0).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
n=${1:-}
case $n in ''|0|*[!0-9]*) echo "usage: $0 <explicit display number, not 0>" >&2; exit 2;; esac
[ -x "$root/build/sublimite" ] || { echo "SKIP real round trip: build/sublimite missing (make all)"; exit 0; }
for c in Xvfb xdotool; do command -v $c >/dev/null 2>&1 || { echo "SKIP real round trip: $c missing"; exit 0; }; done
[ ! -e "/tmp/.X11-unix/X$n" ] || { echo "real round trip: display :$n is in use; pick another" >&2; exit 2; }
real_home=$HOME   # the xdotool shim on this box resolves its libraries through $HOME
xdo() { HOME=$real_home xdotool "$@"; }
work=$(mktemp -d) || exit 1
xpid=
# The UI process outlives its last tab; stop exactly the copy this test started.
cleanup() {
    for p in /proc/[0-9]*; do
        [ "$(readlink "$p/exe" 2>/dev/null)" = "$work/prefix/bin/sublimite" ] && kill "${p#/proc/}" 2>/dev/null
    done
    [ -n "$xpid" ] && kill "$xpid" 2>/dev/null
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM
mkdir -p "$work/prefix/bin" "$work/prefix/share/applications" "$work/om/bin" "$work/home" "$work/run"
chmod 700 "$work/run"
cp "$root/build/sublimite" "$work/prefix/bin/sublimite"
printf '#!/bin/sh\nfresh\n' >"$work/om/bin/omarchy-launch-editor"; chmod 755 "$work/om/bin/omarchy-launch-editor"
export HOME=$work/home XDG_CONFIG_HOME=$work/home/.config XDG_DATA_HOME=$work/home/.local/share
export XDG_STATE_HOME=$work/home/.local/state XDG_CACHE_HOME=$work/home/.cache XDG_RUNTIME_DIR=$work/run
export PREFIX=$work/prefix OMARCHY_PATH=$work/om EDIT_REFUSE_REAL_DISPLAY=1
unset WAYLAND_DISPLAY XDG_CURRENT_DESKTOP
sh "$root/tools/install-omarchy.sh" install >/dev/null || { echo "FAIL real: install"; exit 1; }
Xvfb ":$n" -screen 0 1280x800x24 -nolisten tcp >"$work/xvfb.log" 2>&1 &
xpid=$!
i=0; while [ ! -e "/tmp/.X11-unix/X$n" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
[ -e "/tmp/.X11-unix/X$n" ] || { echo "FAIL real: Xvfb :$n did not start"; exit 1; }
export DISPLAY=:$n
printf 'subject\n' >"$work/msg"
helper=$(cat "$HOME/.local/state/omarchy/defaults/editor")
( "$helper" -- "$work/msg" >"$work/out" 2>&1; echo $? >"$work/rc" ) &
caller=$!
win=; i=0
while [ -z "$win" ] && [ $i -lt 100 ]; do win=$(xdo search --onlyvisible --class sublimite 2>/dev/null | head -n 1); [ -n "$win" ] || sleep 0.1; i=$((i + 1)); done
[ -n "$win" ] || { echo "FAIL real: no sublimite window"; cat "$work/out"; exit 1; }
sleep 0.5
if [ -e "$work/rc" ]; then echo "FAIL real: wrapper returned while the tab was open"; exit 1; fi
echo "ok   real: wrapper blocks while the tab is open"
xdo key --window "$win" ctrl+w
i=0; while [ ! -e "$work/rc" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
if [ "$(cat "$work/rc" 2>/dev/null)" = 0 ]; then echo "ok   real: wrapper returns 0 after the tab is closed"; else echo "FAIL real: rc='$(cat "$work/rc" 2>/dev/null)'"; cat "$work/out"; exit 1; fi
exit 0
