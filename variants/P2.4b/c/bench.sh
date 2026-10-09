#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
export EDIT_GL_UPLOAD=persistent
exec "$root/build/variants/P2.4b/c/bench/gl_bench" --upload-only "$@"
