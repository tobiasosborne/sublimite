# v2-0fv.11 · L1 utf8 harvest and the ©/® Extended_Pictographic fix

Source pin `6fec2e8` (`/home/tobias/Projects/sublimite-v1-oracle`). This
bead carries over v1's decoder, cell width, UAX #29 segmentation, budgeted
step APIs, cluster width and artwork projection. It fixes the ©/®
classification that the dossier located
([wave1-L1-harvest.md](wave1-L1-harvest.md) §2.5). Three pieces of state
become explicit automata, two functions with no callers are dropped, and
v1's cluster-width "slow path" moves out of production code into the tests
as an independent reference.

## The bug and the fix

`gcb_of` returned `G_OTHER` for every non-control code point below U+0300.
The comment said "Below U+0300 the classes are fixed". That was false for ©
U+00A9 and ® U+00AE, which are `UCD_EXT_PICT[0]` and `[1]` and the only
Extended_Pictographic entries below U+0300. Ironically, v1's
`t_table_facts` asserted `UCD_EXT_PICT[0].lo >= 0x80`, which allows exactly
this case, yet the shortcut still skipped it.

The fix lives in `gcb_of` (`src/utf8/utf8.c`). The `cp >= 0x7F` branch now
returns `G_PICT` for 0xA9 and 0xAE. This keeps the shape of the hot path,
so Latin text still touches no table. The comment names the two table
entries. `t_table_facts` pins them: `UCD_EXT_PICT[0] == {A9,A9}`,
`[1] == {AE,AE}` and `[2].lo >= 0x300`. If a Unicode version adds a third
ExtPict below U+0300, that assertion fails before GB11 can regress, and so
does `t_class_audit` (below). `tables.h` is untouched. ©/® stay out of
`UCD_WIDE`, so a bare © is width 1, and a test pins both directions.

`GraphemeBreakTest.txt` 15.1.0 contains **zero** scalars U+00A9 or U+00AE.
The conformance run counts and prints this. That is why v1's corpus run
could not catch the bug. Its random oracle pool did not include them
either.

### Blast radius: every consumer of `gcb_of`, audited

| Consumer | Observes ©/®? | After the fix |
|---|---|---|
| `gstate_start` (`gb11_next`) | yes: a cluster starting with ©/® now opens GB11 | `© ZWJ 😀` and `© ZWJ ®` are one cluster (they broke before) |
| `gstate_join` GB11 | yes: `… ExtPict Extend* ZWJ ©` | joins (it broke before) |
| `incb_of` | no: © was `G_OTHER` below U+0900 (InCB NONE); as `G_PICT` it is also NONE | unchanged |
| `safe_start` (`utf8_grapheme_prev`) | no: reads only CR, LF and Control | unchanged |
| `utf8_break_certain` | classifies © as ExtPict instead of Other; both fall into the same final branch (GB999 → 1), and every `y == ©` path returned the same value for Other | **identical results**: a dump over all 619,369 pairs of {every cp < U+0300} ∪ {19 representatives} hashed identically before and after (`build/utf8-red/bc_dump.c`); the soundness test now has ©, ® and VS16 in its alphabet |
| `utf8_emoji_class` → `utf8_cluster_emoji` | yes | © and ® are `UTF8_EMOJI_PICT`; ©/®-initial clusters report PICT |
| `utf8_artwork_step` / `utf8_cluster_artwork` | yes | a ©/® base is pictographic: the projection stops at its first ZWJ (`® ZWJ 😀` → `®`) |
| cluster width (`cseg_scan`, shared by `utf8_cluster` and `utf8_cluster_step`) | yes | `©+VS16` and `©+ZWJ+ExtPict` are 2; bare ©, `©+VS15` and `©+U+0301` stay 1 |

Outside this module (L3/L4, not yet in v2), v1's `layout.c`
(`utf8_cluster_emoji`) and `font/gsub.c` (`utf8_emoji_class`) will now send
©/®-initial clusters, bare © included, down the whole-cluster emoji or
projection path. Narrow ExtPict characters such as ❤ U+2764 and ‼ U+203C
already take that path. This is reported for the font and layout beads to
confirm with a rendering check, not fixed here.

## Restructured state (anti-decay FSM rule)

Each rewrite is checked for equivalence by the suites below. The v2 module
is 611 lines of `utf8.c` against v1's 621 (M, `wc -l`).

- **GB11 context**: v1 kept two booleans, `pict` and `pict_zwj`. They are
  never both 1, because `pict_zwj' = (cur == ZWJ) && pict` and a ZWJ clears
  `pict`. They are now `gb11 ∈ {NONE, PICT, ZWJ}`. The transitions match
  v1's update case by case (`gb11_next`).
- **GB9c context**: the 0/1/2 integer becomes `enum gb9c {NONE, CONSONANT,
  LINKED}` with a `switch` transition (`gb9c_next`). Same table.
- **Cluster width**: v1's `utf8_cseg` held four fields: `first`, `w`, a
  `kind` bit set (`CK_PICT`, `CK_KBASE`, `CK_VS16`, `CK_KEYCAP`,
  `CK_ZWJ_SEQ`, `CK_DONE`, `CK_FIXED2`) and `prev_zwj`. They coordinated
  one lifecycle. It is now one `enum cw`: SEEK, PICT, PICT_ZWJ, KEYCAP, ONE
  and TWO (ONE and TWO are final). `first` is gone because a cluster that
  holds an invalid byte, CR, LF or Control holds nothing else (GB4/GB5;
  CR LF is the only pair), so meeting one while seeking means it is the
  first unit. `utf8_cluster` runs the same automaton over the segmented
  cluster.
- **Artwork**: v1's `{base, pict}` booleans had an unused (0,1) state. They
  become `phase ∈ {SEEK, TEXT, PICT}`.

## Invariants of kept state

| State | Invariant |
|---|---|
| `utf8_gseg.started` | 0 ⇒ every other field is 0 (fresh from `utf8_gseg_init`, reset on every `UTF8_G_END`). 1 ⇒ the cluster has consumed at least one valid scalar. |
| `utf8_gseg.prev` | `enum gcb` of the last scalar joined into the cluster. |
| `utf8_gseg.gb11` | PICT iff the cluster's scalars end in `ExtPict Extend*`. ZWJ iff they end in `ExtPict Extend* ZWJ`. Otherwise NONE. |
| `utf8_gseg.ri_odd` | 1 iff the trailing run of Regional_Indicator scalars has odd length (0 when the last scalar is not RI). |
| `utf8_gseg.incb` | CONSONANT iff the scalars end in `InCB=Consonant (InCB=Extend)*`. LINKED iff they end in `Consonant (Extend\|Linker)*` with at least one Linker. Otherwise NONE. Never non-NONE at cluster start except for a Consonant first scalar. |
| `utf8_cseg.seg` | as `utf8_gseg`, for the same cluster. |
| `utf8_cseg.phase` | SEEK: no scanned scalar has nonzero width (width 0 if the cluster ends). PICT or PICT_ZWJ: the base is a width-1 ExtPict and no VS16 or ZWJ+ExtPict has followed; PICT_ZWJ iff the last scanned scalar is ZWJ. KEYCAP: the base is `[0-9#*]` and no U+20E3 has followed. ONE or TWO: decided, and later scalars are not scanned. The scanned scalars are exactly those `utf8_grapheme_step` consumed into this cluster. |
| `utf8_artwork_state.phase` | SEEK until the first nonzero-width scalar, then TEXT or PICT by that scalar's class, never changing again. |
| `in_tab` / `IN_TREE` inputs | every `UCD_*` table is sorted, disjoint, non-inverted and non-empty (`tests/utf8_tables_test.c`). `tree10` needs at most 1024 ranges (`_Static_assert`, (P) 2^10). |

There are no globals and no allocation. `nm -u build/rel/src/utf8/utf8.o`
shows `memmove` and `__stack_chk_fail`, both granted `*` in
`tools/system-symbols.txt`. No `ASSERT` is needed: every condition in the
module can come from input and is a return value. Preconditions that the
header states (`n >= 1` for `utf8_decode`, `off` a unit boundary) belong to
the caller, as in v1.

## Not carried over from v1

- `utf8_grapheme_prev_step` and `utf8_gprev`: no caller in v1 production code at the pin (`grep` over `src/`). Only tests, fuzz and bench used them. Reopen when a backward UI walk needs the budget.
- `utf8_ascii_run`: no caller in v1 production code at the pin. Only tests, fuzz and `bench/view_bench.c` used it. With it goes the `__BYTE_ORDER__` branch.
- `cluster_width_slow`: production carried two cluster-width implementations, and the "slow" one shared `gcb_of` with the fast one, so it could not catch this bug. The independent reference is now `ref_cluster_width` in the test and the fuzzer, written from the rule in `utf8.h` over oracle classes.
- The `pict`/`pict_zwj`, `CK_*`, `first`/`w`/`prev_zwj` and `{base, pict}` fields: replaced by the enums above.
- v1 provenance tags in comments (`P1.1b`, `edit-r8e`, `edit-v16`, `edit-ptn`, `PRD 6.2`): v1 bead ids with no meaning in v2. Measured claims cite v1's decision docs instead.
- Header guard `EDITOR_UTF8_H`: renamed `UTF8_UTF8_H` to match the module path, as `SCAN_SCAN_H` did. `tables.h` keeps `EDITOR_UTF8_TABLES_H` because it is generated output and must stay byte-identical.
- Test: the 8-slot heap `place()` ring and `t_place_alias`. Inputs now sit flush against PROT_NONE guard pages in 4 named slots, so the release lane catches overreads too.
- Test: `t_prev_step` and `t_ascii_run`, which go with their functions.
- Fuzzer: the `utf8_grapheme_prev_step` and `utf8_ascii_run` sections, and the silent `return 0` on allocation failure, which now traps (fail closed).
- Bench: `harness.h`, the (G) exit gates (`decode_width` 5/10 µs, `ascii_skip_96k` ≥ 8 GB/s, step rows p99 ≤ 0.5 ms as exit status), the `ascii_skip_96k` row and the confidence-interval printer. Rows are tracked numbers and exit 0, as in v2-0fv.10.

## Tests

`tests/utf8_test.c` (SAN lane: clang ASan+UBSan, plus the gcc -O2 release
binary built by `make all`) covers:

- **Guard slots.** Four `[PROT_NONE][64 KiB RW][PROT_NONE]` mappings, with
  slot size (P) = the largest placed input (`t_random`'s 1 << 16 bytes).
  `put()` places flush right and `put_begin()` flush left. A forked positive
  control proves each guard faults before any case runs. Backward APIs
  (`utf8_prev`, `utf8_grapheme_prev`) run against both guards.
- **Red tests (dossier §2.5).**
  - `t_pict_audit` checks every one of the 3537 `UCD_EXT_PICT` code points,
    then `utf8_emoji_class` for every code point against the tables in
    both directions.
  - `t_gb11_copyright` runs ten GB11 cases with ©/® on either side of the
    ZWJ, plus negative cases, one-shot, backward and chunked at every split
    × 6 budgets.
  - `t_copyright_width` checks © and ® alone (1), with VS16 (2), with VS15
    (1), in ZWJ sequences (2) and with a mark (1), through `utf8_cluster`
    and through `utf8_cluster_step` at every chunk size × 4 budgets, plus
    the artwork projection.
- **Every code point.**
  - `t_vs16_every_cp` takes each scalar C, segments `C U+FE0F` and checks
    length and width against `ref_cluster_width`. It runs through
    `utf8_cluster` and through `utf8_cluster_step` fed C first and VS16
    after a refill.
  - `t_class_audit` covers every scalar. 18 probe sequences around C are
    segmented and compared with the stateless oracle. The test first proves
    the probes separate 424 pairs: every pair of the 15 GCB classes under
    each of 4 InCB values, plus the InCB values the rules read for Other
    and Extend. It then keeps a greedy subset of 10 probes that still
    separates them all and runs those for every code point. This is the
    slow reference for `gcb_of`'s below-U+0300 fast path and for
    `incb_of`'s `cp >= 0x900` shortcut.
  - `t_width` (every code point, inline and table path) and `t_all_scalars`
    (encode, decode, truncation and prev against both guards) are carried
    from v1.
- **Carried from v1, extended.**
  - GraphemeBreakTest: 1187 cases, one-shot and chunked.
  - Oracle random soup: © and ® added to the pool, and cluster width is now
    checked against `ref_cluster_width`, not just the 0..2 bound.
  - Cluster-step equivalence: © and ® added to the samples and the
    alphabet, plus a 25 KB `©(ZWJ®)*` chain.
  - `utf8_break_certain` soundness: ©, ® and VS16 added to the alphabet.
  - Budget tests: exactly 12 calls for the 49,153-byte cluster. Derivation
    (P): the first call takes 4097 B, each later call 4096 B, and
    4097 + 11 × 4096 = 49153.
  - Random buffers.

The suite proves it can fail with three mutants, all in `build/utf8-red/`:

- `p[1]` is read when `n == 1` in `utf8_cluster`'s ASCII path. The
  release binary takes SIGSEGV at the right guard, `utf8_cluster
  (p=0x…9fff "a", n=1)`, exit 139.
- `incb_of`'s `cp >= 0x900` becomes `0x920`. `t_class_audit` and the GB9c
  cases fail.
- The pre-fix `gcb_of`, below.

`tests/utf8_tables_test.c` (SAN lane) checks that every table is sorted,
disjoint, non-inverted, non-empty and ≤ U+10FFFF, that
`UNICODE_VERSION == "15.1.0"`, and spot facts including ©/® ∈ EXT_PICT and
© ∉ WIDE. It reads no UCD data at run time, so it belongs to the unit lane
and not to a generator `--check` lane.

`fuzz/utf8_fuzz.c` (FUZZ lane) is v1's fuzzer with the dropped functions
removed. The oracle now also requires `utf8_cluster`'s width to equal
`ref_cluster_width`. Grapheme step and cluster step share one chunked
driver. Seven seeds live in `fuzz/corpus/utf8/`, including `copyright-zwj`
and `copyright-vs`. Linked against the pre-fix `gcb_of`, the fuzzer traps
on its seeds (`libFuzzer: deadly signal`; the crash input is the
`copyright-vs` seed).

Lanes: no TSan, because the module has no threads and no shared state. No
Law-2 suite, because the module calls no allocator (`nm -u` above).

Run time (M)[AC] load1 1.4–1.8: `build/san/tests/utf8_test` takes 15.3 s
and `build/rel/tests/utf8_test` 5.4 s. `t_class_audit` was 17.6 s under
ASan with all 18 probes and no memoised oracle. The verified greedy subset
and a precomputed oracle table brought it down without dropping a code
point.

## Red and green

Red: the final test linked against `utf8.c` with the pre-fix `gcb_of`.

```text
$ gcc <WARN> -O2 -g -msse2 -Isrc -c build/utf8-red/utf8_prefix.c -o build/utf8-red/utf8_prefix.o
$ gcc -O2 -g -msse2 -pthread build/rel/tests/utf8_test.o build/utf8-red/utf8_prefix.o build/rel/libcore.a -lm -ldl -o build/utf8-red/utf8_test_prefix
$ build/utf8-red/utf8_test_prefix; echo exit=$?      # repeated lines elided
pict audit: U+00A9 in UCD_EXT_PICT[0] has emoji class 0
pict audit: U+00AE in UCD_EXT_PICT[1] has emoji class 0
FAIL tests/utf8_test.c:757 bad == 0
pict audit: 3537 UCD_EXT_PICT code points, 4 misclassified
gb11 case 0: first cluster 5 bytes, want 9          (© ZWJ 😀; cases 1-6 likewise)
FAIL tests/utf8_test.c:782 g == cases[i].first
copyright width case 2: 1 want 2                    (© VS16; ® VS16, © ZWJ 😀, © ZWJ ® likewise)
FAIL tests/utf8_test.c:810 utf8_cluster_emoji(...) == UTF8_EMOJI_PICT
FAIL tests/utf8_test.c:821 utf8_cluster_artwork(p, 9, out) == 2 && ...
VS16 U+00A9: cluster 5/1 step 5/1 want 5/2
VS16 U+00AE: cluster 5/1 step 5/1 want 5/2
class audit U+00A9 (oracle class 14 incb 0): probe 6 breaks 0xa, oracle 0x2
class audit U+00AE (oracle class 14 incb 0): probe 6 breaks 0xa, oracle 0x2
GraphemeBreakTest: 1187 cases, 0 mismatches, 0 chunked mismatches; 0 scalars are U+00A9/U+00AE (the corpus does not exercise them)
FAIL tests/utf8_test.c:1131 g == pos[j] - off       (oracle soup)
FAIL tests/utf8_test.c:1135 w == ref_cluster_width(...)
utf8_test: 459 FAILURES
exit=1
```

Every-code-point audits list only U+00A9 and U+00AE. Chunked equivalence
holds in red because both paths agree on the wrong answer. That is why the
independent oracle is the red test.

Green:

```text
$ build/san/tests/utf8_test
utf8_test: slot 0 [0x7ff781950000 PROT_NONE][0x7ff781951000 RW 65536][0x7ff781961000 PROT_NONE]; read begin-1=SIGSEGV begin=ok end-1=ok end=SIGSEGV
...
pict audit: 3537 UCD_EXT_PICT code points, 0 misclassified
class audit: every code point, 10 of 18 probes separate all 424 class and InCB pairs
GraphemeBreakTest: 1187 cases, 0 mismatches, 0 chunked mismatches; 0 scalars are U+00A9/U+00AE (the corpus does not exercise them)
utf8_test: all passed
$ build/san/tests/utf8_tables_test
utf8_tables_test: ok (Unicode 15.1.0, 16 tables, table bytes 16720)
$ make all          # exit 0
$ make check        # check-san: 14 test binaries passed; check-law2: 3 release suites passed; check: ok
$ make check-tsan   # check-tsan: 5 threaded suites passed
$ make fuzz-smoke   # == build/fuzz/utf8_fuzz fuzz/corpus/utf8 ... Done 7 runs; fuzz-smoke: 6 fuzzers replayed their seeds
```

## tables.h: byte-identical, and the regeneration obligation

`src/utf8/tables.h` is a byte copy of the pin (`cmp` clean). It is 2149
lines with sha256
`e0476d3f09e1bb6251a687d16f618afc9fe98550986b60fa41934aae8c1e6829`, the
same as v1 at `6fec2e8`. The fix did not touch it. `tools/ucdgen.py` is not
in v2 yet; the UCD inputs are, under `vendor/ucd/` with `SHA256SUMS`. The
obligation: when the generator lands, `tools/ucdgen.py --check` (or an
equivalent regeneration diff in `check-lints`) must reproduce this file
byte-identically from `vendor/ucd/`. A Unicode version bump is its own
bead. It reruns `utf8_test` in full, including `t_table_facts`'s
below-U+0300 ExtPict assertion and GraphemeBreakTest at the new version
(the case count 1187 and `UNICODE_VERSION` change with it).

## Bench

`make build/rel/bench/utf8_bench && build/rel/bench/utf8_bench` (other
agents' builds may share the box). The rows are tracked numbers, and every
timing outcome exits 0.

```text
(M)[AC] BAT0=Full load1=1.58 at start
decode_width_500_runs   p50= 3767 ns p99=  7539 ns  (7.53 ns/scalar)
grapheme_500_runs       p50=23960 ns p99= 37147 ns
cluster_500_runs        p50=26211 ns p99= 49026 ns
decode_width_500_iid    p50= 9345 ns p99= 12621 ns
step_combining_49k      p50=48562 ns p99= 68495 ns  (11.85 ns/byte)
step_prepend_40k        p50=61196 ns p99= 84628 ns  (14.94 ns/byte)
cstep_prepend_40k       p50=116528 ns p99=126425 ns (28.45 ns/byte)
step_zwj_35k            p50=35584 ns p99= 50824 ns
step_copyright_zwj_25k  p50=26957 ns p99= 32288 ns
(M)[AC] BAT0=Full load1=1.75 at end
```

The `UTF8_GRAPHEME_BUDGET` derivation (`utf8.h`) uses the slowest chain.
That is `cstep_prepend_40k`: U+0600 is zero-width, so the width automaton
stays in SEEK and scans every unit. It costs t = 28.7 ns/byte (M, `_total`
row), so one 4096-byte call is about 117 µs (p99 126 µs), 4.0× inside the
0.5 ms UI slice (G, v1 `perf/01-perf-target.md` G1 row). For grapheme
steps alone the slowest is `step_prepend` at 15 ns/byte, 61 µs per call.

The cstep path decodes and classifies each unit twice, once in
`utf8_grapheme_step` and once in `cseg_scan`. That doubles the cost on
zero-width chains. It is the same in v1. Law 1 says a perf bead opens only
from observed lag, so this is noted, not changed.

A/B: the same bench source was linked against v1's `utf8.c` and headers
(`build/utf8-ab/`, run v1, v2, v1, v2, (M)[AC] load1 1.46–1.95). Ratios
are v2/v1 of the summed p50s. Segmentation is within run-to-run spread:
grapheme 1.05–1.08, step rows 0.88–0.97, `decode_width_runs` 1.19 (one run
pair is 3080 vs 4300 ns; the decode/width code is unchanged, so this is
noise). Cluster rows are faster (`cluster_500` 0.81–0.82, cstep 0.85–0.98),
and `utf8_cluster_width` on long clusters runs at 0.46–0.53. The reason is
that `utf8_cluster` now stops scanning once the width is decided, where
v1's `cluster_width_slow` rescanned the whole cluster. Both v1 runs exit 1
with "a timed call returned a wrong answer" on the `copyright_zwj_25k`
chain: v1 segments it into single © and ® clusters, which is the bug.

## Files

- `src/utf8/utf8.h`, `src/utf8/utf8.c`, `src/utf8/tables.h`: 249 + 611 + 2149 lines (M, `wc -l src/utf8/*`).
- `tests/utf8_test.c` (1332 lines) and `tests/utf8_tables_test.c` (87). Both are registered in `SAN_SUITES` (`tests/lanes.mk`).
- `fuzz/utf8_fuzz.c` (328) and `fuzz/corpus/utf8/` (7 seeds), registered in `FUZZ_SUITES`.
- `bench/utf8_bench.c` (261) and its `Makefile` rule, which links `libtext.a` and `libcore.a`.
