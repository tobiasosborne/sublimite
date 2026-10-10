# edit-e6x.27 worker report (session 8): gl bench honesty

Files: bench/gl_gate.h (new), bench/gl_bench.c, tests/gl_gate_test.c (new),
docs/decisions/edit-e6x.27.md, this report. src/gl/* untouched.
Power at run: "Not charging" -> (M)[AC]; load1 about 18 (loaded box): no timing is a verdict.

## Red run (tests/gl_gate_test.c vs a header holding the legacy logic)
    gl_gate.h:43:59: runtime error: index 4294967292 out of bounds for type 'char[4096]'
    AddressSanitizer: SEGV on unknown address ... READ memory access
      #0 memcmp  #3 gl_gate_elf_text_bytes  #4 run_elf  #5 main (gl_gate_test.c:42)
With the sh_name=0xfffffffc case removed the next failures were
`gl_gate_test:42: FAIL !run_elf(...)` (ELFCLASS32 accepted); the legacy judge (tag != [AC]
=> PASS, rounded 5560000, no required n, no dropped/partial notion) fails the remaining rows.

## Green run
    == build/san/tests/gl_gate_test
    gl_gate_test: ok
`make all` exit 0 (gcc 13, -Werror ...); `make check` (clang ASan/UBSan) exit 0, 
"check: ... test binaries passed", replay CLI tests all passed.
`./build/bench/gl_bench --bulk-self-check`: jobs=1 chunks_in_200ms=2215 overlapped=1; jobs=3 ... overlapped=1.
`--help` documents all scenarios, statuses and exit codes.

## Per finding
- BLOCKER 7 (ELF overflow): done. gl_gate_elf_text_bytes widens sh_name to u64, checks
  `off <= size && size - off >= 5`, rejects non-ELF64/LE, shnum 0; unit tests: wrapped name,
  name past table, wrong class.
- MAJOR 9 (power): done. Limits enforced under [AC], [bat], [unknown]; unknown + limits met
  => UNKNOWN (never PASS); battery misses => MISS (test with 100 ms samples). Typing now has a
  separate T4 row vs G1 1/2 ms; idle row gated 8 ms / 1e9/90 ns (provisional) with required n 5.
- MAJOR 10 (ingress + minimap): done as the allowed partial form. Ingress is the first
  statement; scroll shift / cell edit now inside the timed region in all modes; the 180000 ns
  minimap allowance is deducted from both percentile budgets; rows are PASS_PARTIAL, g_claim=no.
  Real minimap rendering is NOT added (needs piece/lineidx input): missing.
- MAJOR 11 (samples + bulk): done. Required 10000 per scenario (buffers enlarged); --quick or
  any short run => REFUSED, including scroll_10k (a miss still reports MISS at any n).
  New bulk_active (1 looping bulk job) and bulk_queued3 (1 active + 2 queued) scenarios,
  each full-frame (G3 partial) and typing (G1 T4), overlap verified else REFUSED; `--bulk-only`.
  Note the work pool allows one bulk worker, so "queued" jobs never start in the window.
- MAJOR 15 (exact p99): done. `p99*180 <= 1e9` (floor 5555555); tests at 5555555/5555556/5558000.
- MAJOR 14: untouched (note only).

## Missing / caveats
- The GL path was not exercised end to end: under Xvfb :99 EGL fails (no DRI3), the bench prints
  `status=SKIP reason=EGL_or_matching_Present_unsupported` and exits 2 (scroll-track init
  failures now also return 2). So the new rows (full_frame_T5_*, typing_T4_*, bulk_*) compile and
  the logic is unit-tested, but their printed output was not seen with real numbers.
- G3 itself is still not claimable: minimap and editor ingress/mutation are outside the bench.
- Idle row required n is 5 (isolated samples), not 10000, by design of G3i.
