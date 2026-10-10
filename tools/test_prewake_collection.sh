#!/bin/sh
# The common bench must not require ignored, uncollected experiment sources.
set -eu
cd "$(dirname "$0")/.."
for source in prewake.h contract.c run.py summarize.py a/prewake.c b/prewake.c c/prewake.c; do
    if [ ! -f "bench/prewake/$source" ]; then
        echo "FAIL: missing collectible bench/prewake/$source" >&2; exit 1
    fi
done
if [ ! -f fuzz/prewake_fuzz.c ]; then
    echo 'FAIL: missing prewake state-machine fuzzer' >&2; exit 1
fi
echo 'prewake collection: common API, contracts, runner, summarizer and A/B/C present'
