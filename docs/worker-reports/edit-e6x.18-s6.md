# edit-e6x.18 report (clipboard review fixes #1 #4 #5 #6 #13 #14 #15 #28)

Files: src/x11/clip.c, src/x11/clip.h (x11_clip_mem, x11_clip_set_budget, x11_clip_max_slice), tests/x11_clip_test.c, docs/decisions/P2.2d.md, src/x11/STATUS.md. x11.c/input.c/xi2.c untouched; no x11.c hook needed.

## Red run (new tests on the old code; helpers/peer extended first)

    FAIL tests/x11_clip_test.c:548: forged SelectionClear raised a loss event
    FAIL tests/x11_clip_test.c:552: forged SelectionClear destroyed the clipboard data
    FAIL tests/x11_clip_test.c:578: a local completion exposed another selection's data
    FAIL tests/x11_clip_test.c:592: takeover after confirmation reports the loss (lost 0)
    FAIL tests/x11_clip_test.c:593: paste after takeover returned stale data (4 bytes)
    FAIL tests/x11_clip_test.c:612: INCR one byte below a 4096 lower bound must fail (ok 1, failed 0)
    FAIL tests/x11_clip_test.c:612: INCR immediate terminator below the lower bound must fail (ok 1, failed 0)
    FAIL tests/x11_clip_test.c:612: INCR STRING transfer below the lower bound must fail (ok 1, failed 0)
    FAIL tests/x11_clip_test.c:632: local paste moved 20971520 bytes in one slice (limit 1048576)
    FAIL tests/x11_clip_test.c:646: MULTIPLE moved 12800000 bytes in one slice (limit 1310720)
    FAIL tests/x11_clip_test.c:699: late UTF8 refusal then timely STRING must succeed (ok 0, failed 1)
    FAIL tests/x11_clip_test.c:720: MULTIPLE notifications out of order: 0 0 251 (want none, p2, none)
    FAIL tests/x11_clip_test.c:667: second 600 KB set must exceed the 1 MiB budget
    FAIL tests/x11_clip_test.c:670: mem counts the owned blob (1200000)
    FAIL tests/x11_clip_test.c:676: a receive that would exceed the budget must fail (ok 1, failed 0)
    FAIL tests/x11_clip_test.c:678: failed receive released its buffer (600000 vs 1200000)
    x11_clip_test: FAILED

## Green run
After the fixes: build/san/tests/x11_clip_test (ASAN_OPTIONS=detect_leaks=1, DISPLAY=:99) printed "x11_clip_test: ok" 5/5 times; release build/tests/x11_clip_test ok. The old "forged Clear honoured" test (#1) was replaced by a genuine takeover.

## Gates
- make all: rc 0. make check: "check: 29 test binaries passed". make fuzz: "fuzz: 16 fuzzers built". The 120 s x11_input_fuzz was not run: no decoder (x11_clip_decode_*, input, xi2) changed.
- No bench run (no perf claim made); slice budget 1 MiB is (E).

## Verdicts
All eight findings confirmed. #13 only partly fixed (see below).

## Open problems
- #13 residuals: receive-buffer growth by realloc, plat_clip_set memcpy of up to 64 MiB and Latin-1 conversion remain on the UI thread; needs a worker via src/work mailboxes (design call).
- #14 side effect: the paste buffer counts toward the 128 MiB budget until replaced (an idle one is dropped by plat_clip_set when it is the only way to fit).
- #28 residual: with all 8 job slots occupied a refusal is notified immediately (order not guaranteed).
- Tests for #13 count bytes (x11_clip_max_slice), not milliseconds; a typing-latency-under-large-paste test is still missing.
