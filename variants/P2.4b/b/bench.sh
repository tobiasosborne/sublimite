#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
export EDIT_GL_UPLOAD=orphan
exec "$root/build/variants/P2.4b/b/bench/gl_bench" --upload-only "$@"
