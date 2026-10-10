# edit-q0g-s9 — rebase resolution report

Replayed `0cf661f` onto main `e80bd51`; resulting commit `5bbdb06` on branch `wt/edit-q0g`. Rebase completed, with no unmerged paths.

## Conflict resolution

Only `tests/cli_test.c` was unmerged.

- First hunk: retained main’s `decoy_open`, `same_display_handoff`, and four-argument `session(..., ipc_server *decoy)` signature. The session contract now says only pass/fail, preserving bead §36.
- Second hunk: retained main’s private IPC runtime, same-display handoff, isolated startup, different-display IPC-only decoy, three sessions, and cleanup. Removed all three `ran == 2` success/skip branches and required `ran == 0` for every session.
- Auto-merged checks retain main’s decoy drain/no-handoff assertions together with the bead’s forced-timeout negative control, delayed real backend initialization, and frame-zero startup trace assertions. Auto-merged production main retains display-specific IPC arbitration and the new main-entry startup timestamp.

Read the bead report and decisions for edit-q0g, edit-og2, edit-fff, and edit-o4o. Main’s memory census and benchmark diagnostics remain intact. The combined CLI regression passed both ordinary and delayed backend suites. The full suite later exposed the raster fixture interaction described below. The only test-code resolution beyond retaining main’s decoy code is removal of the obsolete timeout skip handling from all three session calls.

Only authorized Git writes were used: `git add tests/cli_test.c docs/worker-reports/edit-q0g-s9-rebase.md` and `GIT_EDITOR=true git rebase --continue`; after the rebase, `git add tests/raster_test.c docs/worker-reports/edit-q0g-s9-rebase.md` staged the fixture fix and final report. The first continuation returned 1 because the external worktree index was read-only in the sandbox; the identical command succeeded with sandbox escalation (exit 0). No automatic approval review rejected an action. `HANDOFF.md` and `docs/worklog/` are untouched.

## Validation

Compilers: GCC 13.3.0 release with C11 `-Wall -Wextra -Werror -Wshadow -Wconversion`; Clang 18.1.3 ASan/UBSan checks. All live X11 verification uses `DISPLAY=:99 EDIT_DISPLAY=:99`; different-display decoy `:98` is an IPC endpoint string only, with no X11 connection. Sanitizer runs use `ASAN_OPTIONS=detect_leaks=0`: leaks are off.

`DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 all`: **exit 0**. Tail:

```text
gcc -O2 -g -msse2 -pthread build/rel/bench/tabs_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/tabs_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/trace_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/trace_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/undo_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/undo_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/utf8_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/utf8_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/view_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/view_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/work_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/work_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/x11_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/x11_bench
gcc -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
make all exit code: 0
```

`ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make -j4 check`: initial full run **exit 2**, at `raster_test` native resize step 0. No ASan/UBSan error was reported. A direct local-socket preflight returned `Operation not permitted` under the sandbox; the full check ran with sandbox escalation for Xvfb access.

Full logs: `/tmp/edit-q0g-s9-current-all.log`, `/tmp/edit-q0g-s9-current-check.log`.

The combined sanitizer CLI regression passed: same-display launcher handoff (`child_exit=0 handoffs=1`), isolated startup and different-display decoy bypass (`handoffs=0`), frame-zero trace endpoints, delayed production backend completion, three session journals, and dump-error handling. Its `FAIL: window not created within 0 s` is the expected forced-timeout negative control; the outer test confirms `§36 forced startup timeout correctly failed` and returns success.

While the shared display lock was occupied, independent pure sanitizer tests each exited 0 with leaks off:

```text
x11_alignment_test: §6 ok
x11_xi2_test: ok
display guard: real display hostname/unix:0 refused, using :99
display guard: real display localhost:00.0 refused, using :99
display_guard_test: §39 ok
Pure checks exit codes: alignment=0 xi2=0 display_guard=0
```

The zero-display alias cases above are pure policy tests and do not open an X11 connection. The suite initially waited for another worktree’s :99 lock; the owner was inspected read-only, and no foreign process, lock, server, or input was modified.

## Raster fixture interaction and rerun

The initial suite failed with:

```text
native resize step=0 pixel=0,0 got=00445566 expected=00234567
render_test:465: FAIL pixels[(size_t)y*width+x]==expected
```

The readback was the preceding cancellation fixture’s color. `fence_outlives_test` shuts down its backend after queuing Present, but retains the platform window; `raster_native_resize_test` previously reused that window immediately. Minimal fix: at the start of `raster_native_resize_test`, when `g_up` is true, call `work_pool_shutdown(&g_pool)`, `plat_shutdown(&g_plat)`, and set `g_up=false` before `render_test_prepare`. This gives the independent resize contract a fresh native window and workers. All grow/shrink, changed-cell, margin, and pixel assertions remain unchanged, and every resize step still uses the same window within the contract. No production rendering, input, or timing code was changed.

Initial check tail:

```text
P2.5b section 15: GREEN
P2.5b section 17: GREEN
G11 UI completion: both event orders, delayed Present, no worker jobs or timer passed
P2.5b section G11: GREEN
raster_test: kernels PASS (SSE2 == scalar on 4000 random grids, 10240 blends, partitions)
raster law2: windows=10000 allocations=0 guard=0 scope=input->submit
render no-malloc: ASan guard inactive; release run required
render_test: PASS (strips, bounds, cells, lifecycle, ownership, hooks, allocator)
raster pixels: PASS inline caret, full-window scalar comparison and zero worker jobs
raster pixels: PASS full + 10 individual partial comparisons (disconnected/boundaries/unchanged rows)
raster fence: PASS (raw completion rejected, queued fence job joined before backend release)
make: *** [Makefile:105: check] Error 1
make check exit code: 2
```

Final release rebuild after the fixture fix: **exit 0**. Tail:

```text
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -include tests/display_guard.h -MMD -MP -c tests/raster_test.c -o build/rel/tests/raster_test.o
gcc -O2 -g -msse2 -pthread build/rel/tests/raster_test.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/tests/raster_test
make all exit code: 0
```

Full sanitizer rerun: **exit 143**, stopped at the time cutoff after passing the combined CLI/editor/file/search/font tests again and GL gate. Shared :99 contention delayed it until the cutoff. Only this task’s subprocesses were terminated; no foreign process or Xvfb server was modified. This is not accepted full-suite validation. Tail:

```text
review §8 GREEN: full mailbox drains, terminal discovery completion arrives
font_test: fallback fontconfig=1 cjk='/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc' emoji='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
font_test: all passed
== build/san/tests/gl_gate_test
waiting for display :99 (held by 4050683)
gl_gate_test: ok
== build/san/tests/gl_test
gl_bench:463: FAIL !gl_bench_fault("arena")
gl_bench:482: FAIL !gl_bench_fault("prepare")
libEGL warning: DRI3 error: Could not get DRI3 device
libEGL warning: Ensure your X server supports DRI3 to get accelerated rendering
make: *** [Makefile:105: check] Terminated
make check exit code: 143
```

Focused sanitizer verification of the fixture fix completed before cutoff cleanup: `ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/raster_test --live-only`: **exit 0**. It exercises the preceding cancelled Present and the corrected resize contract together, retaining every pixel assertion. Tail:

```text
P2.5b section G11: GREEN
raster_test: kernels PASS (SSE2 == scalar on 4000 random grids, 10240 blends, partitions)
raster pixels: PASS inline caret, full-window scalar comparison and zero worker jobs
raster pixels: PASS full + 10 individual partial comparisons (disconnected/boundaries/unchanged rows)
raster fence: PASS (raw completion rejected, queued fence job joined before backend release)
render native resize: PASS (grow/shrink, changed cells, fractional margins, native window pixels)
raster_test: PASS (requested conformance, upload ordering, typing allocations, live XShm)
raster interaction check exit code: 0
```

Logs: `/tmp/edit-q0g-s9-fixed-all.log`, `/tmp/edit-q0g-s9-fixed-check.log`, `/tmp/edit-q0g-s9-raster-interaction.log`. Leaks were off on all sanitizer runs.


## Existing limitations

The bead explicitly leaves §21 exact consecutive server keymap-generation snapshots unimplemented; optional `map_generations_pending` remains outside the default suite. Hardware XI2 validation and performance gaps remain as documented by the bead. No source conflict is unreconciled. Required full `make check` exit-0 acceptance remains unmet; the actual completed run returned 2. Shared display contention consumed most of the budget, and the time budget was exceeded during the initial run and follow-up verification. The fixture fix and report are staged after rebase completion; no subsequent commit/amend is authorized or performed. Final `git diff --cached --check` passed.
