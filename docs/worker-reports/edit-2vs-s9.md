# edit-2vs report (session 9)

## Root cause
The fence job (src/raster/raster.c fence_job_fn) reads st->fence and the xcb
connection. A frame can be reported complete without that job: the platform
loop delivers Present COMPLETE straight to the adapter (bench pump_view ->
present_done -> render_backend_event), so the adapter goes inactive while the
fence job is still queued or polling. cpu_submit of the next frame then set
st->nhandles = 0, losing that handle (and rewriting st->fence under it);
cpu_shutdown only joined the current handles, so free(v->state) raced the job.
Details: docs/decisions/edit-2vs.md.

## Done
- tests/raster_test.c: fence_outlives_test (live X, deterministic): all 4 raster
  workers are held by gate jobs so the fence job stays queued; frame 1 is
  completed via render_backend_signal; frame 2 is submitted; backend shutdown +
  free(state); then the gate is released.
- src/raster/raster.c: cpu_join_jobs() (cancel all retained handles, wait for
  work_handle_finished, clear list), called in cpu_submit and cpu_shutdown.
  No new globals, no allocation added.

## Red run (before fix, build/san/tests/raster_test)
```
==1852829==ERROR: AddressSanitizer: heap-use-after-free on address 0x7bca7c97a598 at pc 0x57f07d2edeea bp 0x7bca75c61b30 sp 0x7bca75c61b28
READ of size 8 at 0x7bca7c97a598 thread T15
    #0 0x57f07d2edee9 in fence_job_fn /home/tobias/Projects/sublimite/.wt/edit-2vs/src/raster/raster.c:156:43
    #1 0x57f07d31977d in worker_main /home/tobias/Projects/sublimite/.wt/edit-2vs/src/work/work.c:82:13
    #2 0x57f07d21982c in asan_thread_start(void*) asan_interceptors.cpp.o
    #3 0x7bca7ec9cb83 in start_thread nptl/pthread_create.c:447:8
    #4 0x7bca7ed29ecb in clone3 misc/../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

0x7bca7c97a598 is located 265624 bytes inside of 265856-byte region [0x7bca7c939800,0x7bca7c97a680)
freed by thread T0 here:
    #0 0x57f07d21baaa in free (/home/tobias/Projects/sublimite/.wt/edit-2vs/build/san/tests/raster_test+0x14eaaa) (BuildId: 939aa80946f1675a191e6faaa748ba8e7539fa6a)
    #1 0x57f07d26a4e7 in fence_outlives_test /home/tobias/Projects/sublimite/.wt/edit-2vs/tests/raster_test.c:454:5
    #2 0x57f07d265a0d in main /home/tobias/Projects/sublimite/.wt/edit-2vs/tests/raster_test.c:489:9
    #3 0x7bca7ec2a1c9 in __libc_start_call_main csu/../sysdeps/nptl/libc_start_call_main.h:58:16
    #4 0x7bca7ec2a28a in __libc_start_main csu/../csu/libc-start.c:360:3
...
SUMMARY: AddressSanitizer: heap-use-after-free .../src/raster/raster.c:156:43 in fence_job_fn
```

## Green run
```
raster pixels: PASS full + 10 individual partial comparisons (...)
raster fence: PASS (queued fence job joined before backend release)
raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)
```
- make all (gcc 13, -Werror): exit 0.
- make check (clang 18 ASan/UBSan, DISPLAY=:99, ASAN_OPTIONS=detect_leaks=0, leaks NOT checked): exit 0.
- fuzz/raster_fuzz -max_total_time=60: 102081 runs in 61 s, clean.

## 20x loops (build/san, ASan, DISPLAY=:99)
- file_kill_test: pass=20 fail=0
- raster_test: pass=20 fail=0
(file_kill_test did not fail before the fix either on this box; the original
report was intermittent, so the loop is not evidence by itself; the red test is.)

## Missing / out of scope
- Leak check not run (detect_leaks=0); coordinator should run leaks-on.
- The previous frame's fence job is now cancelled at next submit, so an unseen
  Present idle notify is dropped; pixmap-reuse-before-idle semantics were
  already this loose. Not changed.
- Possible separate issue: two paths (platform callback and fence job) both
  signal PRESENT_COMPLETE; the render layer rejects the duplicate with
  RENDER_ERR_STATE, harmless but redundant.
