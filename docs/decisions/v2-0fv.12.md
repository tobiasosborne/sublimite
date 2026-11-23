# v2-0fv.12 · L1 indent harvest

Source pin `6fec2e8` (`/home/tobias/Projects/sublimite-v1-oracle`):
`src/indent/indent.{h,c}`, `tests/indent_test.c`, `fuzz/indent_fuzz.c`,
`bench/indent_bench.c`, decisions P4.2 and P4.5b. Round 1 landed as
`9557035`. Round 2 (the advisor's send-back, below) rebuilt the typing
queries. This note describes the module as it now stands and records what
changed in round 2.

## 1. indent reads a span iterator, not a piece tree (round 1)

The dossier ([wave1-L1-harvest.md](wave1-L1-harvest.md) §3, §9 L1-5b) has
this bead depend on `piece`. In v2, `src/piece/` is still the smoke stub,
and the piece API is being rebuilt from the signed pool spec. indent
therefore reaches its text through one type in its own header:

```c
typedef struct indent_text {
    void (*seek)(void *iter, uint64_t off);
    size_t (*next)(void *iter, const uint8_t **p);
    void *iter;
    uint64_t length;
} indent_text;
```

This is v1's own access pattern (`piece_iter_begin`, `piece_iter_next`)
named as a contract. A piece tree, a piece snapshot and a flat buffer each
adapt in about six lines. The adapter belongs to the caller. The iterator
is stateful because a stateless `span(off)` callback would cost one tree
descent per span, which is 8192 descents per keystroke on one-byte pieces.

## 2. Round 2: the caller hands in the line start

**Ruling (advisor).** Enter is never a no-op. A typing query that can
answer LIMIT pushes a policy into every caller, so ERR_LIMIT leaves the
contract. The question behind it: why does indent search for line
boundaries at all? Both 8 KiB windows, LIMIT, the probe one byte before the
line start, and the 13 µs backward re-walk on one-byte spans existed only
because indent found line edges itself through a forward-only iterator.
Finding newlines is `scan`'s job, and knowing line starts is `lineidx`'s.
The caller that issues Enter has just painted the cursor line.

**Answer: (b) for the line start, (a) for the line end.**

- **Built, line start.** Both typing queries take `line_start` from the
  caller and are total. Enter reads the leading run forward from
  `line_start`, stopping at the first other byte or at `lo`, and costs
  O(indent). '}' tests `[line_start, lo)` for blanks with the same scan
  that computes the indent stops, then scans from `hi` up to the first
  byte other than SP/TAB. That is O(indent + trailing blanks). No line edge
  is ever searched for.
- **Counter-example, line end.** Neither query needs `line_end`, and a
  caller cannot always know it cheaply. Take Enter near the start of a
  long no-wrap line, such as a minified JSON file opened mapped, before
  `lineidx` is exact (PLAN §4.3: degraded `{value, exact=false}`). The
  painted row knows its `line_start` (`viewport_row_cert`, `exact_line`).
  But layout keeps only a bounded window of an unwrapped line's tail (v1
  `paint.c`: "No-wrap may skip an unbounded unused line tail"). Piece keeps
  newline summaries of mapped ORIGINAL lazily (wave1-piece-pool.md). So
  finding the line's terminator is an O(line) scan, which is exactly the
  window problem moved into the caller. Enter used `line_end` only for the
  line-break bytes. '}' needs only the first byte other than SP/TAB after
  `hi`: if that byte is a terminator, the scan has found the line end on
  its own; if it is text, the answer is a plain insertion.
- **The line break moves to the buffer.** `indent_detect` now also votes
  the buffer's line break (`indent_style.eol`). The caller inserts
  `eol` and then the run. That is one policy in one place, the same place
  as the indent style. It is the usual editor model: one line-ending mode
  per buffer, detected on open.
- **A range, not a copy.** Enter returns the run as `[line_start, run.hi)`
  of the text. The run lies below `lo`, so deleting the selection does not
  move it. That removes the output buffer, its cap, `INDENT_ENTER_MAX` and
  the CAPACITY path. The range is the right shape for the piece insert.
  `edit` can insert by reference (v1 `piece_insert_ref`; v2 streams
  ORIGINAL descriptors, wave1-piece-pool.md §2), or copy at most
  `INDENT_BLANK_BYTES` into its stage when the journal needs the bytes.
  Either way the choice is `edit`'s, and indent no longer owns storage.
- **Unknown line start.** A caller without the start passes `lo` and gets
  an empty run (a bare line break) and a plain `'}'`. A caller that wants
  better does a bounded backward newline search, which is `scan`'s job.
  That policy lives in `edit`, where the knowledge is.

**Invariant.** A typing query reads `[line_start, …)` up to the first byte
other than SP/TAB (or `lo`), and `[hi, …)` up to the first byte other than
SP/TAB plus one after a CR, each part at most `INDENT_BLANK_BYTES` blanks.
It also reads the byte before `line_start` for the precondition.

**Simplest alternative, rejected.** Keep the windows and map LIMIT to a
defined answer inside indent. That keeps the 8 KiB search, the probe and
the cost proportional to the window, and the answer would still be worse
than the caller's, which knows the line start from the row or the index.

## 3. API (round 2)

| Query | Signature | Notes |
|---|---|---|
| Enter | `indent_range indent_on_enter(text, line_start, lo)` | The selection is irrelevant: the run lies in `[line_start, lo)`. |
| '}' | `uint64_t indent_on_close_brace(text, line_start, lo, hi, width)` | Replacing `[from, lo)` with `'}'` is the edit. |
| Brackets | `indent_pair indent_bracket_match(text, cursor, lo, hi)` | Returns source and mate. v1's `paint.c` re-derived the source by re-reading the byte. |
| Trailing blanks | `size_t indent_trailing_ws(text, lo, hi, out, cap)` | Returns the total. Stores `min(total, cap)`. |
| Style | `indent_style indent_detect(text)` | `{kind, eol, width}`. |

`indent_code` is gone. Every query is total.

### Error causes classified (A15)

| v1 cause | Who can produce it | v2 |
|---|---|---|
| `lo > hi`, `hi > length`, `cursor > length`, `line_start > lo` | caller bug only: the UI thread owns the buffer and computes the offsets from it | `ASSERT` |
| `line_start` not 0, `lo` or one past a LF | caller bug only (the caller passes the row's or the index's exact start, or `lo`) | `ASSERT`; the byte is read first, then asserted (os.h: ASSERT arguments have no side effects) |
| `width` outside 1..16 | configuration input, which `config` validates on load; `indent_detect` returns 1..16 | `ASSERT` here; validation is config's obligation (F2) |
| caret between CR and LF | **input**: paste `"\r"` directly before a LF, and the caret sits between them | no special case. Enter copies the run, and '}' sees the CR as text: plain insertion. Neither query reads at `lo` beyond its scans, so it is just another position. Pinned in `defined_cases`. |
| NULL pointers | caller bug only | header precondition (round 1) |
| ERR_LIMIT (8 KiB windows) | input: long lines | removed; see §4 |
| ERR_CAPACITY (Enter output, trailing ranges) | Enter: none (bounded); trailing: input | Enter returns a range; trailing returns its count |

ASSERT is the one macro from `os.h`. `nm -u indent.o` is `os_assert_fail`,
`__memcpy_chk`, `__stack_chk_fail`. Thirteen precondition violations are
pinned by forked death tests (`assert_cases`, which expects SIGABRT).

## 4. Behaviour changes against v1 (each pinned by a test)

v1 behaviour on ordinary lines stays pinned. Every typing case that v1
answered (no LIMIT and no RANGE, judged by v1's own window rule in
`model_v1_answered`) is compared with the v1 whole-line model:
122,586 cases in `indent_test` plus every fuzz input. The leading run
equals v1's leading bytes, and `from` equals v1's.

1. **Enter is never a no-op.** v1 returned LIMIT, a rejected keystroke,
   when the line start or end lay more than 8192 bytes from the caret.
   Now Enter returns the run for any line length. Test: `defined_cases`,
   a 4B solid line with a 4-space indent, which gives `[0,4)` (finding F1
   of round 1, resolved).
2. **'}' is never rejected.** v1 returned LIMIT on the same long lines.
   Now: a plain insertion on text, a dedent on a blank line.
3. **Degraded answers, not errors.** A leading run of more than
   `INDENT_BLANK_BYTES` blanks gives Enter an empty run (a bare line
   break), and either part of '}' with more than that many blanks counts
   as text (a plain insertion). Only lines of more than 8 KiB of blanks get
   there. Tests: `defined_cases` (B, B+1, 4B) and the bound sweep, with
   runs of 0, 2, B and B+1 on both sides of the caret.
4. **The line break is the buffer's.** v1 Enter copied the cursor line's
   terminator, else the previous line's, else LF. Now `indent_detect`
   votes: every LF in the prefix votes CRLF when a CR precedes it, else LF,
   and a tie chooses LF. The two agree on every uniformly terminated
   file. They differ only on mixed files, where v2 no longer propagates a
   stray CRLF. Tests: the `det[]` rows `"a\r\nb\r\nc\n"` (CRLF),
   `"a\r\nb\n"` (a tie gives LF), `"\r\n"`, and the prefix case.
5. **Caret between CR and LF** is a position, not RANGE (§3).
6. **Detection: "nonblank" means a byte other than SP, TAB or CR** (round
   1). v1 counted `"\t\r \n"` as content.

## 5. Numbers

- **`INDENT_BLANK_BYTES` = 8192, (G)/(M).** The worst typing query is '}'
  on a blank line. It reads `2B + 1` bytes: B blanks before the caret, then
  B blanks and the LF after it. The worst text is one-byte spans, one
  iterator call per byte. The row `worst_blank brace 2B+1 bytes span=1`
  measured p99 642,046 ns at B = 16,384, (M)[AC] load1 4.31. That is
  19.6 ns/byte, so `2B + 1` bytes fit the 0.5 ms UI slice (G, v1
  `perf/01-perf-target.md`, G1 row: "UI slices ≤ 0.5 ms") only for
  B ≤ 12,755. B is the largest power of two below that. Re-measured at
  B = 8192: p99 205,640 ns (M)[AC] load1 3.55. It equals v1's number by
  arithmetic, not by carrying it.
- **`INDENT_PREFIX_BYTES` = 64 KiB: product behaviour (v1 P4.2) with an
  (E) rationale.** A text's style shows in its first screens. 64 KiB is
  about 1,600 lines of 40 bytes. The cost per open stays constant however
  large the file: detection p99 204,568 ns (M)[AC], flat.
- **Backward bracket block: (E).** One seek per block. The block reuses B,
  so the stack scratch introduces no new number.
- `INDENT_MAX_WIDTH` 16 and `INDENT_DEFAULT_WIDTH` 4: product behaviour
  (v1 P4.2).

## 6. Invariants of kept state

The module has no globals and keeps no state across calls.

| State | Invariant |
|---|---|
| `walk {t, pos, end}` | `pos <= end`. The spans returned so far cover the window up to `pos`, and the iterator stands at `pos`. No byte at or past `end` is handed out. |
| Enter `run` | `[line_start, run)` is SP/TAB, `run <= lo`, and `run - line_start <= B + 1`. The walk ends at `min(lo, line_start + B + 1)`, so a run longer than B is seen and returned empty. |
| '}' `stop`, `prev`, `column` | Over the SP/TAB run `[line_start, x)`: `stop` is one past the last completed indent stop (`line_start` if none), and `prev` is the stop before it. `column < width` counts the columns since `stop`, and a tab completes the stop. |
| `tail` and `blanks` (`blank_tail`) | BLANK: `blanks <= B` SP/TAB bytes from `hi` were seen. CR: they were followed by one CR, whose successor decides. Within `B + 2` bytes the answer is always decided, so a walk that runs out has reached the end of the text. |
| `trail` and `run` (trailing blanks) | TEXT: no open run. RUN: `[run, x)` is SP/TAB and reaches the current byte. RUN_CR: `[run, x - 1)` is SP/TAB, followed by one CR. |
| `phase`, `lead`, `kind`, `prev` (detect) | LEAD: the line so far is SP/TAB. `lead` counts them, and `kind` is TABS iff one is a tab. BLANK: a CR ended the run and only SP/TAB/CR followed. BODY: a byte other than SP/TAB/CR was seen. `prev` is the byte before the current one, so it decides the vote of each LF. |
| `votes` | Counts over complete lines (`tabs`, `spaces`, `gcd` of the space-only `lead`s) and over the LFs in the prefix (`crlf`, `lf`). |
| `depth` (brackets) | 1 + the unmatched own-type openers (forward) or closers (backward) between the source and the scan position. |

## 7. Not carried over from v1

- `indent_source`, `source_offset`, `source_read`, `byte_reader`, the cut re-seek (round 1): no read ever crosses the selection.
- `line_start`/`line_end` searches, `INDENT_TYPING_BYTES`, both typing windows, `term`, `side`, `before_side`, `back_bound`, `forward_end`, `typing_range_ok`'s CR|LF probe, the start−2 terminator probe (round 2): the caller hands in the line start, and the line break is the buffer's.
- `indent_code` and every error value: §3.
- The Enter output buffer, `cap`, `INDENT_ENTER_MAX` and CAPACITY: Enter returns a range.
- `indent_edit` and its `bytes[1]` tail (always `"}"`), `uses_tabs` on the brace call (never read), `bool` style fields (now enums).
- `BACK_BLOCK` (1024, underived), `leading > SIZE_MAX - newline` (dead), `add_range`'s `lo >= hi` skip (dead), `indent_detect_bytes` and the plain/`_after_delete` twins (no production caller).
- Test: the piece fixture and `edit_malloc_guard_*` (indent imports no allocator). Bench: `harness.h`, the `/tmp/edit-corpus` files and the TRACK/MISS verdicts.

## 8. Tests

`tests/indent_model.h`, shared with the fuzzer, holds three things:

- **`gtext`**: every chunk alone on its own pages between PROT_NONE
  guards, alternating flush-right and flush-left. `gtext_allow` makes every
  chunk outside the query's readable bytes PROT_NONE.
- **The v2 model**: the header contract over the flat text.
- **The v1 model**: whole-line Enter lead and '}' answer, plus v1's window
  rule for when v1 answered.

Every query runs **guarded** and **straddled** (spans cross every edge).
`tests/indent_test.c` (SAN lane and gcc -O2 release):

- `guard_armed`: the forked positive control. The test fails before any
  case unless the outside chunks and both guard sides fault.
- `assert_cases`: 13 precondition violations die by SIGABRT. The legal
  boundaries (width 16, `line_start == lo == length`) pass.
- `v1_cases`: every v1 Enter (as a lead length), brace, bracket, trailing
  and detect case, on one span and on one-byte spans, plus the EOL rows.
- `defined_cases`: v1's LIMIT and RANGE cases with their v2 answers:
  degraded at B+1 and 4B and exact at B, giant selection never read,
  4B solid line, caret between CR and LF, unknown line start.
- `small_sweep`: every text of length ≤ 4 over the 7 byte classes: 2801
  texts, every selection, with the true and the unknown line start, every
  bracket cursor and window, trailing blanks, detect, widths 1..4. That is
  2,528,364 query calls.
- `bound_sweep`: cursor lines `head + fill·k1 | selection | fill·k2 +
  tail`, with k ∈ {0, 2, B, B+1} on both sides (P: one byte inside and
  one byte past the bound). Fills are SP, TAB and `x`. Selections are 0, 1
  and B+2 bytes. Heads are `""`, `"\n"`, `"\r\n"`, `"x\n"`. Tails are
  `""`, LF, CRLF, `x`, CR, CR CR. Each of the 10,368 lines runs with the
  true and the unknown start, guarded and straddled.
- `long_cases`: brackets on both sides of the backward block edges, and
  the detection prefix, whose cut line would vote tabs and whose unread
  tail would vote LF.

`fuzz/indent_fuzz.c` runs every query against both models, guarded and
straddled, with the true line start and the unknown one. There are 12
seeds in `fuzz/corpus/indent/`, among them blank runs at and past B on
both sides of the caret, a 2B solid line, and a caret between CR and LF.

## 9. Red and green (round 2)

Red 1, the new header and test against the round-1 implementation:

```text
$ make build/rel/tests/indent_test
src/indent/indent.c:13:22: error: 'INDENT_TYPING_BYTES' undeclared (first use in this function)
src/indent/indent.c:101:1: error: unknown type name 'indent_code'
src/indent/indent.c:105:45: error: 'INDENT_ERR_RANGE' undeclared (first use in this function)
```

Mutants of the final `indent.c` in `build/indent-red2/`, each linked as
`gcc -O2 -g -msse2 -pthread build/rel/tests/indent_test.o build/indent-red2/indent_<m>.o build/rel/libtext.a build/rel/libcore.a -lm -ldl`:

```text
== pastlo   (Enter's walk not clipped at lo)          exit=139
tests/indent_test.c:270: FAIL enter "    abc" at 2: [0,4) want 2 blanks
Program received signal SIGSEGV ... indent_on_enter (ls=0, lo=16384) ... defined_cases () at tests/indent_test.c:385
== degrade  ('}' degrades at lo - ls >= B, not > B)   exit=1, 2426 FAILs
tests/indent_test.c:97: FAIL bound brace n=8192 ls=0 lo=8192 hi=8192 width=3: from 8192 want 8190
== noassert (line-start ASSERT dropped)               exit=1
tests/indent_test.c:253: FAIL ASSERT did not fire: enter line_start not after a LF
== crlf     (a CR after the blanks always ends the line) exit=1, 5785 FAILs
tests/indent_test.c:97: FAIL small brace n=3 ls=0 lo=1 hi=1 width=1: from 0 want 1
== fuzzer linked against degrade                      exit=77
indent_fuzz: fuzz/indent_fuzz.c:44: from == model_brace(b, n, ls, lo, hi, width)
```

(The pastlo backtrace is from an earlier run at B = 16384; with B = 8192
the same mutant again exits 139.)

Green, (M)[AC]:

```text
$ build/san/tests/indent_test        # 11.4 s, load1 3.29 (release: 7.9 s)
indent_test: guard page=4096: chunk0 (outside)=SIGSEGV chunk1=ok chunk1-1=SIGSEGV chunk2=ok chunk2+len=SIGSEGV chunk3 (outside)=SIGSEGV
indent_test: 13 precondition violations die by ASSERT
indent_test: small sweep: 2801 texts over 7 byte classes, length <= 4, 2528364 query calls, guarded and straddled
indent_test: bound sweep: 10368 cursor lines around INDENT_BLANK_BYTES=8192, true and unknown line start, guarded and straddled
indent_test: 122586 typing cases v1 answered keep v1's answer
indent_test: all passed
$ build/fuzz/indent_fuzz -max_total_time=60 -max_len=49200 build/indent-fuzz2 fuzz/corpus/indent   # at B = 16384, load1 4.36
Done 1423 runs in 61 second(s)    exit=0
$ build/fuzz/indent_fuzz -max_total_time=60 -max_len=24600 build/indent-fuzz2 fuzz/corpus/indent   # at B = 8192, load1 4.05
Done 1930 runs in 61 second(s)    exit=0
```

Acceptance, (M)[AC] load1 2.54 at start and 3.80 at end, 19:08–19:11 UTC:

```text
make all          exit=0
make check        exit=0   (check_symbols: ok; check_dag: ok; check-layers: 6 isolated layer links passed;
                            check-san: 17 test binaries passed; check-law2: 4 release suites passed; check: ok)
make check-tsan   exit=0   (check-tsan: 6 threaded suites passed)
make fuzz-smoke   exit=0   (fuzz-smoke: 8 fuzzers replayed their seeds)
```

## 10. Bench: before and after

`make build/rel/bench/indent_bench && build/rel/bench/indent_bench`. Rows
are tracked numbers and the bench exits 0. "Before" is the round-1 landing
(9557035), (M)[bat] load1 3.63. "After" is round 2, (M)[AC] load1
3.55–3.43 (other agents' builds share the box).

| Row | before p50 / p99 | after p50 / p99 |
|---|---|---|
| enter viewport_rows=300 flat | 256 / 311 ns | 55 / 57 ns |
| enter viewport_rows=300 span=1 | 13,144 / 42,052 ns | 78 / 79 ns |
| brace viewport_rows=300 flat | 101 / 116 ns | 57 / 59 ns |
| brace viewport_rows=300 span=1 | 13,644 / 48,525 ns | 79 / 82 ns |
| bracket viewport_rows=300 flat | 111 / 5,849 ns | 98 / 4,217 ns |
| bracket viewport_rows=300 span=1 | 2,859 / 66,383 ns | 2,622 / 70,426 ns |
| long_line_limit enter+brace flat (v1 LIMIT rows) | 4,624 / 5,213 ns | — (no LIMIT) |
| long_line_limit enter+brace span=1 | 39,108 / 144,222 ns | — |
| worst_blank brace 2B+1 bytes flat | — | 16,070 / 43,618 ns |
| worst_blank brace 2B+1 bytes span=1 | — | 88,844 / 205,640 ns |
| worst_blank enter B+1 bytes flat | — | 4,904 / 10,430 ns |
| worst_blank enter B+1 bytes span=1 | — | 43,274 / 105,320 ns |
| detect prefix=64KiB flat | 96,186 / 249,123 ns | 86,070 / 204,568 ns |

Ordinary Enter and '}' cost O(indent) now: 55–82 ns on either span shape.
That is the 13 µs window re-walk removed. The worst rows are lines of
8 KiB of blanks, the only lines that come near the bound.

## 11. Obligations on later beads (not done here)

- **F2, `edit` (L2) and the paint path (L4).**
  - The piece adapter: `seek = piece_iter_begin`, `next = piece_iter_next`,
    plus a snapshot twin for detection on a worker.
  - Pass the cursor line's exact start: from the painted row's
    `line_start` when `exact_line`, else from an exact `lineidx` answer,
    else a bounded backward newline search, else `lo`.
  - Insert `style.eol` and then the returned run, by reference or by copy
    (at most B bytes).
  - Validate `width` before calling. That validation belongs to `config`
    on load.
- Round 1's F1 (Enter rejected past column 8192) is resolved by §4.1 and
  §4.2.

## 12. Files

- `src/indent/indent.h`, `src/indent/indent.c`: 127 + 321 lines (M, `wc -l`). Round 1 had 118 + 399. Diff against round 1: `indent.c` −150 +72, `indent.h` −71 +80.
- `tests/indent_test.c` (619), `tests/indent_model.h` (307): `tests/indent_test.c := san`.
- `fuzz/indent_fuzz.c` (117) and `fuzz/corpus/indent/` (12 seeds): `fuzz/indent_fuzz.c := fuzz`.
- `bench/indent_bench.c` (181) and its `Makefile` rule, which links `libtext.a` and `libcore.a`.
