#!/bin/sh
# Same release objects/test/bench interface; only the candidate default differs.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
export DISPLAY=:99 EDIT_DISPLAY=:99
make build/libedit.a build/rel/bench/gl_bench.o build/rel/tests/gl_test.o
variant_libs="-lm -ldl"
for file in src/*/LDLIBS; do variant_libs="$variant_libs $(cat "$file")"; done
for variant in a b c; do
    out="build/variants/P2.4b/$variant"
    mkdir -p "$out/rel/src/gl" "$out/bench" "$out/tests"
    "${CC_RELEASE:-gcc}" -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion \
        -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -c "variants/P2.4b/$variant/gl.c" \
        -o "$out/rel/src/gl/gl.o"
    "${CC_RELEASE:-gcc}" -O2 -g -msse2 -pthread build/rel/bench/gl_bench.o \
        "$out/rel/src/gl/gl.o" build/libedit.a $variant_libs -o "$out/bench/gl_bench"
    "${CC_RELEASE:-gcc}" -O2 -g -msse2 -pthread build/rel/tests/gl_test.o \
        "$out/rel/src/gl/gl.o" build/libedit.a $variant_libs -o "$out/tests/gl_test"
    echo "P2.4b: built $variant"
done
