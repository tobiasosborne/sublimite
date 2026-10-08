#!/bin/sh
# bench_variant.sh [variant_dir|src/piece] [piece_bench args...]
# Builds bench/piece_bench.c against the variant's *.c (instead of src/piece/piece.c)
# plus every other src/*/*.c that compiles (as a static archive, so only referenced
# objects link), with the Makefile's release flags, then prints the variant's .text
# size as a BENCH line and runs the bench. Output: build/variants/<name>/piece_bench.
set -u
cd "$(dirname "$0")/.." || exit 2
var=src/piece
if [ $# -gt 0 ]; then
    case "$1" in --*) ;; *) var=${1%/}; shift ;; esac
fi
[ -d "$var" ] || { echo "bench_variant: no such dir $var" >&2; exit 2; }
if [ "$var" = src/piece ]; then name=intree; else name=$(basename "$var"); fi
out=build/variants/$name
rm -rf "$out"; mkdir -p "$out/v" "$out/o"
CF="-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -I$var -Isrc"

vobjs=
for f in "$var"/*.c; do
    o=$out/v/$(basename "$f" .c).o
    gcc $CF -c "$f" -o "$o" || { echo "bench_variant: compile failed: $f" >&2; exit 2; }
    vobjs="$vobjs $o"
done
[ -n "$vobjs" ] || { echo "bench_variant: no .c in $var" >&2; exit 2; }

# other modules (skip piece itself and modules needing extra libs); failures only warn
oobjs=
for f in src/*/*.c; do
    d=$(dirname "$f")
    [ "$d" = src/piece ] && continue
    [ -f "$d/LDLIBS" ] && continue
    o=$out/o/$(echo "$f" | tr / _ | sed 's/\.c$/.o/')
    if gcc $CF -c "$f" -o "$o" 2>/dev/null; then oobjs="$oobjs $o"; else echo "bench_variant: warning: skipped $f (does not compile)" >&2; fi
done
rm -f "$out/libother.a"
[ -n "$oobjs" ] && ar rcs "$out/libother.a" $oobjs

gcc $CF -c bench/piece_bench.c -o "$out/piece_bench.o" || exit 2
libs=; [ -f "$out/libother.a" ] && libs=$out/libother.a
gcc -O2 -g -msse2 -pthread "$out/piece_bench.o" $vobjs $libs -lm -ldl -o "$out/piece_bench" || exit 2

text=$(size $vobjs | awk 'NR>1 {t += $1} END {print t+0}')
echo "BENCH row=build col=text_size p50=$text p99=$text unit=B n=1 gate=none status=TRACK"
exec "$out/piece_bench" "$@"
