# edit-40t — session 9 worker report

This is a partial bead delivery. P2-1 sections 2, 5, 17, 18, 19 and 35 are implemented. Sections 13, 14 and 20 remain, with executable optional red lanes and the required integration boundaries described below. The coordinator must not close the bead as fully accepted.

Read CLAUDE.md, the named P2-1 sections, src/gl/STATUS.md and docs/decisions/edit-zzj.12.md. Git was used read-only. No bd, HANDOFF.md, docs/worklog/ or other editor source was modified. All windows/connections used DISPLAY=:99; no display-zero access was performed.

Evidence: test counters and recorded runs below are (M) [AC]. The box was loaded; progress checks are structural, not latency gates. No single timing is used as a performance gate verdict. GCC 13 and clang 18 were used with the repository’s C11 -Wall -Wextra -Werror -Wshadow -Wconversion flags. Sanitizer runs use ASAN_OPTIONS=detect_leaks=0 because LeakSanitizer cannot run in the worker sandbox; the coordinator must rerun with leaks enabled. No new typing-path heap allocation was added.

## Per-finding red and green

### P2-1 §2 — backend identity

Done. All GL diagnostics, Present hints and mapped-cell APIs authenticate the GL implementation and non-null state before casting. Null backend is actually initialized with its advertised state; raster has its factory identity and bounded opaque storage. Missing GL state is also rejected.

Red, written/run before the fix:

```text
gl_test:1102: FAIL gl_completion_status(&b)==RENDER_ERR_STATE
```

Green:

```text
gl_test: P2-1 section 2 PASS (null/raster diagnostics, missing GL state)
```
### P2-1 §5 — benchmark error ownership

Done. Owner-specific failure labels stop and physically join borrowed bulk jobs, then shut down the driver and free arenas. Self-check failures also join before returning. Fault seams cover arena, prepared driver, initialized driver, active contention/frame failure, and module-backed contention. Module-backed acquisition faults cover bytes, tree, snapshot, index, find and save. Native stages require an admitted EGL/Present context and SKIP on :99.

Red, written/run before the fix:

```text
gl_bench:489: FAIL ok && !gl_bench_fault("self-overlap")
gl_test:1103: FAIL gl_live_threads()==before
AddressSanitizer:DEADLYSIGNAL
=================================================================
==15==ERROR: AddressSanitizer: SEGV on unknown address 0x7c13d17df730 (pc 0x7c13d1188d8d bp 0x7c13cc7fcbc0 sp 0x7c13cc7fc378 T1)
==15==The signal is caused by a READ memory access.
gl_bench: bulk_self_check jobs=1 chunks_in_200ms=4211 overlapped=1
```

Green:

```text
gl_bench:458: FAIL !gl_bench_fault("arena")
gl_bench:477: FAIL !gl_bench_fault("prepare")
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
gl_bench: bulk_self_check jobs=1 active_kind=0 shared_pool=1 chunks_in_200ms=2140 overlapped=1 evidence=(M) power=[AC]
TRACK name=A_egl_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=6.06
TRACK name=A_egl_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=6.06
TRACK name=A_egl_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=6.06
BENCH name=egl_init_15px status=SKIP reason=EGL_or_matching_Present_unsupported result=-9 power=[AC]
gl_test: section 5 stage=init SKIP: EGL/Present context unavailable on :99
TRACK name=A_egl_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=5.89
BENCH name=egl_init_15px status=SKIP reason=EGL_or_matching_Present_unsupported result=-9 power=[AC]
gl_test: section 5 stage=bulk SKIP: EGL/Present context unavailable on :99
TRACK name=A_egl_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=5.37
BENCH name=egl_init_15px status=SKIP reason=EGL_or_matching_Present_unsupported result=-9 power=[AC]
gl_test: section 5 stage=frame SKIP: EGL/Present context unavailable on :99
TRACK name=A_egl_scroll_600_15px status=SKIP verdict=TRACK reason=Xvfb_:99_has_no_real_vblank frames=0 requested_frames=600 evidence=(M)[AC] power_status="Not charging" load1=5.46
BENCH name=egl_init_15px status=SKIP reason=EGL_or_matching_Present_unsupported result=-9 power=[AC]
gl_test: section 5 stage=integrated SKIP: EGL/Present context unavailable on :99
gl_test: P2-1 section 5 PASS (arena/driver acquisition, active contention failure cleanup; native stages conditional)
```
### P2-1 §13 — startup critical path

Remaining. A held GPU initializer through production editor_open reproduces the synchronous startup dependency. Correct raster bootstrap, separately owned candidate/probe window and safe adoption need src/editor/open.c and editor lifecycle state. Those are outside the permitted editor edit list; backend.c only selects the factory and cannot establish that ownership. No startup implementation change was made.

Red, written/run before the fix:

```text
sublimite: EGL init failed: EGL/GL/Present unavailable (code=-10); using raster; no retry
gl_test:1154: FAIL usable
gl_test: bootstrap result=0 entered=1 returned_while_held=0
```

No green run: this finding remains unfixed. Its named optional review lane is intentionally failing and is excluded from the default suite.
### P2-1 §14 — blink polling

Remaining. The optional regression proves a pending frame creates a worker timer job. src/editor/editor.c still clamps pending GPU waits and manufactures poll events, and its caret hint is raster-only. Removing GL wakes alone would leave that duplicate timer and would violate the existing completion-continuation contract. Native readiness plus caret-aware editor integration must land together; no blink implementation change was made.

Red, written/run before the fix:

```text
gl_test:1149: FAIL no_timer
```

No green run: this finding remains unfixed. Its named optional review lane is intentionally failing and is excluded from the default suite.
### P2-1 §17 — EGL display lifetime

Done. Each backend owns a separate native XCB connection and EGLDisplay. It releases context/surface, calls eglTerminate after its objects are quiescent, releases EGL thread state, and disconnects its owned native connection before the platform owner is destroyed. Successful and failed initialization both use that teardown. Pure tests count termination across repeated failed attempts and refuse termination for an uninitialized display. Existing native shared-platform survival/fresh-connection tests now require independently owned displays; they SKIP here because EGL is unavailable.

Red, written/run before the fix:

```text
gl_test:1100: FAIL count.terminated==attempt+1
```

Green:

```text
gl_test: P2-1 section 17 PASS (final and repeated failed-init display teardown)
```
### P2-1 §18 — initialization timeout cleanup

Done. Driver retains the initialization work handle. Cleanup cancels and physically joins it before reading/shutting down the backend or freeing its arena. The init wait receives only that handle/generation. A bounded wait helper permits deterministic late-success and late-failure tests without changing the production driver deadline.

Red, written/run before the fix:

```text
gl_test:1119: FAIL !b.initialized && b.state==NULL
```

Green:

```text
gl_test: P2-1 section 18 PASS (timeout followed by late success/failure)
```
### P2-1 §19 — independent completion progress

Done. GL refuses bulk-only pools and selects a dedicated foreground lane when available, otherwise a raster lane. The test/bench driver provides a raster lane. Tests hold the same pool’s bulk worker and verify completion mail arrives independently on both supported lane types.

Red, written/run before the fix:

```text
gl_test:1097: FAIL rc==RENDER_ERR_UNSUPPORTED
```

Green:

```text
gl_test: P2-1 section 19 PASS (bulk-only refused; raster/foreground completion with blocked shared bulk)
```
### P2-1 §20 — paired identity

Remaining. An in-memory native-serial/editor-frame mismatch reproduces the existing joiner rejection. An explicit association must be emitted and consumed by tools/keyinject.c and tools/refwin_pairs.py, with an EGL integration lane in tests/refwin_test.c. Those files are outside the permitted edit list. No constant-offset inference or raw-serial T6 tracing was added.

Red, written/run before the fix:

```text
Traceback (most recent call last):
  File "<string>", line 6, in <module>
  File "tools/refwin_pairs.py", line 76, in complete_editor
    raise ValueError(f"pair {row['pair_id']}: editor Present serial has no unique matching key frame")
ValueError: pair 1: editor Present serial has no unique matching key frame
gl_test:1104: FAIL WIFEXITED(status) && WEXITSTATUS(status)==0
```

No green run: this finding remains unfixed. Its named optional review lane is intentionally failing and is excluded from the default suite.
### P2-1 §35 — actual renderer contention

Done. Surrogates now borrow the renderer’s actual pool and run each active workload plus queued-three; their names explicitly say surrogate. Additional integrated rows use production lineidx_build_start, find_literal over an immutable piece snapshot, and file_save_write with durable file replacement. Index restarts occur before the measured frame/allocator guard. Standalone index/find/save and queued-three rows verify progress and ownership; queued-three settles with find active and index/save queued. The module self-check verifies exact line count, expected search total, saved bytes, and queued work on one pool. Every refusal/error routes through cleanup. The old process-global bulk checksum sink was removed; results are per job. No new globals were introduced.

Red, written/run before the fix:

```text
gl_test:1116: FAIL rc==0 && shared
gl_test: renderer pool shared=0
```

Green:

```text
gl_test: renderer pool shared=1
gl_test: P2-1 section 35 PASS (renderer and contention share workers)
```

Additional ownership and integrated-workload red/green evidence:

```text
gl_test:1299: FAIL clean
gl_test: section 5 integrated acquisition faults PASS (bytes/tree/snapshot/index/find/save)
gl_bench: integrated index/find/save workloads missing
gl_bench: index_fixture progress=1048576 lines=16385 exact=1 expected=16385 building=0 failed=0
gl_bench: integrated_self_check kind=0 module=lineidx same_pool=1 correct=1
gl_bench: integrated_self_check kind=1 module=find_literal same_pool=1 correct=1
gl_bench: integrated_self_check kind=2 module=file_save_write same_pool=1 correct=1
gl_bench: integrated_self_check kind=3 module=queued3 same_pool=1 correct=1
```

## Verification

Release:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all
```

Final release build exits zero (M) [AC]. Final full ASan/UBSan make check exits zero: 60 test binaries and replay CLI contracts passed (M) [AC]. Final release and sanitizer GL suites, surrogate/module-backed workload self-checks, and the optional missing-EGL native lane also exit zero (M) [AC]. The last rebuild only corrected the printed index counter unit to index_source_bytes; it does not change workload or renderer behavior. Earlier full-suite failure was the test’s finite lifetime trace registry being consumed by the added worker fixtures; frozen trace conformance now runs before those fixtures. The GCC typedef-shadow collision introduced by including the file API in the benchmark test translation unit is resolved by renaming the frozen test’s local file token in its private include; tests/render_test.c is unchanged.


```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make -j4 check
```

```text
check: 60 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
```

Additional release checks:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 build/tests/gl_test
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/gl_bench --bulk-self-check
DISPLAY=:99 EDIT_DISPLAY=:99 build/bench/gl_bench --integrated-self-check
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 sh tools/test_gl_optional_native.sh build/san/tests/gl_test
```

```text
gl_bench: index_fixture progress=1048576 lines=16385 exact=1 expected=16385 building=0 failed=0
gl_bench: integrated_self_check kind=0 module=lineidx same_pool=1 correct=1
gl_bench: integrated_self_check kind=1 module=find_literal same_pool=1 correct=1
gl_bench: integrated_self_check kind=2 module=file_save_write same_pool=1 correct=1
gl_bench: integrated_self_check kind=3 module=queued3 same_pool=1 correct=1
gl_review: native diagnostics SKIP: Xvfb/EGL context unavailable (result=-9; :99 has no DRI3)
```

Optional red lanes for the remaining findings (all intentionally return failure):

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/gl_test --review p2-13
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/gl_test --review p2-14
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/gl_test --review p2-20
```

GL fuzzer:

```sh
DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/fuzz/gl_fuzz -max_total_time=60 -timeout=10
```

```text
Done 1826398 runs in 61 second(s)
```

The GL completion-state fuzzer completed cleanly for the required duration (M) [AC]. No native GL completion or performance claim is inferred from this deterministic state fuzzer.

Native GL tests on :99 explicitly SKIP because this Xvfb fixture lacks DRI3/EGL rendering. Conditional real-display lanes remain for context binding/readback, shared-platform survival with separate displays, initialization/resize, native swap/fence faults, completion continuation, framebuffer resize/close/fresh initialization, and benchmark faults after GL initialization/during active rendered contention. Pure backend-identity, teardown dispatch, timeout ownership, independent-lane, workload and acquisition-fault tests run without a GL context. A real display with EGL and matching PIXMAP Present is still required for native acceptance, paired EGL validation, process-wide blink wake measurements and GL latency rows.

## Decisions and limits

See docs/decisions/edit-40t.md for ownership and pool choices. The GL driver’s existing deadline and blink wake mechanism remain; this bead removes the timeout cleanup race and bulk starvation, not the separately deferred blink timer. The editor startup and paired-tool changes remain explicit coordinator work, not implicit future behavior of this patch.

Outside-scope observations were not fixed: lineidx_building can become false before the final result message has been adopted, so the benchmark waits for lineidx_complete before validating/restarting; general work/raster/editor findings from the review belong to their assigned workers. The existing test translation unit contains older globals; this patch adds none and removes the benchmark’s global checksum sink.

Work stopped adding scope at the mandated cutoff. Final acceptance is partial because the three named integration findings remain open and native GL/Present validation skipped. No performance gate pass is claimed from the loaded-box progress checks. The coordinator must rerun native GL checks on a real EGL/Present-capable display and sanitizer checks with leak detection enabled.
