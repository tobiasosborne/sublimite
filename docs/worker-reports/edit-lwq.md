# edit-lwq worker report (INCOMPLETE: token budget reached)

## Done (all 31 bench binaries build with gcc, -Werror)
- bench/harness.h: bench_report returns structural failure only (empty or dropped samples); bench_report_fp
  still returns 1 on a miss (pure helper, tests use it) and prints " MISS". bench_exit_code always 0,
  bench_merge_exit is "any non-zero", bench_gate_report structural only, LOADED/bench_qualify_load removed,
  bench_load1 kept (stamp), gate_line prints TRACK when both limits are 0.
- scan, view (eligible/INVALID/quiet_stamp removed, 1 ms / 5.6 ms -> TRACK), indent (TRACK), utf8 (5/10 us -> TRACK),
  trace (50 ns -> TRACK), tabs (1 MB -> TRACK; slice 0.5 ms prints MISS), keys, ipc, minimap, x11, undo (G10f -> TRACK note),
  journal, findui_count.h, work, find (G6v -> TRACK), scroll, savectl (G8d -> TRACK; always-3 exit removed),
  font, raster (exit 2 paths -> 0), gl_gate.h/gl_bench.c (gl_gate_exit -> 0, dropped still fails).
- tests/harness_test.c: removed bench_qualify_load/BENCH_LOADED checks, flipped exit-code expectations (NOT compiled or run).

## Not done
- editor_bench.c, editor_large_bench.c (bench_merge_exit(.,3) calls, G10/G11 TRACK), lineidx_bench.c, piece_bench.c,
  file_bench.c, render_bench.c, prewake_bench.c, zygote (rows already TRACK): not audited for custom exit logic.
  editor_bench.c:334/473 and editor_large_bench.c:244 rely on exit 3 / `track`; merge_exit now maps any non-zero to 1.
  editor_bench.c:473 asserts bench_merge_exit(rc,0)==1 - verify.
- G11 idle and G10 memory rows not yet set TRACK anywhere (editor_bench.c).
- font 50 us and keys 1 us / ipc 10 ms limits have no §0.2 derivation; left printing MISS (not in the brief's list).
- bench/interaction.h still rejects sample counts < 10000 (argument validation).
- tests/gl_gate_test.c may assert gl_gate_exit(MISS)==1 (not checked); tests/harness_test.c, `make check`, `make bench`
  not run; no before/after exit outputs captured.
- Shell note: some Bash greps were denied by the auto-mode classifier; used Read instead.
