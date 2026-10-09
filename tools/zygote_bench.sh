#!/bin/sh
# Build-independent P4.14 experiment; real display requires two explicit opt-ins.
set -eu
cd "$(dirname "$0")/.."
mode=--self-check
selected=0
real=0
launches=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --self-check|--run|--build)
            if [ "$selected" = 1 ]; then echo 'choose one mode' >&2; exit 2; fi
            mode=$1; selected=1 ;;
        --real-display) real=1 ;;
        --launches)
            shift
            if [ "$#" = 0 ]; then echo '--launches requires N' >&2; exit 2; fi
            case "$1" in
                ''|*[!0-9]*) echo '--launches must be an integer in 1..10000' >&2; exit 2 ;;
            esac
            # Bound string length before shell arithmetic (avoid overflow).
            if [ "${#1}" -gt 5 ] || [ "$1" -lt 1 ] || [ "$1" -gt 10000 ]; then
                echo '--launches must be an integer in 1..10000' >&2; exit 2
            fi
            launches=$1 ;;
        *) echo 'usage: tools/zygote_bench.sh [--build|--self-check|--run] [--launches N] [--real-display]' >&2; exit 2 ;;
    esac
    shift
done
if [ "$real" = 1 ]; then
    if [ "${EDIT_ALLOW_REAL_DISPLAY:-}" != 1 ]; then
        echo '--real-display requires EDIT_ALLOW_REAL_DISPLAY=1' >&2; exit 2
    fi
    if [ -z "${DISPLAY:-}" ] || [ "${DISPLAY:-}" != "${EDIT_DISPLAY:-}" ]; then
        echo '--real-display requires matching nonempty DISPLAY and EDIT_DISPLAY' >&2; exit 2
    fi
else
    export DISPLAY=:99 EDIT_DISPLAY=:99
fi
set -- "$mode"
if [ -n "$launches" ]; then set -- "$@" --launches "$launches"; fi
if [ "$real" = 1 ]; then set -- "$@" --real-display; fi
cc=${CC:-gcc}
flags='-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g'
lib=build/libedit.a
if [ "${ZYGOTE_SAN:-0}" = 1 ]; then
    flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer -O1"
    lib=build/san/libedit.a
fi
if [ ! -f "$lib" ]; then echo "missing $lib; run make all / make check first" >&2; exit 1; fi
libs='-lm -ldl'
for f in src/*/LDLIBS; do libs="$libs $(cat "$f")"; done
for v in a b c d; do
    # Word splitting is intentional for local fixed compiler/link flags.
    $cc $flags -Isrc "variants/P4.14/$v/main.c" "variants/P4.14/$v/open.c" variants/P4.14/editor.c "$lib" $libs -o "variants/P4.14/$v/launch"
done
$cc $flags -Isrc bench/zygote_bench.c -o variants/P4.14/zygote_bench
if [ "$mode" = --build ]; then exit 0; fi
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
exec variants/P4.14/zygote_bench "$@"
