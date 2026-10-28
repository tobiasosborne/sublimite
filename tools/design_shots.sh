#!/bin/sh
# tools/design_shots.sh <binary> <outdir>
# Deterministic scene capture for the design hill-climb (edit-cxho.21).
# Renders 11 scenes x 2 themes (dark, light) of the given sublimite binary on a
# private Xvfb, writes NN-name-theme.png, manifest.json and contact-sheet.png.
# Needs: Xvfb xdotool import(ImageMagick) montage sha256sum.
# Env: EDIT_SHOTS_DISPLAY (default :91, private; never :0).
set -eu
[ $# -eq 2 ] || { echo "usage: $0 <binary> <outdir>" >&2; exit 2; }
bin=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
mkdir -p "$2"
out=$(cd "$2" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
fix=$here/design_fixtures
# Two private displays, one lane per theme, so the two ~10 s-per-scene idle
# waits overlap. Only :91 and :92 (never :0, :95, :97, :99).
disp_dark=:91; disp_light=:92

# (M) Tobias's ThinkPad X1 Carbon Gen 11 panel screenshots are 3296x2060.
W=3296; H=2060
# (P) 192 dpi = 2x the 96 dpi CSS reference pixel; the same scale the panel
# reports, so glyph sizes match his screenshots.
DPI=192
# (P) default 10 pt -> round(10*DPI/72) em px; embedded DejaVu's
# units/em=2048, ASCII advance=1233, hhea ascent/descent=1901/483
# (src/editor/runtime.h, src/font/font.c round-to-nearest metrics).
# These are the same 16 x 31 px cells at this harness DPI, not screen guesses.
em=$(((10 * DPI + 36) / 72))
cw=$(((em * 1233 + 1024) / 2048))
ch=$((((em * 1901 + 1024) / 2048) + ((em * 483 + 1024) / 2048)))
text_col=$(awk 'END{n=NR+1; d=1; while(n>=10){d++;n=int(n/10)};print d+4}' "$fix/readme.md")
# The find scene selects the first literal occurrence; derive its count width.
matches=$(awk '{s=$0;while(match(s,/minimap/)){n++;s=substr(s,RSTART+RLENGTH)}} END{print n+0}' "$fix/readme.md")
count_cells=$((1 + 4 + ${#matches})) # (P) "1 of " plus decimal count
# Settle rule: a frame is settled when the screen has not changed for QUIET_NS.
# (P) the caret blinks with a 500 ms half period (EDITOR_BLINK_NS in
# src/editor/editor.h), so a blinking caret changes the screen at least every
# 500 ms; 1.2 s = 2.4 half periods (a full 1 s cycle plus 20 %) of identical
# frames proves the blink has stopped. The editor stops blinking after 10 s idle
# (EDITOR_IDLE_NS, PRD G11) and leaves the caret visible, so every scene ends in
# the same caret-visible state. Cost: about 10 s per scene (M).
QUIET_NS=1200000000
# Upper bound on one settle: (P) idle stop 10 s + QUIET 1.2 s, times 3 as slack
# for a loaded box (E): 34 s.
SETTLE_MAX_NS=34000000000

work=$(mktemp -d)
lane_pids=""
cleanup() {
    for p in $lane_pids; do kill "$p" 2>/dev/null || true; done
    wait 2>/dev/null || true
    if [ -n "${EDIT_SHOTS_KEEP_WORK:-}" ]; then echo "design_shots work: $work" >&2; else rm -rf "$work"; fi
}
trap cleanup EXIT HUP INT TERM
export EDIT_REFUSE_REAL_DISPLAY=1

# Large fixture: pure function of the loop index (no seed, no clock).
# (P) 20000 lines is the scene definition; 300 px of minimap viewport box is not
# asserted, only that the file is far taller than the screen (M: ~110 rows).
big=$work/big.md
awk 'BEGIN{for(i=1;i<=20000;i++){
  if(i%50==1) printf "## Section %d\n\n", int(i/50)+1;
  else if(i%7==0) printf "- item %d: the quick brown fox jumps over the lazy dog\n", i;
  else if(i%11==0) printf "    code_%d(a, b, c); /* indented */\n", i;
  else printf "Paragraph line %d with **bold** and `code` words in it.\n", i}}' >"$big"

# Polling uses raw xwd dumps ((M) 0.08 s per grab vs 0.49 s for import -window
# root at 3296x2060 on this box [AC]); only the settled frame is converted to PNG.
# (M) xwd header is 107 bytes plus a 256-entry colormap of 12 bytes each
# (od of a dump: header_size 107, ncolors 256) = 3179 bytes; the colormap bytes
# differ between grabs of an identical screen, so comparisons skip them.
XWD_SKIP=3179
grab() { xwd -root -silent >"$1"; }
topng() { convert xwd:"$1" -strip -depth 8 "$2"; }

# settle <prev-file-or-empty> <result-file>: wait until the frame differs from
# prev (when given: the input has been consumed), then until the screen has
# been identical for QUIET_NS. Compare dumps from byte XWD_SKIP.
settle() {
    prev=$1; res=$2; seen_change=0; t0=$(date +%s%N); tstable=$t0
    [ -n "$prev" ] || seen_change=1
    rm -f "$sd/g.last"
    while :; do
        now=$(date +%s%N)
        if [ $((now - t0)) -gt $SETTLE_MAX_NS ]; then
            echo "settle: no quiet frame within $SETTLE_MAX_NS ns" >&2; return 1
        fi
        grab "$sd/g.xwd"
        if [ $seen_change -eq 0 ]; then
            if cmp -s -i $XWD_SKIP "$sd/g.xwd" "$prev"; then continue; fi
            seen_change=1
        fi
        if [ -f "$sd/g.last" ] && cmp -s -i $XWD_SKIP "$sd/g.xwd" "$sd/g.last"; then
            if [ $((now - tstable)) -ge $QUIET_NS ]; then cp "$sd/g.xwd" "$res"; return 0; fi
        else
            tstable=$now; cp "$sd/g.xwd" "$sd/g.last"
        fi
    done
}

# scene <num-name> <theme> <files...> -- <keystrokes as xdotool args>
# Keystroke script lives in $keys (lines: "key <chord>" or "type <text>").
run_scene() {
    name=$1; theme=$2; keys=$3; shift 3
    sd=$work/scene-$theme; rm -rf "$sd"; mkdir -p "$sd/home" "$sd/cfg/sublimite" "$sd/data" "$sd/run" "$sd/state"
    chmod 700 "$sd/run"
    printf 'theme = "%s"\n' "$theme" >"$sd/cfg/sublimite/config.toml"
    env HOME="$sd/home" XDG_CONFIG_HOME="$sd/cfg" XDG_DATA_HOME="$sd/data" \
        XDG_RUNTIME_DIR="$sd/run" XDG_STATE_HOME="$sd/state" XDG_CACHE_HOME="$sd/state" \
        "$bin" "$@" >"$sd/ed.log" 2>&1 &
    epid=$!
    n=0; win=""
    while [ -z "$win" ]; do
        win=$(xdotool search --onlyvisible --name . 2>/dev/null | head -n 1 || true)
        n=$((n + 1)); [ $n -lt 200 ] || { echo "$name: no window" >&2; return 1; }  # (E) 200 x 0.1 s
        [ -n "$win" ] || sleep 0.1
    done
    # No window manager under Xvfb: the client chooses its own size, so force
    # the panel geometry directly (verified in report: editor relayouts).
    xdotool windowmove "$win" 0 0 windowsize "$win" "$W" "$H"
    xdotool mousemove --window "$win" 0 $((H / 2)) # deterministic non-hover margin
    # Keys queue on the window in X order, so they are processed after the
    # resize; one settle at the end (no intermediate base frame: it would cost
    # a second 10 s idle wait per scene).
    if [ -n "$keys" ]; then
        printf '%s\n' "$keys" | while IFS= read -r line; do
            case $line in
                "key "*) xdotool key --delay 50 ${line#key } ;;
                "type "*) xdotool type --delay 50 -- "${line#type }" ;;
                "hover tab")
                    settle "" "$sd/prehover.xwd"
                    # (P) second tab is one 16-cell slot past the text edge.
                    xdotool mousemove --window "$win" $(((text_col + 16 + 2) * cw)) $((ch / 2)) ;;
                "hover find")
                    settle "" "$sd/prehover.xwd"
                    # (P) map 8 + outer margin 1 + panel gap 1; toggles 8,
                    # count gap 1. Aa is the first two cells of that summary.
                    aa=$((W / cw - 8 - 1 - 1 - 8 - 1 - count_cells))
                    xdotool mousemove --window "$win" $((aa * cw + cw / 2)) $(((H / ch - 1) * ch + ch / 2)) ;;

            esac
        done
    fi
    if [ -f "$sd/prehover.xwd" ]; then
        settle "$sd/prehover.xwd" "$sd/final.xwd" || { echo "$name-$theme: hover settle failed" >&2; return 1; }
    else
        settle "" "$sd/final.xwd" || return 1
    fi
    topng "$sd/final.xwd" "$out/$name-$theme.png"
    kill "$epid" 2>/dev/null || true; wait "$epid" 2>/dev/null || true; epid=""
}

# Scene table: id, keystrokes, files. Order of the four tabs puts README last
# so the Markdown file is the active one.
four="$fix/hello.c $fix/util.py $fix/notes.txt $fix/readme.md"
all_scenes="01-empty 02-tabs 03-find 04-replace 05-goto 06-palette 07-code 08-large 09-hover-tab 10-hover-find 11-find-empty"
[ -z "${EDIT_SHOTS_ONLY:-}" ] || all_scenes=$EDIT_SHOTS_ONLY  # debugging: e.g. "01-empty"
# lane <theme> <display>: private Xvfb plus every scene for one theme. Its own
# trap kills the Xvfb and the editor even when a scene fails.
lane() {
    theme=$1; export DISPLAY=$2; epid=""; xpid=""
    trap 'kill $epid $xpid 2>/dev/null || true' EXIT HUP INT TERM
    Xvfb "$DISPLAY" -screen 0 "${W}x${H}x24" -dpi "$DPI" -noreset >"$work/xvfb-$theme.log" 2>&1 &
    xpid=$!
    n=0
    until xdotool getdisplaygeometry >/dev/null 2>&1; do
        n=$((n + 1)); [ $n -lt 100 ] || { echo "Xvfb $DISPLAY did not start" >&2; return 1; }  # (E) 100 x 0.1 s
        sleep 0.1
    done
    for s in $all_scenes; do
        case $s in
            01-empty)   run_scene $s $theme "" ;;
            02-tabs)    run_scene $s $theme "" $four ;;
            03-find)    run_scene $s $theme "key ctrl+f
type minimap" $four ;;
            04-replace) run_scene $s $theme "key ctrl+h
type minimap" $four ;;
            05-goto)    run_scene $s $theme "key ctrl+g
type 12" $four ;;
            06-palette) run_scene $s $theme "key ctrl+shift+p
type wrap" $four ;;
            07-code)    run_scene $s $theme "" "$fix/hello.c" ;;
            09-hover-tab) run_scene $s $theme "hover tab" $four ;;
            10-hover-find) run_scene $s $theme "key ctrl+f
type minimap
hover find" $four ;;
            11-find-empty) run_scene $s $theme "key ctrl+f" $four ;;
            08-large)   run_scene $s $theme "key ctrl+g
type 10000
key Return" "$big" ;;
        esac
    done
}
( lane dark "$disp_dark" ) & lane_pids="$!"
( lane light "$disp_light" ) & lane_pids="$lane_pids $!"
rc=0
for p in $lane_pids; do wait "$p" || rc=1; done
lane_pids=""
[ $rc -eq 0 ] || { echo "design_shots: a lane failed" >&2; exit 1; }

# manifest.json
power=$(cat /sys/class/power_supply/BAT0/status 2>/dev/null || echo unknown)
bsha=$(sha256sum "$bin" | cut -d' ' -f1)
{
    printf '{\n  "binary_sha256": "%s",\n  "dpi": %d,\n  "width": %d,\n  "height": %d,\n  "power": "%s",\n  "scenes": [\n' "$bsha" "$DPI" "$W" "$H" "$power"
    first=1
    for theme in dark light; do
        for s in $all_scenes; do
            f=$out/$s-$theme.png
            [ $first -eq 1 ] || printf ',\n'
            first=0
            printf '    {"scene": "%s", "theme": "%s", "file": "%s", "sha256": "%s"}' \
                "$s" "$theme" "$(basename "$f")" "$(sha256sum "$f" | cut -d' ' -f1)"
        done
    done
    printf '\n  ]\n}\n'
} >"$out/manifest.json"

# Contact sheet: 4 columns, each tile 800 px wide (E: 4 x 800 = 3200 px sheet).
montage $(for theme in dark light; do for s in $all_scenes; do printf '%s ' "$out/$s-$theme.png"; done; done) \
    -tile 4x -geometry 800x+6+6 -background "#808080" -strip "$out/contact-sheet.png"
echo "design_shots: $(ls "$out"/*-dark.png "$out"/*-light.png | wc -l) scenes in $out"
