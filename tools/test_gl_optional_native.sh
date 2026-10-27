#!/bin/sh
# EGL may be absent on Xvfb; native review lanes must skip successfully.
set -eu
if [ "${EDIT_DISPLAY_LOCK_SCRIPT:-}" != "$0" ]; then
    export DISPLAY=:99 EDIT_DISPLAY_LOCK_SCRIPT="$0"
    exec sh "$(dirname "$0")/with_display_lock.sh" sh "$0" "$@"
fi
binary=${1:-build/tests/gl_test}
output=$(DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_GL_EGL_LIBRARY=/nonexistent/editor-libEGL.so \
    "$binary" --review 1)
printf '%s\n' "$output"
case "$output" in
    *'SKIP: Xvfb/EGL context unavailable'*) ;;
    *) exit 1 ;;
esac
