#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
export DISPLAY=:99 EDIT_DISPLAY=:99
unset EDIT_ALLOW_REAL_DISPLAY
mode=${1:-build}
out=build/prewake
mkdir -p "$out"
flags='-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wshadow -Wconversion -pthread -Isrc -Ibench/prewake'
case "$mode" in
 test|test-san)
  cc=gcc; opt='-O2 -g'
  if [ "$mode" = test-san ]; then cc=clang; opt='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; fi
  i=0
  for v in a b c; do
   "$cc" $flags $opt -DPREWAKE_VARIANT="$i" -DPREWAKE_IMPL="\"$v/prewake.c\"" bench/prewake/contract.c -o "$out/test-$v"
   ASAN_OPTIONS=detect_leaks=0 "$out/test-$v"
   i=$((i+1))
  done
  ;;
 build|build-san)
  cc=gcc; opt="-O2 -g"; lib=build/libedit.a
  if [ "$mode" = build-san ]; then cc=clang; opt="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"; lib=build/san/libedit.a; fi
  make "$lib"
  libs=$(cat src/*/LDLIBS)
  for v in a b c; do
   "$cc" $flags $opt -DPREWAKE_IMPL="\"prewake/$v/prewake.c\"" bench/prewake_bench.c "$lib" -lm -ldl $libs -o "$out/prewake-$v${mode#build}"
  done
  ;;
 *) echo 'usage: tools/prewake_bench.sh build|build-san|test|test-san' >&2; exit 1;;
esac
