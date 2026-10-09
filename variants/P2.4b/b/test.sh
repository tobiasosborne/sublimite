#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
export DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_GL_UPLOAD=orphan
exec "$root/build/variants/P2.4b/b/tests/gl_test" "$@"
