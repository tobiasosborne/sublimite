# v2-0fv.12 · L1 indent harvest

Source pin `6fec2e8` (`/home/tobias/Projects/sublimite-v1-oracle`):
`src/indent/indent.{h,c}`, `tests/indent_test.c`, `fuzz/indent_fuzz.c`,
`bench/indent_bench.c`, decisions P4.2 and P4.5b. The behaviour of every
query is carried. Two edge cases change, and the change is stated below.
The interface is rebuilt: indent no longer names `piece`.

## Headline: indent reads a span iterator, not a piece tree

The dossier ([wave1-L1-harvest.md](wave1-L1-harvest.md) §3, §9 L1-5b) makes
this bead depend on L1-3 (`piece`). In v2 `src/piece/` is still the smoke
stub (`piece_app_id_len` only), and the piece API is being rebuilt from the
signed pool spec ([wave1-piece-pool.md](wave1-piece-pool.md)). A verbatim
harvest could neither compile nor be tested, so indent now reaches its text
through one type in its own header:

```c
typedef struct indent_text {
    void (*seek)(void *iter, uint64_t off);
    size_t (*next)(void *iter, const uint8_t **p);
    void *iter;
    uint64_t length;
} indent_text;
```

This is v1's own access pattern, `piece_iter_begin` and `piece_iter_next`,
named as the module's contract. A piece tree, a piece snapshot and a flat
buffer each adapt in about six lines: `seek` is `piece_iter_begin(it, tree,
off)` and `next` is `piece_iter_next`. Two of the three v1 callers already
held a tree (`input.c`, `paint.c`) and the third a snapshot (`buffers.c`).
The adapter belongs to the caller (L2 `edit`, the L4 paint path), so the
include DAG permits indent to depend on `piece` but it does not.

- **Invariant**: a query only reads bytes of the windows its header comment
  names. Each window is read by one forward `walk` that clips every span to
  the window's end (`walk_next`).
- **Simplest alternative**: wait for `piece` and harvest the v1 signatures
  unchanged. That blocks the bead on L1-3 and L1-4. It also ties the tests
  to the piece fixture and leaves the omission logic in place (next item).
- **Counter-example to a stateless `span(off)` callback**: on a tree of
  one-byte pieces, every span would cost an O(log n) descent. The 8 KiB
  typing window would then need 8192 descents per keystroke. A stateful
  iterator keeps each step O(1), which is why `seek` and `next` are
  separate.

### The selection needs no mapping

v1 previewed a selection `[lo, hi)` through `indent_source`, which mapped
logical offsets past `lo` up by `hi - lo`, and through `byte_reader`, which
re-seeked when a read crossed `lo`. Only one read ever crossed: brace's scan
of `[start, end)`, which runs through the cursor. v2 splits that scan at
the cursor. The backward typing window then ends at `lo` and the forward
window starts at `hi`. No window contains the selection, so every typing
read works directly on the text, and every returned offset is at most `lo`.
That offset is the same before and after the deletion. `indent_source`,
`source_offset`, `source_read` and the refill re-seek are gone.

## API, v1 → v2

| v1 | v2 | Why |
|---|---|---|
| `indent_on_enter`, `indent_on_enter_after_delete` | `indent_on_enter(text, lo, hi, out, &length)` | The plain form had no production caller (`input.c` called only `_after_delete`). `lo == hi` is the plain case. |
| `cap` and `ERR_CAPACITY` on Enter | `out[INDENT_ENTER_MAX]`, (P) `T + 2` | The leading run lies inside the backward window, so the output is bounded. The only caller passed a fixed buffer. |
| `indent_on_close_brace(_after_delete)` → `indent_edit {lo, hi, length, bytes[1]}` | `indent_on_close_brace(text, lo, hi, width, &from)` | `length` was always 1, `bytes[0]` always `'}'`, and `hi` always the cursor. That is three fields of state that carry no information. The edit is "replace `[from, lo)` with `'}'`". |
| `indent_style {bool uses_tabs; width}` passed to brace | `uint8_t width` | Brace never read `uses_tabs` ("advisory; existing bytes win"). |
| `bool uses_tabs` | `indent_kind {INDENT_SPACES, INDENT_TABS}` | Enum rule. |
| `indent_bracket_match(..., &match)` | `indent_bracket_match(..., indent_pair *)` | `paint.c` re-derived the source bracket by re-reading the byte (`strchr("()[]{}", ch)` and `source--`), which duplicated indent's choice. The pair returns both. |
| `indent_trailing_ws_ranges` | `indent_trailing_ws` | Same contract. `out` may be NULL only with `cap == 0`. |
| `indent_detect(snapshot, &style)` → code, `indent_detect_bytes` | `indent_style indent_detect(text)` | No error was possible except a NULL argument. `indent_detect_bytes` had no production caller. |
| `INDENT_ERR_ARGUMENT` for NULL pointers | preconditions in the header | A NULL argument is a caller bug that no input can produce. ARGUMENT remains for `width` outside 1..16, a value that can come from configuration. |

## Behaviour changes (each pinned by a test)

1. **'}' returns ERR_LIMIT only when the answer is undetermined.** v1
   required both line boundaries within `INDENT_TYPING_BYTES`, so typing
   `}` more than 8 KiB into any line, including a minified JSON line, was
   rejected. v2 decides from what the windows show. A byte other than SP or
   TAB on the cursor line, in either window, makes the answer a plain
   insertion (`*from == lo`) without reading further. LIMIT remains only
   when both windows hold SP/TAB alone and one of them stops short of the
   line's edge. Every result v1 returned with OK is unchanged. Tests:
   `limit_cases` (a 4T solid line, both ends) and the bound sweep against
   the model. P4.5b's contract holds: work is bounded, nothing is guessed,
   and nothing is written on LIMIT.
2. **"Nonblank" in detection means a byte other than SP, TAB or CR.** v1
   ended the leading run at a CR and then counted any later byte, SP or TAB
   included, as content, so `"\t\r \n"` voted tabs. v2 follows the header's
   own word "nonblank". Test: the `det[]` row `"\t\r \n  a\n"`.

Enter keeps v1's LIMIT exactly. The line start can lie past the backward
window, and the leading run is at the line start, so the answer really does
depend on unread bytes. See finding F1.

## Invariants of kept state

The module has no globals and keeps no state across calls. Each local
automaton and its fields satisfy the following.

| State | Invariant |
|---|---|
| `walk {t, pos, end}` | `pos <= end`. The spans returned so far cover `[pos0, pos)` exactly, and the iterator stands at `pos`. No byte at or past `end` is handed out. |
| `line_start` result | One past the last LF in `[lo - T, lo)`, or 0 when that window starts at 0, or `INDENT_NONE`. So `lo - start <= T` (P), and the Enter output is at most `INDENT_ENTER_MAX`. |
| `term` | The terminator of the cursor line as seen in `[hi, hi + T)`: LF, CRLF, end of text, or UNKNOWN when the window ends first. A LF at `hi` is never CRLF, because `typing_range_ok` refused that cursor. |
| `side` (`before_side`, `after_side`) | CONTENT: the window holds a byte of the cursor line other than SP/TAB (a lone CR counts). EDGE: SP/TAB only, up to the line's edge. UNKNOWN: SP/TAB only, up to the window's end short of the edge. `*start` is meaningful only for a before-EDGE. |
| `tail` (`after_side`) | BLANK: only SP/TAB were seen. CR: those were followed by one CR whose successor decides. |
| `stop`, `prev`, `column` (brace) | Over the SP/TAB run `[start, x)`: `stop` is one past the last completed indent stop (`start` if none), and `prev` is the one before it. `column < width` counts the columns since `stop`, and a tab completes the stop. |
| `trail` and `run` | TEXT: no open run. RUN: `[run, x)` is SP/TAB and reaches the current byte. RUN_CR: `[run, x - 1)` is SP/TAB, followed by one CR. A RUN_CR that meets SP/TAB restarts at that byte, because the lone CR was text. |
| `phase`, `lead`, `kind` (detect) | LEAD: the line so far is SP/TAB. `lead` counts them, and `kind` is TABS iff one is a tab. BLANK: a CR ended the run and only SP/TAB/CR followed. BODY: a byte other than SP/TAB/CR was seen. `lead` and `kind` are frozen once LEAD ends. |
| `votes` | Counts over complete lines in BODY with `lead > 0`. `gcd` is the GCD of the space-only `lead`s (0 if none). |
| `depth` (bracket) | 1 + the unmatched own-type openers (forward) or closers (backward) between the source and the scan position. |

There are no ASSERTs. Every condition in the module can come from input
and is a return code. The `indent_text` contract (`next` returns `n >= 1`
before the end) belongs to the caller's adapter. If it is violated, a walk
ends early instead of looping. No branch handles it beyond that.

## Not carried over from v1

- `indent_source`, `source_offset`, `source_read`, `byte_reader` and `reader_refill`'s cut re-seek: no window contains the selection (above).
- `BACK_BLOCK` (1024, underived): `line_start` and `before_side` walk the window forward and keep the last hit, so they need no copy. Backward bracket matching copies blocks of `INDENT_TYPING_BYTES`, the one stack size already justified.
- The `white` boolean and the stop scan over the whole line: these became the `side` enums and the before/after split.
- `bool in_leading, has_tab, has_content` in `detect_state`: these became `phase` and `indent_kind`.
- `indent_edit` and its flexible `bytes[1]` tail: always `"}"`.
- `if (leading > SIZE_MAX - newline) return INDENT_ERR_RANGE`: dead, because `leading <= T` (P).
- `add_range`'s `lo >= hi` skip: dead, because every emitted run holds at least one byte.
- `indent_detect_bytes`, plain `indent_on_enter`/`indent_on_close_brace`, NULL-argument checks: see the API table.
- `#include "piece/piece.h"`, the `<stdbool.h>` header dependency and the `EDIT_INDENT_H` guard (now `INDENT_INDENT_H`).
- Test: the piece fixture, `piece_init_mapped` guard pages and `edit_malloc_guard_*`. Every query now runs against PROT_NONE chunks (below). indent imports no allocator (`nm -u`: `memchr`, `memcpy`, `__memcpy_chk`, `__stack_chk_fail`, all granted `*`), so it has no Law-2 suite, as with scan and utf8.
- Fuzzer: the piece tree and `piece_snapshot`. It uses the same guarded chunk source and model as the test.
- Bench: `harness.h`, the `/tmp/edit-corpus` files (`ascii_code.c`, `log_1g.txt`) and the TRACK/MISS verdict machinery. The text is now generated, and the rows are tracked numbers that exit 0.

## Tests (`tests/indent_test.c`, SAN lane and gcc -O2 release)

`tests/indent_model.h` is shared with the fuzzer and holds two things:

- **`gtext`**: an `indent_text` whose every chunk sits alone on its own
  pages between PROT_NONE guards. Chunks alternate flush-right and
  flush-left. `gtext_allow` makes every chunk outside the query's readable
  windows PROT_NONE.
- **`model_*`**: the header contract restated over a flat logical byte
  array with whole-line reasoning. It shares no code with `indent.c`.
  `model_brace` computes v1's visual-column answer over the whole line
  (from v1's fuzz oracle) and separately what the windows show. It checks
  the two against each other (`consistent`).

Every query runs **guarded**, where a read outside its windows faults in
every build, and **straddled**, where spans cross every window edge, so a
missing clip gives a wrong answer.

- `guard_armed`: a forked positive control. The test fails before any case
  runs unless the outside chunks and both guard sides fault and the window
  chunks read.
- `v1_cases`: every v1 Enter, brace, bracket, trailing and detect case, on
  one span and on one-byte spans, plus RANGE and ARGUMENT.
- `limit_cases`: dossier acceptance. On a 4T blank line Enter and brace
  return LIMIT with `*length == 0`, the whole `out` buffer still `0xa5`,
  and `*from == INDENT_NONE`. Only the open-side window is readable. A
  giant selection `[T, 3T)` stays PROT_NONE and is never read. A 4T solid
  line answers brace with OK (change 1).
- `small_sweep`: every text of length ≤ 4 over the 7 byte classes the
  module distinguishes (SP, TAB, CR, LF, ordinary, `{`, `}`): 2801 texts.
  It runs every selection, every bracket cursor and window, trailing and
  detect, widths 1..4, guarded (one-byte chunks) and straddled (one span):
  1,096,968 query calls.
- `bound_sweep`: dossier acceptance ("typing-bound windows over
  boundary-straddling lines"). Cursor lines `head + fill·k1 | selection |
  fill·k2 + tail`, with k1 ∈ {2, T−1, T, T+1} and k2 ∈ {2, T−2, T−1, T,
  T+1}. These put the previous LF, the text start, the line's LF, its CR
  and the text end one byte inside and one byte outside each window (P).
  The fills are SP, TAB and `x`, the selections 0, 1 and T+1 bytes, and
  there are 4 heads and 4 tails: 8640 lines, each guarded and straddled.
- `long_cases`: brackets on both sides of the backward copy-block edges
  (multiples of T below the source), and a detection prefix whose cut line
  would vote tabs.

## Fuzzer (`fuzz/indent_fuzz.c`, FUZZ lane, 9 seeds in `fuzz/corpus/indent/`)

The input sets the text, which is optionally mapped onto the module's byte
classes, the cursor, the selection/window, the width and the chunk stride.
Every query runs straddled and then guarded against the model, plus the
RANGE and ARGUMENT contracts. The seeds are a short header, an empty text,
a CRLF selection, tab stops, nested brackets, mixed detection, a blank line
longer than the window (LIMIT), a blank line whose LF sits exactly at the
window's first byte (`long-blank-edge`), and a solid line longer than both
windows (change 1).

## Red and green

Red 1, header and test first (no `indent.c`):

```text
$ make build/san/tests/indent_test
tests/indent_test.c:133:(.text+0xc8b0): undefined reference to `indent_trailing_ws'
tests/indent_test.c:119:(.text+0xd8b4): undefined reference to `indent_bracket_match'
tests/indent_test.c:150:(.text+0xdd7d): undefined reference to `indent_detect'
make: *** [Makefile:150: build/san/tests/indent_test] Error 1
```

Mutants in `build/indent-red/`, each linked as
`gcc -O2 -g -msse2 -pthread build/rel/tests/indent_test.o build/indent-red/indent_<m>.o build/rel/libtext.a build/rel/libcore.a -lm -ldl`:

```text
# Red 2: walk_next without the clip (spans read past their window)
$ build/indent-red/indent_test_noclip; echo exit=$?
tests/indent_test.c:213: FAIL enter "    abc" [2,2): rc 0 length 5
...                                   (35479 FAIL lines)
*** buffer overflow detected ***: terminated
exit=134

# Red 3: back_bound one byte wider (reads lo - T - 1)
$ gdb -batch -ex run -ex bt --args build/indent-red/indent_test_bound
Program received signal SIGSEGV, Segmentation fault.
line_start (lo=32768, ...) at build/indent-red/indent_bound.c:77
#2  limit_cases () at tests/indent_test.c:344
        (the first PROT_NONE byte below the readable window; release exit=139)

# Red 4: back_bound one byte narrower (only the differential can see it)
$ build/indent-red/indent_test_narrow; echo exit=$?
tests/indent_test.c:359: FAIL enter over a giant selection: length 0
tests/indent_test.c:97: FAIL bound enter n=8194 lo=8192 hi=8192: rc 4 want 0, length 0 want 8193, tail untouched 1
tests/indent_test.c:106: FAIL bound brace ... width=4: rc 4 want 0, from 18446744073709551615 want 8192
indent_test: 4322 FAILURES
exit=1
```

Green:

```text
$ build/san/tests/indent_test        # (M)[bat] load1 9.26: 15.4 s
indent_test: guard page=4096: chunk0 (outside)=SIGSEGV chunk1=ok chunk1-1=SIGSEGV chunk2=ok chunk2+len=SIGSEGV chunk3 (outside)=SIGSEGV
indent_test: small sweep: 2801 texts over 7 byte classes, length <= 4, 1096968 query calls, guarded and straddled
indent_test: bound sweep: 8640 cursor lines around INDENT_TYPING_BYTES=8192, guarded and straddled
indent_test: all passed
$ build/rel/tests/indent_test        # 9.2 s, same lines
```

Fuzzer red: the narrowed-window mutant linked into the fuzzer traps on the
`long-blank-edge` seed (a LF exactly at `c - T`). That seed was added
after the first seed set missed this edge:

```text
$ build/indent-red/indent_fuzz_narrow -runs=0 fuzz/corpus/indent; echo exit=$?
indent_fuzz: fuzz/indent_fuzz.c:44: rc == model_enter(L, ln, lo, want, &wlen)
==268859== ERROR: libFuzzer: deadly signal
exit=77
```

Fuzzing, (M)[bat] load1 8.69: the run took about 5 min from the seeds,
with no crash. It found 258 new coverage inputs, which were kept out of the
committed corpus (`build/indent-fuzz-found/`). That was followed by
`-max_total_time=60 -max_len=24600` over both sets: 2242 runs, exit 0.
Throughput is low by design, because every input maps and protects one
chunk per `stride` bytes.

Acceptance, (M)[bat] load1 3.38 at start, 16:59–17:01 UTC:

```text
make all          exit=0
make check        exit=0   (check_symbols: ok; check_dag: ok; check-layers: 6 isolated layer links passed;
                            check-san: 15 test binaries passed; check-law2: 3 release suites passed; check: ok)
make check-tsan   exit=0   (check-tsan: 5 threaded suites passed)
make fuzz-smoke   exit=0   (fuzz-smoke: 7 fuzzers replayed their seeds)
```

## Bench (`bench/indent_bench.c`, tracked numbers)

`make build/rel/bench/indent_bench && build/rel/bench/indent_bench` (other
agents' builds share the box):

```text
(M)[bat] BAT0=Discharging load1=3.63 at start
enter viewport_rows=300 flat       p50=     256 ns p99=     311 ns  (20000 samples)
brace viewport_rows=300 flat       p50=     101 ns p99=     116 ns  (20000 samples)
bracket viewport_rows=300 flat     p50=     111 ns p99=    5849 ns  (20000 samples)
enter viewport_rows=300 span=1     p50=   13144 ns p99=   42052 ns  (20000 samples)
brace viewport_rows=300 span=1     p50=   13644 ns p99=   48525 ns  (20000 samples)
bracket viewport_rows=300 span=1   p50=    2859 ns p99=   66383 ns  (20000 samples)
long_line_limit enter+brace flat   p50=    4624 ns p99=    5213 ns  (20000 samples)
long_line_limit enter+brace span=1 p50=   39108 ns p99=  144222 ns  (20000 samples)
detect prefix=64KiB flat           p50=   96186 ns p99=  249123 ns  (2000 samples)
(M)[bat] BAT0=Discharging load1=3.63 at end
```

v1's last recorded run (P4.5b, (M)[AC] load1 7.22, piece tree of a mapped
file): ordinary Enter p99 0.769 µs, bracket 3.151 µs, long-line Enter
19.327 µs, brace 21.053 µs, detect 196.381 µs. The flat rows sit at or
below those numbers.

The `span=1` rows are the worst a span iterator can present: one indirect
call per byte. `line_start` and `before_side` walk the whole backward
window forward, because a forward-only iterator cannot stop at the nearest
LF. On one-byte spans that is up to 8192 calls per keystroke: 13 µs p50,
42–49 µs p99, about 10× inside the 0.5 ms UI slice (G, v1
`perf/01-perf-target.md`). A real piece tree has pieces made of typing
bursts. Under Law 1 this is a tracked number. If a trace ever names it,
the remedy is a backward probe in blocks; it was not added on speculation.

## Findings for the coordinator (not fixed here)

- **F1 (trial use: typing rejected).** Enter more than `INDENT_TYPING_BYTES`
  into a line returns LIMIT in v1 and in v2. v1's editor counted it as a
  rejected command, so pressing Enter past column 8192 of a long line did
  nothing. v2 removes the same rejection for `}` (change 1), but Enter
  needs the line's leading run, which lies at the line start. Options for
  the `edit` bead: insert a bare terminator with no indent when the start
  is out of the window (a deliberate, documented guess), or ask `lineidx`
  for the line start when an exact index exists. The decision is the
  owner's. Until then `edit` must reject or defer on LIMIT without
  mutating and without a synchronous retry (P4.5b, unchanged).
- **F2.** The piece adapter belongs with `edit` and the L4 paint path when
  those land: `seek = piece_iter_begin`, `next = piece_iter_next`, plus a
  snapshot twin for detection on a worker.

## Files

- `src/indent/indent.h`, `src/indent/indent.c`: 118 + 399 lines (M, `wc -l`). v1 had 86 + 300, written densely with several statements per line. v2 uses one statement per line and has more comments.
- `tests/indent_test.c` (557), `tests/indent_model.h` (311), registered in `SAN_SUITES`.
- `fuzz/indent_fuzz.c` (130) and `fuzz/corpus/indent/` (9 seeds), registered in `FUZZ_SUITES`.
- `bench/indent_bench.c` (165) and its `Makefile` rule, which links `libtext.a` and `libcore.a`.
- `tests/lanes.mk`.
