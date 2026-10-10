# edit-tkw — session 9 worker report

Scope: P2-1 sections 3, 4, 29, 30, 31, 32 and 33. All interrupted source/test
changes were reviewed and retained. The initial tree already built; the
continuation added a missing pre-stb cmap-relative offset check, its failing
sanitizer regression, and the missing decision/status/report documentation.
Git was used only for read-only status/diff/show; no bd, HANDOFF or worklog edits.
No new globals, vendor changes, or editor changes.

## Red/green provenance

The previous worker left tests and fixes together, without a report or red
logs. It is impossible to certify that session's original ordering. The red
runs below were reconstructed against HEAD font/discovery/Unicode source
copies under build/s9-red, linked with the already-written regression tests.
UI API aliases call the old general APIs solely for baseline linking. The
owner API is absent from HEAD: its red is a regression-test link failure with
the new owner implementation excluded. The fuzz red replays the original
trailing-byte face selection against the new seed-admission assertion. These
are explicitly reconstructed red runs, not invented historical evidence.

The new cmap regression was written and run against the inherited WIP first,
failed with UBSan, and only then received its fix. The first fuzz campaign
exited zero but contained that UBSan diagnostic, so it is NOT claimed clean.
The final campaign used UBSAN_OPTIONS=halt_on_error=1.

All sanitizer runs used ASAN_OPTIONS=detect_leaks=0: LeakSanitizer cannot run
in the sandbox. Coordinator must rerun with leaks enabled. All display tests
were directed to :99. Baseline sanitizer snippets below omit stack frames
after the diagnostic; messages/check failures and exit results are pasted.

## P2-1 section 3

Done: cap oversized buffers before stb initialization; validate every supported cmap-relative addition in uint64_t before stb reads/narrows it. Sparse virtual-memory and high-bit cmap fixtures exercise both.

Red:

```text
$ DISPLAY=:99 FONT_REVIEW_S9=3 ASAN_OPTIONS=detect_leaks=0 build/s9-red/font_test
tests/font_test.c:110: CHECK failed: font_init(&face, bytes, huge) == FONT_ERR_INIT
tests/font_test.c:111: CHECK failed: face.data == NULL
build/s9-red/src/font/../../vendor/stb_truetype.h:1288:60: runtime error: left shift of 128 by 24 places cannot be represented in type 'int'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior build/s9-red/src/font/../../vendor/stb_truetype.h:1288:60 
AddressSanitizer:DEADLYSIGNAL
=================================================================
==4==ERROR: AddressSanitizer: SEGV on unknown address 0x7334c71ff032 (pc 0x58f61d3088b8 bp 0x7ffe12732530 sp 0x7ffe12732440 T0)
exit=1
```

Additional actual WIP red before the new fix:

```text
$ FONT_REVIEW_S9=3 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/font_test
src/font/../../vendor/stb_truetype.h:1288:60: runtime error: left shift of 128 by 24 places cannot be represented in type 'int'
exit=1
```

Green:

```text
$ DISPLAY=:99 FONT_REVIEW_S9=3 ASAN_OPTIONS=detect_leaks=0 build/san/tests/font_test
P2-1 §3 GREEN: sparse oversized font and cmap-relative offsets refused before stb
exit=0
```

## P2-1 section 4

Done: check finite CFF move/line/control coordinates before destination conversion, check scaled endpoints before int conversion, and compute bitmap spans in int64_t with allocation/work ceilings. Extreme-coordinate and small-ascent fixtures reject cleanly.

Red:

```text
$ DISPLAY=:99 FONT_REVIEW_S9=4 ASAN_OPTIONS=detect_leaks=0 build/s9-red/font_test
build/s9-red/src/font/../../vendor/stb_truetype.h:2741:23: runtime error: 3.44063e+09 is outside the range of representable values of type 'int'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior build/s9-red/src/font/../../vendor/stb_truetype.h:2741:23 
tests/font_test.c:356: CHECK failed: font_glyph_metrics(&face, 'B', &metric) == FONT_ERR_INIT
tests/font_test.c:357: CHECK failed: font_raster_glyph(&face, 'B', &scratch, &bitmap) == FONT_ERR_INIT
tests/font_test.c:356: CHECK failed: font_glyph_metrics(&face, 'B', &metric) == FONT_ERR_INIT
tests/font_test.c:357: CHECK failed: font_raster_glyph(&face, 'B', &scratch, &bitmap) == FONT_ERR_INIT

exit=1
```

Green:

```text
$ DISPLAY=:99 FONT_REVIEW_S9=4 ASAN_OPTIONS=detect_leaks=0 build/san/tests/font_test
P2-1 §4 GREEN: extreme coordinates and small-ascent bitmap rejected
exit=0
```

## P2-1 section 30

Done: propagate interpreter errors as FONT_ERR_INIT, preserve valid empty glyphs, try healthy fallback faces and retain INIT when all covering faces fail. Fixtures cover missing endchar, invalid return, subroutine exhaustion, recovery and empty success.

Red:

```text
$ DISPLAY=:99 FONT_REVIEW_S9=30 ASAN_OPTIONS=detect_leaks=0 build/s9-red/font_test
tests/font_test.c:377: CHECK failed: font_glyph_metrics(&face, 'B', &metric) == FONT_ERR_INIT
tests/font_test.c:378: CHECK failed: font_raster_glyph(&face, 'B', &scratch, &bitmap) == FONT_ERR_INIT
tests/font_test.c:377: CHECK failed: font_glyph_metrics(&face, 'B', &metric) == FONT_ERR_INIT
tests/font_test.c:378: CHECK failed: font_raster_glyph(&face, 'B', &scratch, &bitmap) == FONT_ERR_INIT
tests/font_test.c:377: CHECK failed: font_glyph_metrics(&face, 'B', &metric) == FONT_ERR_INIT
tests/font_test.c:378: CHECK failed: font_raster_glyph(&face, 'B', &scratch, &bitmap) == FONT_ERR_INIT
tests/font_test.c:400: CHECK failed: slot != RENDER_NO_SLOT
tests/font_test.c:402: CHECK failed: font_cache_glyph(&cache, (const uint8_t *)"BB", 2, 2, &slot) == FONT_ERR_INIT

exit=1
```

Green:

```text
$ DISPLAY=:99 FONT_REVIEW_S9=30 ASAN_OPTIONS=detect_leaks=0 build/san/tests/font_test
P2-1 §30 GREEN: CFF errors rejected, empty glyph preserved, fallback recovers
exit=0
```

## P2-1 section 31

Done: route foreground cache work through structural TT/CFF/bitmap/subdivision/scanline bounds. Tiny-bitmap, high-outline TT and CFF fixtures reject; general worker raster stays available. These are bounded CPU-work admissions, not wall-time guarantees under OS descheduling.

Red:

```text
$ DISPLAY=:99 FONT_REVIEW_S9=31 ASAN_OPTIONS=detect_leaks=0 build/s9-red/font_test
tests/font_test.c:452: CHECK failed: font_glyph_ui_metrics(&face, 'A', &metric) == FONT_ERR_INIT
tests/font_test.c:453: CHECK failed: font_raster_glyph_ui(&face, 'A', &scratch, &bitmap) == FONT_ERR_INIT
tests/font_test.c:454: CHECK failed: scratch.used == 0
tests/font_test.c:485: CHECK failed: font_cache_glyph(&cache, (const uint8_t *)"B", 1, 1, &slot) == FONT_ERR_INIT
tests/font_test.c:485: CHECK failed: font_cache_glyph(&cache, (const uint8_t *)"B", 1, 1, &slot) == FONT_ERR_INIT
[repeated cache rejection checks omitted]
P2-1 §31 slices (M)[AC], shared loaded box, paired rejection/normal max: 1228340/23014 ns; TRACK only

exit=1
```

Green:

```text
$ DISPLAY=:99 FONT_REVIEW_S9=31 ASAN_OPTIONS=detect_leaks=0 build/san/tests/font_test
P2-1 §31 slices (M)[AC], shared loaded box, paired rejection/normal max: 20767/20139 ns; TRACK only
P2-1 §31 GREEN: tiny-bitmap high-outline workload refused on UI
exit=0
```

## P2-1 section 32

Done: cleanup applies independently to direct-child reaping and pipe EOF, closes the read side and kills the owned process group. Exited-helper/inherited-stdout cancellation fixture now finishes without adopting results.

Red:

```text
$ DISPLAY=:99 FONT_REVIEW_S9=32 ASAN_OPTIONS=detect_leaks=0 build/s9-red/font_test
tests/font_test.c:1040: CHECK failed: work_handle_finished(pool, handle)

exit=1
```

Green:

```text
$ DISPLAY=:99 FONT_REVIEW_S9=32 ASAN_OPTIONS=detect_leaks=0 build/san/tests/font_test
P2-1 §32 GREEN: cancellation finishes after helper exits with inherited stdout
exit=0
```

## P2-1 section 29

Done for the authorized font-module scope: backend reservations, stable worker-prepared runtime owner, authenticated mailbox adoption, grid binding and layout callback. Covered Unicode grid and independently rasterized atlas bytes agree. Editor hookup/production native pixels and automatic owner rotation remain an editor follow-up; src/editor was not changed.

Red:

```text
$ clang ... font_test.o no-runtime.a ... # owner implementation absent
ld: build/san/tests/font_test.o: in function `test_runtime_owner':
tests/font_test.c:1061:(.text+0x8f9c): undefined reference to `font_runtime_limits'
ld: tests/font_test.c:1063:(.text+0x8fe5): undefined reference to `font_runtime_init'
ld: tests/font_test.c:1065:(.text+0x9023): undefined reference to `font_runtime_glyph'
ld: tests/font_test.c:1069:(.text+0x909a): undefined reference to `font_runtime_prepare_job'
ld: tests/font_test.c:1073:(.text+0x91de): undefined reference to `font_runtime_glyph'
ld: tests/font_test.c:1080:(.text+0x92de): undefined reference to `font_runtime_bind'
ld: tests/font_test.c:1081:(.text+0x931e): undefined reference to `font_runtime_glyph'
ld: build/san/tests/font_test.o: in function `adopt_runtime':
tests/font_test.c:1050:(.text+0x231ec): undefined reference to `font_runtime_event'
ld: tests/font_test.c:1050:(.text+0x231fb): undefined reference to `font_runtime_event'
clang: error: linker command failed with exit code 1 (use -v to see invocation)

exit=1
```

Green:

```text
$ DISPLAY=:99 FONT_REVIEW_S9=29 ASAN_OPTIONS=detect_leaks=0 build/san/tests/font_test
P2-1 §29 GREEN: worker-prepared owner adopts through mailbox; Unicode grid/pixels match
exit=0
```

## P2-1 section 33

Done: every raw seed exercises face zero; optional independent control bytes
select additional TTC faces. Startup asserts admission and the intended A/B
paths for normal, subroutine-bomb and CID seeds. The parser fuzzer also invokes
the bounded foreground raster API.

Red (original selector restored in the seed-admission check):

```text
$ ASAN_OPTIONS=detect_leaks=0 build/s9-red/font_fuzz -runs=1
CFF seed 0: legacy trailing-byte face index=3; expect interpreter admission
exit=132 (REQUIRE trap)
```

Green:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/fuzz/font_fuzz -runs=1
CFF seed 0: face zero initialization and A/B paths verified
CFF seed 1: face zero initialization and A/B paths verified
CFF seed 2: face zero initialization and A/B paths verified
exit=0
```

## Final verification

Release compiler: gcc 13; sanitizer/fuzzer compiler: clang 18. Makefile uses
C11, -Wall -Wextra -Werror -Wshadow -Wconversion. Initial and final make all
completed successfully. Release font_test and sanitizer font_test both passed.
Release allocation guard was active: exhaustion sweep and repeated raster
reported zero allocations; runtime owner layout runs under the same guard.
Unicode rendering integration suite passed both baked sizes and covered
corpus content. No single timing observation is used as a gate verdict.

Final seeded fuzz command:

```text
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  build/fuzz/font_fuzz build/s9-cff-corpus -max_total_time=60 -max_len=16384 \
  -artifact_prefix=build/s9-cff-corpus/
Done 247219 runs in 61 second(s)
exit=0
```

The final campaign completed 61 seconds and 247219 inputs (M)[AC], with no
ASan/UBSan diagnostics; requested duration was 60 seconds (G). All generated
seeds reached the interpreter. The earlier campaign's signed-shift diagnostic
was converted into the new cmap regression and fixed before this final run.

The once-only loaded font benchmark exited zero. Observations (M)[AC], TRACK:
runtime Latin p50/p99 3106/4743 ns; CJK 5789/6011 ns; varied cold cluster
2941/8400 ns; long-extension slice 69813/74894 ns; cached cluster 42/44 ns.
The paired per-glyph rejection/normal observations are pasted above. These
measurements do not prove a universal timing gate under worker contention.

Final normal-user full suite passed (M)[AC], with UBSan halt enabled:

```text
$ DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 make check
check: 60 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
exit=0
```

```text
$ make all
exit=0
$ DISPLAY=:99 build/tests/font_test
font_test: mallocs during exhaustion sweep: 0
font_test: mallocs during 600 rasterisations: 0
font_test: all passed
exit=0
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/san/tests/font_test
font_test: all passed
exit=0
```

The full-suite count and allocation counts above are structural verification
observations (M)[AC]; zero typing allocations is the binding limit (G).
The first shared-display make check failed outside this bead in x11_clip_test:

```text
FAIL tests/x11_clip_test.c:226: large selection is announced with INCR
FAIL tests/x11_clip_test.c:227: INCR served bytes identical (600001 of 10500)
FAIL tests/x11_clip_test.c:235: TARGETS is never INCR
x11_clip_test: FAILED
make: *** [Makefile:101: check] Error 1
```

A direct :99 clipboard rerun exited zero. The received size matches another
clipboard fixture, consistent with concurrent ownership on the shared display.
No X11 source was changed. An additional private mount/network namespace ran Xvfb at :99 with its socket
backed by this worktree, without any :0 windows. That attempt failed in the
out-of-scope file permission fixture: user-namespace root capabilities bypass
the intended access denial. It is not the acceptance run and no file source
was changed. The normal-user final make check passed completely. Its output
is in build/s9-check-final.log; the private attempt is in
build/s9-check-isolated.log. Only the initial non-isolated Xvfb fixture remains
running for other shared :99 users; the private fixture was stopped by its trap.

## Decisions and remaining work

Design and the exact editor-hook/lifetime sequence are recorded in
docs/decisions/edit-tkw.md. Module STATUS.md is updated. Remaining outside this
bead: editor installs the runtime owner/callback and backend reservations,
rotates exhausted owners after physical work completion/mailbox drain/backend
retirement, and verifies production Unicode native pixels. Larger arbitrary
outline workloads need exclusive worker preparation; foreground admission
rejects them cleanly and tries healthy fallback faces. Coordinator leak-enabled
verification remains required. No remaining named font-module fix is known.
