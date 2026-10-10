#!/bin/sh
# EGL may be absent on Xvfb; native review lanes must skip successfully.
set -eu
binary=${1:-build/tests/gl_test}
output=$(DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_GL_EGL_LIBRARY=/nonexistent/editor-libEGL.so \
    "$binary" --review 1)
printf '%s\n' "$output"
case "$output" in
    *'SKIP: Xvfb/EGL context unavailable'*) ;;
    *) exit 1 ;;
esac
