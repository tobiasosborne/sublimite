# edit-e6x.24 worker report (Sonnet, budget reached; verification incomplete)

Changed (worktree .wt/edit-e6x.24): src/x11/clip.c, src/x11/clip.h, tests/x11_clip_test.c, docs/decisions/P2.2h.md (new), src/x11/STATUS.md (section appended).

Done: receive realloc growth, copy and Latin-1->UTF-8 run on a private src/work bulk worker behind the mailbox. The shrink/free at the end, and frees of blobs of 1 MiB or more, run there too. mem accounting is charged by the UI thread before the worker acts and released when the worker reports, so it is exact at quiescence. New clip.h API: x11_clip_ui_bytes, x11_clip_max_poll_ns(p, reset, &cpu_ns), x11_clip_buf_new/_data/_free, x11_clip_set_buf (zero-copy publish). x11_clip_busy now counts pending frees.

Red (counters added, old receive path), san build, load about 5-18, Not charging [bat-status file says Not charging, treat as AC]:
  FAIL 64 MiB UTF8 INCR: UI thread touched 133955584 bulk bytes (limit 1048576)
  FAIL 64 MiB UTF8 INCR: worst UI poll 63.897 ms (wall)
  FAIL 8 MiB Latin-1 INCR: UI thread touched 33030144 bulk bytes; worst poll 43.499 ms
Green (release build, load 26, Not charging): "64 MiB in 1716 ms, UI bulk bytes 0, worst UI poll 20.852 ms wall / 0.307 ms CPU" and "8 MiB Latin-1 ... UI bulk bytes 0, 3.645 ms wall / 0.224 ms CPU"; x11_clip_test: ok (2 runs; CPU 0.2-0.6 ms (M)[Not charging]). Wall spikes on this loaded box are preemption (CPU time is the asserted metric, limit 2 ms release, 20 ms under ASan because ASan's allocator inflated it to 5.5 ms CPU).

NOT verified: after the last edits (worker-failure test for >64 MiB Latin-1 expansion, ASan CPU limit macro) the san x11_clip_test had passed everything except the since-relaxed CPU limit, and the new failure test passed in that run. The full `make check` was still running in the background when the budget ended (output /tmp/claude-1000/-home-tobias-Projects-editor/6e346364-e270-4c93-8f6d-5b873dbec77a/tasks/b3bdv9omt.output). `make all`, `make fuzz` and the fuzz run (fuzz/x11 clipboard fuzzer uses clip.c) were NOT run by me: coordinator must run make all, make check (DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=1), make fuzz and a fuzz run, and a leak check.

Open: plat_clip_set still memcpy's on the caller (borrowed buffer); use x11_clip_set_buf. Serving converts Latin-1 per chunk on the UI thread. Clip polls the worker at a 1 ms wake while jobs are out (x11.c does not poll the pool eventfd; proposal in P2.2h.md). The "no allocation when typing is queued" guard check is only active in the release test (ASan owns malloc). libxcb still reads each 256 KiB reply on the UI thread.
