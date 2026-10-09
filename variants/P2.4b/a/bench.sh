#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
export EDIT_GL_UPLOAD=subdata
exec "$root/build/variants/P2.4b/a/bench/gl_bench" --upload-only "$@"
