#!/bin/sh
# Build-independent P4.14 experiment; never opens the user's display.
set -eu
cd "$(dirname "$0")/.."
export DISPLAY=:99 EDIT_DISPLAY=:99
unset EDIT_ALLOW_REAL_DISPLAY
mode=${1:---self-check}
case "$mode" in --self-check|--run|--build) ;; *) echo 'usage: tools/zygote_bench.sh [--build|--self-check|--run]' >&2; exit 2 ;; esac
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
for v in a b c; do
    # Word splitting is intentional for local fixed compiler/link flags.
    $cc $flags -Isrc "variants/P4.14/$v/main.c" "variants/P4.14/$v/open.c" variants/P4.14/editor.c "$lib" $libs -o "variants/P4.14/$v/launch"
done
$cc $flags -Isrc bench/zygote_bench.c -o variants/P4.14/zygote_bench
if [ "$mode" = --build ]; then exit 0; fi
cat /sys/class/power_supply/BAT0/status
cut -d' ' -f1 /proc/loadavg
exec variants/P4.14/zygote_bench "$mode"
