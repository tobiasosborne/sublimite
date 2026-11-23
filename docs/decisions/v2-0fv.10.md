# v2-0fv.10 · L1 scan harvest

Source pin `6fec2e8` (`/home/tobias/Projects/sublimite-v1-oracle`). The
module keeps v1's SSE2 algorithm: count by 255-block byte-lane chunks
and skip a chunk whole unless it holds the target rank. It drops one
function with no callers and the one unreachable return. The tests now
run every differential case against PROT_NONE guard pages.

## API

`src/scan/scan.h` exports `scan_counts`, `scan_count` and
`scan_find_nth_newline`. The header states the bounds contract that the
tests enforce: a call reads exactly `[p, p + n)`, and `n == 0` reads
nothing. v1 callers (`piece.c`: `scan_count`, `scan_find_nth_newline`;
`lineidx.c`: both, and `.nonascii`) use exactly this surface.

## No CPU dispatch

SSE2 belongs to the x86-64 base ISA (P), so every CPU this build targets
runs the kernels. There is no feature to detect. `base_cpu_has_*` stays
unused here. If an AVX2 variant ever opens from a measured need (Law 1),
it dispatches through `base_cpu_has_avx2()` and is tested against the
same scalar reference. `src/scan/scan.o` has no undefined symbols
(`nm -u`), so it needs no `tools/system-symbols.txt` grant.

## Invariants of kept state

The module has no globals and keeps no state across calls. Each loop
variable is bounded as follows.

| State | Invariant |
|---|---|
| `i` (both scanners) | `i <= n`. `[p, p + i)` is fully accounted for and nothing at or past `p + n` is ever loaded. Whole-block loads happen only while `n - i >= BLOCK`, and the scalar tail reads `p[i]` only for `i < n`. |
| `acc_nl`, `acc_hi`, `acc` | Per-lane match counts for the current chunk. A lane gains at most 1 per block and a chunk is at most `CHUNK_BLOCKS = 255` blocks, so no lane wraps (P). |
| `r` (`scan_count`) | Exact newline and high-byte counts of `[p, p + i)`. |
| `k` (`scan_find_nth_newline`) | Rank of the target among the newlines of `[p + i, p + n)`. Each skipped chunk subtracts its count. |
| `q`, `k`, `m` (`select_newline`) | Precondition: the chunk's whole blocks from `q` hold more than `k` newlines, counted with the same compare on the same bytes. `m` holds the unvisited match bits of block `q`. The walk therefore stops inside the chunk, and no bound or fallback return is needed. |

## Not carried over from v1

- `scan_find_byte`: has no caller in v1 production code (`grep` over `src/` at the pin; only `scan.c` and the tests). It also duplicates `memchr`.
- `return NULL; /* unreachable */` after the chunk descent: replaced by `select_newline`, whose loop ends by the stated precondition.
- `_mm_storeu_si128` to a stack array in `hsum_bytes`: two `_mm_cvtsi128_si64` lane extracts give the same sum without the round trip.
- Header guard `SCAN_H`: renamed `SCAN_SCAN_H` (module path, like `BASE_BASE_H`).
- Test heap fixture and `__asan_address_is_poisoned` exact-bound probe: every case now runs flush against PROT_NONE guards in every build (rel, ASan). The sanitizer-specific matrix is gone.
- Test `core_vm_*` guard helpers: v2 `os` offers no PROT_NONE call, so the test and fuzzer map guards with `mmap`/`mprotect` directly.
- Test rank list `{17, 255, 256, 300, total + 1000}` and the 600-rank cap: replaced by every rank (exact reference offsets) for spans up to 8 KiB and first/second/middle/last-two ranks for 8 MiB spans. No underived numbers remain.
- Fuzzer's `scan_find_byte` queries, `sysconf` and silent `return 0` on mmap failure: the function is gone, the page size comes from `os_page_size()`, and a fixture failure traps (fail closed).
- Bench `--self-check-tail`, `--self-check-oracle`, `report_gate*`, `bench_verdict*`, `harness.h`: these judged gates that no longer exist.
- Bench thresholds (G) as exit status: see below.

## Bench: gates become tracked numbers

v1 `bench/scan_bench.c` exits nonzero when a row misses its gates.
These are (G) p50 and p99 ≥ 12 GB/s for `scan_count` and
`scan_find_nth_newline`, and a `scan_count_G6_basis` row at p50 ≥ 16 GB/s
with p99 ≥ 12 GB/s. v1's last recorded run (`src/scan/STATUS.md` at the
pin) missed all of them on a loaded box: count 4.36/3.29 GB/s and nth
5.25/4.93 GB/s p50/p99, (M)[AC], load1=8.35. Law 1 makes benches tracked
numbers, so the v2 bench prints p50/p99 (ns and GB/s) with a power and
load stamp, and exits 0 on every timing outcome. A wrong answer from a
timed call still exits 1: that is a correctness failure, not a miss.
The thresholds are recorded here as v1 history and are not v2 gates.

v2 run, `make build/rel/bench/scan_bench && build/rel/bench/scan_bench`
(other agents' builds may share the box):

```text
(M)[AC] BAT0=Charging load1=1.39 at start
(M)[AC] BAT0=Charging load1=1.36 at end
lines=15334873 nonascii=41516204; cached anonymous buffer, primitive only
scan_count p50=127892202 ns (8.40 GB/s) p99=139410301 ns (7.70 GB/s), 20 runs over 1073741824 bytes
scan_find_nth_newline(last) p50=118103180 ns (9.09 GB/s) p99=131643966 ns (8.16 GB/s), 20 runs over 1073741824 bytes
exit=0
```

The A/B run linked the same bench object against v1 `scan.c`, compiled
into `build/scan-ab/` and alternated v1, v2, v1, v2. It measured
(M)[AC] load1 1.08–1.14. p50 agreed within run-to-run spread:
count 8.37/8.39/8.47/8.38 GB/s and nth 9.21/9.20/9.14/9.18 GB/s.

## Lanes

`tests/scan_test.c` is in `SAN_SUITES`, so it runs under clang
ASan+UBSan and as a gcc `-O2` release binary. `fuzz/scan_fuzz.c` is in
`FUZZ_SUITES`, with seeds in `fuzz/corpus/scan/`. The module is not in
TSan, because it has no threads or shared state. It is not in Law 2
either, because it calls no allocator and has no imports at all.
`bench/scan_bench` has its own rule in the Makefile and links
`libtext.a` and `libcore.a`.

## Red and green

The guard fixture prints its layout and proves it can fail. Forked
probes read `begin - 1` and `end`, which must take SIGSEGV, and
`begin` and `end - 1`, which must not. An unarmed guard fails the suite
before any differential case runs.

Red 1, header and test first (no implementation):

```text
$ make build/san/tests/scan_test
tests/scan_test.c:121:(.text+0x2c2c): undefined reference to `scan_count'
tests/scan_test.c:114:(.text+0x30b9): undefined reference to `scan_find_nth_newline'
make: *** [Makefile:149: build/san/tests/scan_test] Error 1
```

Red 2 is a one-byte overread mutant: both scalar tails read `i <= n`.

```text
$ sed 's/for (; i < n; i++)/for (; i <= n; i++)/' src/scan/scan.c > build/scan-red/scan_overread.c
$ clang <WARN> -fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Isrc -c build/scan-red/scan_overread.c -o build/scan-red/scan_overread.o
$ clang -fsanitize=address,undefined -pthread build/san/tests/scan_test.o build/scan-red/scan_overread.o build/san/libcore.a -lm -ldl -o build/scan-red/scan_test_overread
$ build/scan-red/scan_test_overread; echo exit=$?
scan_test: small guard page=4096: [0x7cf528c76000 PROT_NONE 4096][0x7cf528c77000 RW 12288][0x7cf528c7a000 PROT_NONE 4096]; read begin-1=SIGSEGV begin=ok end-1=ok end=SIGSEGV
==221630==ERROR: AddressSanitizer: SEGV on unknown address 0x7cf528c7a000 (pc 0x5b0583c3d04e bp 0x7ffc6bd9ea90 sp 0x7ffc6bd9ea10 T0)
==221630==The signal is caused by a READ memory access.
    #0 0x5b0583c3d04e in scan_count build/scan-red/scan_overread.c:51:28
    #1 0x5b0583c3c64f in check_range tests/scan_test.c:121:46
    #2 0x5b0583c3c26f in run_case tests/scan_test.c:150:9
    #3 0x5b0583c39bd0 in sweep tests/scan_test.c:170:9
exit=1
```

The faulting address `0x…7a000` is exactly `end`, the first PROT_NONE
byte. The gcc `-O2` release build of the same mutant dies with
`Segmentation fault (core dumped)`, exit 139. The fuzzer linked against
the mutant crashes on its first seed (`AddressSanitizer: SEGV … in
scan_count scan_overread.c:51:23`).

Red 3 is a seeded differential mismatch: `CHUNK_BLOCKS = 256`, so a byte
lane can wrap.

```text
$ build/scan-red/scan_test_lane256; echo exit=$?
tests/scan_test.c:125: FAIL all-newlines n=4113 count got 17/0 want 4113/0
tests/scan_test.c:116: FAIL all-newlines n=4113 nth k=0 got 4096 want 0
...
exit=1
```

Red 4 is an inert guard: a test copy that maps the guard pages RW.

```text
$ build/scan-red/scan_test_inert; echo exit=$?
scan_test: small guard page=4096: [...][...][...]; read begin-1=ok begin=ok end-1=ok end=ok
scan_test: FAIL guard pages not armed; overread checks would be inert
exit=1
```

Green:

```text
$ build/san/tests/scan_test
scan_test: small guard page=4096: [0x71c43a2a4000 PROT_NONE 4096][0x71c43a2a5000 RW 12288][0x71c43a2a8000 PROT_NONE 4096]; read begin-1=SIGSEGV begin=ok end-1=ok end=SIGSEGV
scan_test: large guard page=4096: [0x71c4365fd000 PROT_NONE 4096][0x71c4365fe000 RW 8392704][0x71c436dff000 PROT_NONE 4096]; read begin-1=SIGSEGV begin=ok end-1=ok end=SIGSEGV
scan_test: PASS (sweep 0..8192 both guards, 8388625-byte large spans, guards armed)
$ make all        # exit 0
$ make check      # check: ok (check-san: 12 test binaries passed; check-law2: 3 release suites passed)
$ make check-tsan # check-tsan: 5 threaded suites passed
$ make fuzz-smoke # == build/fuzz/scan_fuzz fuzz/corpus/scan ... Done 6 runs; fuzz-smoke: 5 fuzzers replayed their seeds
```

## Files

- `src/scan/scan.h`, `src/scan/scan.c` hold the module: 22 + 85 lines (M, `wc -l src/scan/*`).
- `tests/scan_test.c` runs the guard-page differential suite with a positive guard control.
- `fuzz/scan_fuzz.c` and `fuzz/corpus/scan/` hold the guarded scalar differential fuzzer and its six seeds.
- `bench/scan_bench.c` and its `Makefile` rule produce the tracked throughput numbers.
- `tests/lanes.mk` registers the suites.
