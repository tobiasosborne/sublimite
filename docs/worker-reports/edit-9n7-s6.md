# edit-9n7 worker report (s6)

The raster fuzzer is running again. The repair stays entirely in its transport:
`xcb_change_gc` now uses a live-transport mock, and every margin fill requires
and consumes a foreground-update flag. The foreground mask and value pointer
are checked. Rectangle fill and flush were already mocked; inspection of
`cpu_paint_margins` found no other missing native call from edit-u82. No new
allocation or production change was introduced.

Full suite acceptance remains pending the sandbox and unrelated failures below.
Only this bead was attempted. No git or bd command was run, and no coordinator
file was edited.

## Files touched

- `fuzz/raster_fuzz.c`: foreground-update regression assertion, mock and macro mapping.
- `docs/decisions/edit-9n7.md`: transport contract and verification choice.
- `docs/worker-reports/edit-9n7-s6.md`: this report.

Evidence and compatibility scratch copies are under `build/edit-9n7/` in this
worktree. The neighboring worktree was read only. The production raster backend
was not edited.

## Red: test first, before the mock

The foreground-update flag and assertion were written before the implementation;
`build/edit-9n7/raster_fuzz-red.c` preserves that version. The existing guaranteed
ownership schedule submits a margin-bearing frame even for empty input (P: no
input bytes are needed to reach the unconditional submission). The red build
succeeded, and libFuzzer crashed on its initial empty input before any mutation.

BAT0 immediately before the run: `Not charging`, hence (M)[AC]. Everything in the
following output, including source locations, addresses, process identifiers
and exit status, is captured (M)[AC] from the sanitizer run rather than a chosen
count, duration or budget.

```text
$ build/fuzz/raster_fuzz -artifact_prefix=build/edit-9n7/
AddressSanitizer:DEADLYSIGNAL
=================================================================
==4==ERROR: AddressSanitizer: SEGV on unknown address 0x00000000001a (pc 0x7b185fce6b04 bp 0x7ffe2eafd870 sp 0x7ffe2eafd7d0 T0)
==4==The signal is caused by a READ memory access.
==4==Hint: address points to the zero page.
    #0 0x7b185fce6b04 in xcb_send_request_with_fds64 (/lib/x86_64-linux-gnu/libxcb.so.1+0x13b04) (BuildId: 16796503bce879b4d9697eb4fada10eeb6af4040)
    #1 0x7b185fce72dc in xcb_send_request (/lib/x86_64-linux-gnu/libxcb.so.1+0x142dc) (BuildId: 16796503bce879b4d9697eb4fada10eeb6af4040)
    #2 0x7b185fceb333 in xcb_change_gc (/lib/x86_64-linux-gnu/libxcb.so.1+0x18333) (BuildId: 16796503bce879b4d9697eb4fada10eeb6af4040)
    #3 0x62f6223d11b1 in cpu_paint_margins /home/tobias/Projects/sublimite/.wt/edit-9n7-s6/fuzz/../src/raster/raster.c:347:5
    #4 0x62f6223b94a9 in cpu_submit /home/tobias/Projects/sublimite/.wt/edit-9n7-s6/fuzz/../src/raster/raster.c:569:5
    #5 0x62f6223fac7b in render_backend_submit /home/tobias/Projects/sublimite/.wt/edit-9n7-s6/src/render/render.c:335:10
    #6 0x62f6223ca166 in ownership_fuzz /home/tobias/Projects/sublimite/.wt/edit-9n7-s6/fuzz/raster_fuzz.c:372:5
    #7 0x62f6223ca166 in LLVMFuzzerTestOneInput /home/tobias/Projects/sublimite/.wt/edit-9n7-s6/fuzz/raster_fuzz.c:446:5
    #8 0x62f6222bfe74 in fuzzer::Fuzzer::ExecuteCallback(unsigned char const*, unsigned long) (/home/tobias/Projects/sublimite/.wt/edit-9n7-s6/build/fuzz/raster_fuzz+0xa0e74) (BuildId: 3142589bc18c305269e852b3fd882db9fec0e794)
    #9 0x62f6222c10a1 in fuzzer::Fuzzer::ReadAndExecuteSeedCorpora(std::vector<fuzzer::SizedFile, std::allocator<fuzzer::SizedFile>>&) (/home/tobias/Projects/sublimite/.wt/edit-9n7-s6/build/fuzz/raster_fuzz+0xa20a1) (BuildId: 3142589bc18c305269e852b3fd882db9fec0e794)
    #10 0x62f6222c1697 in fuzzer::Fuzzer::Loop(std::vector<fuzzer::SizedFile, std::allocator<fuzzer::SizedFile>>&) (/home/tobias/Projects/sublimite/.wt/edit-9n7-s6/build/fuzz/raster_fuzz+0xa2697) (BuildId: 3142589bc18c305269e852b3fd882db9fec0e794)
    #11 0x62f6222aeb8f in fuzzer::FuzzerDriver(int*, char***, int (*)(unsigned char const*, unsigned long)) (/home/tobias/Projects/sublimite/.wt/edit-9n7-s6/build/fuzz/raster_fuzz+0x8fb8f) (BuildId: 3142589bc18c305269e852b3fd882db9fec0e794)
    #12 0x62f6222d9216 in main (/home/tobias/Projects/sublimite/.wt/edit-9n7-s6/build/fuzz/raster_fuzz+0xba216) (BuildId: 3142589bc18c305269e852b3fd882db9fec0e794)
    #13 0x7b185f62a1c9 in __libc_start_call_main csu/../sysdeps/nptl/libc_start_call_main.h:58:16
    #14 0x7b185f62a28a in __libc_start_main csu/../csu/libc-start.c:360:3
    #15 0x62f6222a3b74 in _start (/home/tobias/Projects/sublimite/.wt/edit-9n7-s6/build/fuzz/raster_fuzz+0x84b74) (BuildId: 3142589bc18c305269e852b3fd882db9fec0e794)

AddressSanitizer can not provide additional info.
SUMMARY: AddressSanitizer: SEGV (/lib/x86_64-linux-gnu/libxcb.so.1+0x13b04) (BuildId: 16796503bce879b4d9697eb4fada10eeb6af4040) in xcb_send_request_with_fds64
==4==ABORTING
MS: 0 ; base unit: 0000000000000000000000000000000000000000


artifact_prefix='build/edit-9n7/'; Test unit written to build/edit-9n7/crash-da39a3ee5e6b4b0d3255bfef95601890afd80709
Base64: 
exit: 1
```

## Green: same artifact and bounded fuzzing

BAT0 was checked before each run: `Not charging`, hence (M)[AC]. Leak detection
was disabled after the required suite attempt established that LeakSanitizer
cannot operate under this sandbox's ptrace. AddressSanitizer and UBSan stayed
active. The repaired binary replays the empty crash artifact successfully:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/fuzz/raster_fuzz build/edit-9n7/crash-da39a3ee5e6b4b0d3255bfef95601890afd80709
Executed build/edit-9n7/crash-da39a3ee5e6b4b0d3255bfef95601890afd80709 in 0 ms
exit: 0
```

The replay timing and statuses above are captured (M)[AC]; this is a fixed-input
replay, not the bounded fuzz campaign. The campaign's requested duration of
60 seconds repeats the prior edit-cee measured duration (M)[AC], supplied in
`../edit-cee-s6/docs/worker-reports/edit-cee-s6.md`; it is an (E) verification
sample, not a new performance budget. Actual duration and executed-input count
below come directly from libFuzzer's final summary (M)[AC].

```text
$ ASAN_OPTIONS=detect_leaks=0 build/fuzz/raster_fuzz -max_total_time=60 -artifact_prefix=build/edit-9n7/
#63387	DONE   cov: 2566 ft: 15149 corp: 576/8114b lim: 122 exec/s: 1039 rss: 258Mb
Done 63387 runs in 61 second(s)
exit: 0
```

No crash or sanitizer error. Launch load was 3.57 (M)[AC], the leading field
read from `/proc/loadavg`, not a load budget.

## Compatibility with the pending edit-cee landing

The binding prohibition on git took precedence over the suggested git diff
command. Direct `diff -u` comparisons showed the neighboring changes in the
inline deadline fuzz operation and raster completion timer. The mock/test hunks
are in the transport, outside those edits. An exact-context patch dry run on a
scratch copy of the neighboring harness succeeded:

```text
$ patch --dry-run --fuzz=0 build/edit-9n7/raster-cee-before.c < build/edit-9n7/mock.patch
checking file build/edit-9n7/raster-cee-before.c
exit: 0
```

The zero fuzz allowance means exact hunk matching (P); the success status is
captured (M)[AC]. The combined scratch harness uses the neighbor's pending
backend and deadline operation plus this transport repair. It compiled with the
required Clang warnings and sanitizers and passed the same bounded campaign:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/edit-9n7/raster-cee_fuzz -max_total_time=60 -artifact_prefix=build/edit-9n7/
#69915	DONE   cov: 2548 ft: 11156 corp: 565/8895b lim: 142 exec/s: 1146 rss: 248Mb
Done 69915 runs in 61 second(s)
exit: 0
```

Duration, input count and status are captured (M)[AC], with the requested
sample derived as above. BAT0: `Not charging`; launch load 2.75 (M)[AC], from
`/proc/loadavg`. This verifies the combined behavior without pulling the
neighbor's production changes into this bead.

## Required builds and suite verification

`make all` passed with installed GCC 13.3.0 (M)[AC], from `gcc --version`.
The sanitizer and libFuzzer builds passed with installed Clang 18.1.3 (M)[AC],
from `clang --version`, using the required C11 warning flags. The full release
build log is `build/edit-9n7/make-all.log`; the final verification is:

```text
$ make all
make: Nothing to be done for 'all'.
exit: 0
```

The status is captured (M)[AC]; BAT0 was `Not charging` before final verification.
The initial `make check` built every sanitizer suite, then stopped in the base
suite at the sandbox's LeakSanitizer limitation:

```text
LeakSanitizer has encountered a fatal error.
HINT: LeakSanitizer does not work under ptrace (strace, gdb, etc)
```

With `ASAN_OPTIONS=detect_leaks=0`, full `make check` got past the base suite and
stopped in CLI at local IPC setup:

```text
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:206: check] Error 1
```

These are captured diagnostics (M)[AC], not a clean full `make check` result.
A non-display `make check TEST_SAN=...` selection stopped at the live cases in
`dataloss_test`. Every remaining selected binary was then run serially under
Clang ASan/UBSan so that failure did not hide later results. Selection followed
Makefile's classification, additionally excluding native `file_kill_test`.
The complete inventory is `build/edit-9n7/nondisplay-results.txt`: 61 binaries
(M)[AC], derived by counting inventory entries; 58 passed (M)[AC], derived by
counting PASS/success-exit entries. The failures are `dataloss_test`, `ipc_test`
and `journal_kill_test`, detailed below. The display-dependent data-loss cases
need to be classified alongside the native suites. All other selected suites
passed, including `raster_scroll_test`, `render_test` and `lineidx_test`.

The headless native-margin transport regression also passed:

```text
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review edit-u82
edit-u82: fractional strips, stable frame, theme change, geometry shrink, no allocations passed
exit: 0
```

Status is captured (M)[AC]. The replay CLI shell contract passed with the same
ASan/UBSan runtime setting. The display-lock shell contract was interrupted
while waiting for the shared display lane; it is not counted as passed.

## Display and benchmark limitations

Display suites fail inside this sandbox because the X socket cannot be used.
The targeted native raster resize check produced this captured (M)[AC] result:

```text
$ DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 build/san/tests/raster_test --review P2-1-9
raster_test:605: FAIL render_test_prepare(&b,&cfg)==RENDER_OK
exit: 1
```

The display name and review selector are existing fixture identifiers. The
coordinator must rerun the display suites, including data-loss and file-kill
native cases, on Xvfb :99. Native pixels and leak checking are not certified here.

The release raster benchmark returned successfully with a native skip:

```text
SKIP G3/G3z/G3i: cannot open display; not measured
```

BAT0 was `Not charging` immediately before the command. The log is
`build/edit-9n7/raster-bench.log`; it establishes no runtime performance result.
No product hot path changed.

## Found outside this bead; not fixed

- `ipc_test` cannot initialize its Unix socket inside the sandbox. Captured
  (M)[AC] output:

  ```text
  init rc=4 errno=1 (Operation not permitted)
  FAIL tests/ipc_test.c:130: init_rc==IPC_OK
  exit: 1
  ```

- `journal_kill_test` fails replay. Its existing visitor requires the legacy
  window payload; `journal_set_window` writes `JOURNAL_WINDOW_BYTES`. Direct
  source inspection confirms this mismatch, already reported by edit-u82.
  Captured (M)[AC] output:

  ```text
  journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
  exit: 1
  ```

- `dataloss_test` and `file_kill_test` are absent from Makefile's display lane
  despite native editor/platform dependencies. The data-loss sweep reached
  `editor_open returned -103` and the `two_process_save` failure (M)[AC]. These
  need the coordinator's native rerun; no Makefile or fixture was changed.

- An extra GCC compile of the fuzz-only harness fails on the existing packed
  Present-event pointer cast in `transport_special`. A scratch reconstruction
  of the pre-bead harness produces the same GCC error, so it predates this fix.
  Captured (M)[AC] diagnostic:

  ```text
  error: converting a packed 'xcb_present_complete_notify_event_t' pointer (alignment 1) to a 'xcb_generic_event_t' pointer (alignment 4) may result in an unaligned pointer value [-Werror=address-of-packed-member]
  ```

  This does not affect successful `make all`: that target builds release
  artifacts, while fuzz harnesses use Clang. Both compiler outputs are preserved
  as `build/edit-9n7/fuzz-gcc.log` and `fuzz-gcc-before.log`.

The transport repair and red/green acceptance are complete. Full suite
acceptance remains pending coordinator native/leak verification and the
unrelated IPC/journal findings above. No other bead was fixed.
