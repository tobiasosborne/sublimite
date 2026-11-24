# Wave 1 — L1 (`libtext.a`) harvest dossier

> Lane M4 stretch dossier, written after the L0 dossier
> ([wave1-L0-harvest.md](/home/tobias/Projects/sublimite-v2/docs/decisions/wave1-L0-harvest.md))
> per the lane brief. Same rules: v1 pinned read-only at
> `/home/tobias/Projects/sublimite-v1-oracle` (detached at `6fec2e8`);
> citations are FILE + SYMBOL there; `docs/PLAN_V2.md` (PLAN) line numbers,
> written against v1 `c95b6eb`, are re-resolved to symbols at the pin
> (§0.2); line counts measured with `wc -l` (M) at the pin, commands
> stated; no invented numbers, no time estimates (Law 4); no `bd`, no
> build, no v1 run in this lane.
>
> **Sequencing warning.** PLAN §6 puts L1 inside Wave 1, but every L1
> bead depends on L0 beads from the L0 dossier (§9 maps them: L0-1 `os`,
> L0-2 fault lease, L0-3 `base`, L0-5 `work`). Two L1 modules
> (`lineidx`, `find`) additionally cannot even *compile* in v2 until
> their upward `file/file.h` dependency is replaced by the L0 pinned
> fault lease surfaced through `piece_snapshot` (PLAN §4.1, §0.1 below).
> Read the L0 dossier first; this one assumes its §0 conventions
> (shared acceptance block, `EDIT_APP_ID`, globals list) without
> repeating them.

## 0. Cross-cutting facts

### 0.1 L1 partial order: conforming inside L1, two upward edges into L2

v2 `tools/check_dag.py` `INTRA_RULES` for L1:
`"scan,utf8 < piece < undo,lineidx,indent,find"`; inter-layer rule: L1
may include L0 + L1 only. Auditing every quoted include in v1
`src/{scan,utf8,indent,piece,undo,lineidx,find}` at `6fec2e8`:

| Edge | v1 site (FILE + SYMBOL) | v2 verdict |
|---|---|---|
| `piece → scan` | `src/piece/piece.c` include; `scan_count` used for chunk newline counting (`nl_count` path) | OK |
| `piece → base, os` (L0) | `src/piece/piece.c` includes; `os_mutex` (pool/view locks, `core`), base macros | OK |
| `indent → piece` | `src/indent/indent.h` (`piece_tree`/`piece_snapshot` in every signature) | OK (`piece < indent`) |
| `undo → piece, base` | `src/undo/undo.h` (`piece_tree*`, `edit_pool` in `undo_log`); `undo.c` → `os/os.h` | OK |
| `lineidx → piece, scan, work, os` | `src/lineidx/lineidx.h` (`work_pool`, `piece_snapshot`), `lineidx.c` (`scan_count`, `os_slice_ns` family) | OK |
| `find → piece, work, base` | `src/find/find.h` (`piece_snapshot`, `work_ctx`), `literal.c` (`edit_cpu_has_avx2`) | OK |
| **`lineidx → file` (L2)** | **`src/lineidx/lineidx.c`: `#include "file/file.h"`** — `file_backing_faulted` (job abort check in the build worker, adoption guard, `lineidx_any_nonascii`, chunk query), `file_snapshot_backing` + `file_backing_acquire`/`file_backing_release` (in `lineidx_bind_snapshot` path) | **VIOLATES layering** (L1 → L2 upward). PLAN §4.1 exists to kill exactly this edge |
| **`find → file` (L2)** | **`src/find/find_int.h`: `#include "file/file.h"`** — single use: `file_snapshot_faulted(m->snapshot)` inside `poll_stop` (the metered cancellation poll) | **VIOLATES layering**, same root cause, smaller surface |

Both violations ask one question — "has the mapped backing under this
`piece_snapshot` faulted since I last looked?" — plus, for `lineidx`,
retain/release of that backing. v1 answers it in L2 `file` because
v1's fault registration lived there historically (`file_bus`, since
moved to L0 `os_fault_*`, L0 dossier §1.5(b)). PLAN §4.1's design is
the v2 answer and the L1 beads must implement their half of it:

- L0-2 provides `(slot, gen)` + pin/unpin/poll (L0 dossier §1.5(b)).
- The `piece` bead (§4) surfaces it: a snapshot taken over a fault-
  registered mapped original carries the pin; `piece_snapshot`
  exposes a fault poll (name/shape from the L0-2 + piece decision
  notes) so L1 consumers never name `file_*`. Today the only
  snapshot→backing channel is `piece_snapshot_mapping` returning
  `piece_map_hooks` (`acquire`/`release` only, no fault query) —
  `lineidx_bind_snapshot`'s header comment even says it "Retains
  only its lifetime token" while the code reaches for
  `file_snapshot_backing(snapshot)`, i.e. it downcasts through L2
  knowledge of what `hooks->ctx` is. That downcast is the thing
  being deleted.
- `lineidx` then binds/polls through `piece` only; `find`'s
  `poll_stop` asks the snapshot. L2 `file` later *registers*
  mappings with L0 and hands `piece` the token at
  `piece_init_mapped` time — direction restored (L2 → L1 → L0).

Until L0-2 + the piece hook land, `lineidx.c` and `find_int.h`
cannot be harvested without either (a) carrying the violation
(checker fails — not an option, fail-closed), or (b) stubbing the
fault query to "never faulted" (silently wrong on truncated mapped
files — data-integrity failure mode, not an option). Hence the
hard bead ordering in §9. This is L1's headline structural fact.

Softer notes: `scan` and `utf8` include **no** module headers at
all (pure span functions) — they can land before everything except
the build skeleton. `utf8/tables.h` is a generated second header
inside its own module (fine; generator `tools/ucdgen.py` + UCD data
are `vendor`/tools concerns, §2). `piece` ships a *test-observability*
header inside the module, `src/piece/piece_test.h`, included by
`piece.c` itself — §4.4 deals with it. `find` has three internal
headers (`find_int.h`, `literal.h`, `visit.h`) plus the semi-public
`options.h` — §7.

### 0.2 PLAN line citations re-resolved (L1-relevant)

| PLAN citation | Symbol at `6fec2e8` | Verdict |
|---|---|---|
| `piece.h:34` (§4.1, "1,920 B/edit COW bound") | MEMORY BOUND comment in `src/piece/piece.h`: "(+ path copies for live snapshots: 1920 B per edit made while one is live)" — still at that line | Yes; it is the (P)-tagged input to PLAN's snapshot prepayment derivation (§4) |
| `piece.c:441` (§4.1, private boundary-chunk prepayment) | `checkpoint_snapshot_storage` in `src/piece/piece.c` — comment: "Prepay a writable saved boundary before publishing the first snapshot that can observe transaction bytes in it"; uses `piece_checkpoint.boundary_private`, `cp->saved.add_len` mark, `cp->restore->tbl` | Yes (function sits a few lines below the cited line at the pin; symbol is unambiguous) |
| `lineidx.h:61` (§4.1, fault safety contract) | `lineidx_bind_snapshot` comment in `src/lineidx/lineidx.h` ("A fault makes all answers inexact and cancels pending adoption; reload uses a new index") | Yes — this contract is what the L0 pin must preserve without the L2 include (§6) |
| `lineidx.h:37` (§4.3, 512 × 64 KiB = 32 MiB (P) cap) | `LINEIDX_CHUNK` (65536u) + `LINEIDX_EDIT_MAX_CHUNKS` (512u) defines in `src/lineidx/lineidx.h` | Yes; arithmetic is PLAN's own (P) statement, constants verified in header |
| `lineidx.h:57` (§4.3, `{value, exact=false}` estimate contract) | `lineidx_result` typedef + query comments in `src/lineidx/lineidx.h` ("Exact when… else an estimate by byte proportion") | Yes |
| `lineidx.c:858` (§4.3, reserved-capacity refusal) | Admission check inside `lineidx_edit`'s planning helper in `src/lineidx/lineidx.c`: `if (p->pieces > x->cap - p->kept \|\| p->pieces > LINEIDX_EDIT_MAX_CHUNKS) return -1;` — still at that line | Yes; PLAN §4.3 replaces this refusal-to-recreate with degraded mode + off-path regrowth (§6) |
| `editor_large_test.c:1119` (§4.3) / `editor_large_test:1119` (§6) | Occupied-bulk foreground-navigation case; L5-scale integration test | Out of L1 lane; re-created with L5, cited as behaviour the lineidx bead's continuations must enable (§6) |

### 0.3 Globals, Windows, `#include "*.c"` — L1 sweep

- **Globals:** none mutable at file scope in any L1 production file
  (grep over `src/{scan,utf8,indent,piece,undo,lineidx,find}/*.c`:
  all state in caller/core-owned structs — `piece`'s `core`,
  `undo_log`, `lineidx`, find's caller-storage programs). `utf8`'s
  `src/utf8/tables.h` holds `static const` range tables +
  `UNICODE_VERSION` (const data, not mutable globals; header-static
  per-TU copies are the v1 shape — keep, linkers fold them).
  Conforming with CLAUDE.md closed-globals rule; nothing to name.
- **Windows:** no `*_win.c` anywhere in L1. One branch total:
  `last_byte` in `src/lineidx/lineidx.c` — `#if defined(_WIN32)`
  C11 reverse search vs `#define last_byte memrchr`. Harvest keeps
  `memrchr` (GNU/POSIX), branch deleted; if the worker prefers
  keeping the C11 loop unconditionally for seam purity, record it —
  either compiles; do not keep the `#if`.
- **`#include "*.c"`:** exactly three in L1's test/bench estate —
  `tests/lineidx_os_test.c` includes `../src/lineidx/lineidx.c`,
  `tests/find_p1r_test.c` includes `../src/find/literal.c` **and**
  `../src/find/find.c`, `bench/undo_bench.c` includes
  `../src/undo/undo.c`. Each needs the §-per-module rewrite
  (public headers + link `libtext.a`, `--wrap` for injection).
  Everything else is clean.
- **Module-internal test hooks in production code:** `piece.c`
  compiles `piece_test_*` observability under `PIECE_TESTING`
  (auto-enabled in ASan builds, `src/piece/piece_test.h`). v2's
  "no `#include *.c`" rule makes this *more* relevant, not less:
  tests that need height/pool stats (`piece_test_get_stats`,
  `piece_test_get_memory`, `piece_test_get_state`) get them from
  this sanctioned channel instead of including the `.c`. Keep the
  mechanism; ensure the release build exports none of it (the
  header already gates it — acceptance: `nm` the release
  `libtext.a` for `piece_test_` symbols == none).

## 1. `scan` — `src/scan/`

PLAN disposition (§3): **harvest verbatim**. Smallest module in L1;
land it first, it unblocks `piece`'s and `lineidx`'s counting.

### 1.1 Files / counts (`wc -l src/scan/*`, M)

| File | Lines | Disposition |
|---|---|---|
| `src/scan/scan.h` | 12 | Verbatim |
| `src/scan/scan.c` | 87 | Verbatim (SSE2) |
| `src/scan/STATUS.md` | 35 | Read only (P1.2/P1.2c record; guarded-page test history) |

### 1.2 API (`src/scan/scan.h`, guard `SCAN_H` — note: not `EDIT_`-prefixed, harmless)

Three functions, one struct, no state, no allocation:
`scan_counts { uint64_t newlines, nonascii }`,
`scan_count(p, n)` (count `'\n'` and bytes ≥ 0x80),
`scan_find_byte(p, n, c)` (first occurrence or NULL),
`scan_find_nth_newline(p, n, k)` (0-based k-th `'\n'` or NULL).
Implementation (`src/scan/scan.c`): SSE2 via `<emmintrin.h>`
*unconditionally* (x86-64 baseline: SSE2 is architectural — no CPU
dispatch, no `edit_cpu_has_*` call; the signed-compare trick
`_mm_cmplt_epi8(x, zero)` for ≥0x80 is documented inline; the
255-block flush bound in `scan_count`/`scan_find_nth_newline` is a
lane-overflow (P) bound stated in the comment — keep comments).
Scalar tails handle <16-byte remainders. No overread by
construction (STATUS.md records protected-page differential
testing for lengths 0..8192 — the test approach, §1.4, is the
valuable part).

### 1.3 Dependencies / Windows / globals

Includes: `scan.c` → `"scan/scan.h"` + `<emmintrin.h>` only.
Zero module edges — conforms trivially (`scan < piece`). No
Windows parts, no globals. Portability note for the bead, not a
fix: unconditional SSE2 intrinsics assume the x86-64 baseline
CLAUDE.md's gcc-13 release target already assumes; a future
non-x86 port writes a scalar `scan.c` against the same 12-line
header. Do not add dispatch now (no repro; `base`'s AVX2 query
exists if a measured need appears, Law 1).

### 1.4 Tests / fuzz / bench (`wc -l`, M — command in §2.4 preamble)

- `tests/scan_test.c` — 161. Differential vs scalar reference +
  the align/protected-page cases STATUS.md describes. No
  `#include "*.c"`. Verbatim.
- `fuzz/scan_fuzz.c` — 108. Scalar differential with guarded
  ranges (STATUS.md). Verbatim; Law 1 satisfied (scan parses
  arbitrary bytes in the differential sense and its overread
  surface is the risk — fuzzer + mprotect fixtures are the point).
- `bench/scan_bench.c` — 183. **Caution:** STATUS.md says its
  rows carry throughput *gates* affecting exit status in v1
  (">=12 GB/s p50/p99 primitive gates…" with those numbers as
  recorded there — (M)/(G) on v1's box). CLAUDE.md Law 1 demotes
  benches to tracked numbers that exit 0 ("a miss is a note in
  the bead, never a blocker"), G1 being the sole enforced guard.
  The harvest must reconcile: keep the measurement + oracle
  validation, convert gate-failures to printed notes + exit 0,
  record the change in the decision note. Silently carrying an
  exit-nonzero bench would break `make check` philosophy;
  silently deleting the thresholds without note would lose the
  baseline. Numbers themselves are not re-stated as v2 gates here.

### 1.5 Targeted fixes

None. PLAN names no scan fix; STATUS.md's open items are coverage
history, already closed in v1 (P1.2c). Verbatim means verbatim.

## 2. `utf8` — `src/utf8/`

PLAN disposition (§3): **harvest + targeted fix** — "fix ©/®
`UCD_EXT_PICT`". The bug is fully located below (§2.5); it is a
one-function classification shortcut, tables are correct.

### 2.1 Files / counts (`wc -l src/utf8/*`, M)

| File | Lines | Disposition |
|---|---|---|
| `src/utf8/utf8.h` | 291 | Harvest (public API incl. header-inline `utf8_decode`, `utf8_cell_width`, `utf8_ascii_run`) |
| `src/utf8/utf8.c` | 621 | Harvest + §2.5 fix in `gcb_of` |
| `src/utf8/tables.h` | 2149 | Harvest **as generated output** — "Generated by tools/ucdgen.py from Unicode 15.1.0. Do not edit." |

No `STATUS.md`. Generator: `tools/ucdgen.py` (v1 tools/), inputs
are UCD 15.1.0 data files (`UnicodeData.txt`,
`EastAsianWidth.txt`, `emoji/emoji-data.txt`,
`DerivedCoreProperties.txt`, `auxiliary/GraphemeBreakProperty.txt`)
— in v2 those inputs + the generator land under the Wave 0 vendor/
tools work (`PLAN §5`: generator `--check` for UCD in structural
lints). The utf8 bead's contract with that: `tables.h` at the pin
is the golden output; when v2's `ucdgen.py` lands,
`tools/ucdgen.py --check` (or equivalent regeneration diff) must
reproduce it byte-identically from the vendored UCD, and any future
Unicode version bump is its own bead with full `utf8_test` +
GraphemeBreakTest revalidation. **Do not hand-edit `tables.h` to
fix §2.5** — the tables already contain the right facts.

### 2.2 Public API (`src/utf8/utf8.h`, guard `EDITOR_UTF8_H`)

Includes `<stddef.h>`, `<stdint.h>`, `<string.h>` only. Header
contract (keep verbatim): bytes are never rejected — ill-formed
input decodes as length-1 invalid units carrying the byte value,
so every buffer segments and round-trips byte-exactly; no
allocation, no globals; offsets `size_t` (single contiguous span
per call — piece/lineidx supply the spans).

- Decode/encode: `utf8_step {cp, len, valid}`,
  `utf8_decode` (**header-inline deliberately**, with the measured
  rationale in its comment — packed struct return serialised
  loops out-of-line; do not outline it in the harvest),
  `utf8_prev`, `utf8_encode` (surrogates/>U+10FFFF → U+FFFD).
- Width: `utf8_cell_width` (inline fast path `cp < 0x300` →
  `cp != 0xAD`, else `utf8_cell_width_table` out-of-line). Rules
  in the comment are the contract (0 for Mn/Me/Mc/Cf/Default_
  Ignorable/VS/skin-tone, 2 for EAW W/F + Emoji_Presentation,
  zero wins ties; unassigned follow tables — the U+2FFFD/U+E01F0
  examples stay).
- Graphemes (UAX #29, Unicode 15.1, GB3..GB13 incl. GB9c):
  `utf8_grapheme_next`, `utf8_grapheme_prev`, and the resumable
  budgeted step APIs `utf8_gseg` + `utf8_grapheme_step`
  (`UTF8_G_END/BUDGET/MORE`, `UTF8_GRAPHEME_BUDGET` with its (M)
  per-byte cost note) and `utf8_gprev` +
  `utf8_grapheme_prev_step`, plus `utf8_break_certain`
  (conservative certain-break predicate for forward segmentation
  restarts). Header states: passes all cases of
  `GraphemeBreakTest.txt` (count as stated there, checked by
  `tests/utf8_test.c` — the fixture is part of the harvest).
- Clusters/width/artwork: `utf8_cluster`, `utf8_cluster_width`
  (the cluster-width rule layout consumes: Hangul→2, width-1
  ExtPict + VS16 or ZWJ+ExtPict → 2, keycap `[0-9#*]`+U+20E3 →
  2, VS15 doesn't narrow), `utf8_cluster_artwork` +
  `utf8_artwork_step` (unshaped-artwork projection for font),
  `utf8_emoji_class` (`UTF8_EMOJI_PICT`/`UTF8_EMOJI_RI`) +
  `utf8_cluster_emoji`, budgeted `utf8_cseg` +
  `utf8_cluster_step`, and inline `utf8_ascii_run` (SWAR ASCII
  prefix, little-endian `ctz` path with portable fallback —
  keep both branches).

Implementation notes (`src/utf8/utf8.c`): range lookup via `IN`
(binary search) / `IN_TREE` macros over `UCD_*` tables;
`utf8_cell_width_table` (ZERO_WIDTH wins over WIDE, skin-tone
range hard-coded with `edit-r8e` comment); `gcb_of` (GCB classifier
— §2.5); `gstate_start`/`gstate_join` (GB rule chain, each rule
comment-tagged GB3…GB999 — keep tags, they are the audit trail);
`incb_of` (GB9c states); `cseg_*` cluster-step internals. Private
by file-static; no exported symbol beyond the header list.

### 2.3 Dependencies / Windows / globals

`utf8.c` → `"utf8/utf8.h"`, `"utf8/tables.h"` (same module). No
module edges — conforms (`utf8 < piece`, in fact utf8 is consumed
by layout/font above, not by piece: piece is byte-oriented). No
Windows code (the `__BYTE_ORDER__` branch in `utf8_ascii_run` is
endianness, not platform). No mutable globals (§0.3).

### 2.4 Tests / fuzz / bench

All L1 test/bench/fuzz counts in this dossier were measured at
the pin in two `wc -l` invocations (module mains, then the
secondary suites) (M).

- `tests/utf8_test.c` — 1063. Includes the GraphemeBreakTest
  corpus run, every-code-point width checks ("checks that and
  every code point", `utf8.h`), table-facts (`t_table_facts`
  referenced by `gcb_of`'s comment), chunking/budget equivalence
  for the step APIs. No `#include "*.c"`.
- `tests/utf8_tables_test.c` — 70. Generator/table consistency
  companion; classify its UCD-data needs with the vendor bead
  (if it reads raw UCD at test time, it belongs to the
  generator `--check` lane, not the unit lane — worker decides
  and records).
- `fuzz/utf8_fuzz.c` — 326 (largest L1 fuzzer). Chunked-vs-oneshot
  segmentation equivalence + arbitrary-bytes safety. Verbatim.
- `bench/utf8_bench.c` — 270. Tracked numbers (Law 1); the
  grapheme/cluster slices' (M) notes in `utf8.h` derive from this
  class of measurement — re-record on the v2 box (power-stamped),
  don't gate.

### 2.5 Targeted fix: ©/® and `UCD_EXT_PICT`, by symbol — bug located

**Facts.** `UCD_EXT_PICT` in `src/utf8/tables.h` begins with
`{0x00A9, 0x00A9}` (©) and `{0x00AE, 0x00AE}` (®), then
`{0x203C, …}` — i.e. ©/® are the *only* Extended_Pictographic
code points below U+0300 in Unicode 15.1's data as generated.
The generator is innocent: `tools/ucdgen.py` builds `EXT_PICT`
from `emoji-data.txt` `Extended_Pictographic` ranges and `WIDE`
from EastAsianWidth W/F + `Emoji_Presentation` (©/® correctly
*absent* from `UCD_WIDE` — they are not default-emoji-presentation,
so scalar width 1 is correct).

**Bug.** `gcb_of` in `src/utf8/utf8.c` short-circuits
`cp < 0x300`: returns `G_CONTROL` for Cc/U+00AD range members and
`G_OTHER` otherwise, with the comment "Below U+0300 the classes
are fixed (tests/utf8_test.c t_table_facts checks it against the
tables)". That fixed-class claim is false for exactly © and ®:
they never reach the `IN(UCD_EXT_PICT, cp)` → `G_PICT` check below
the shortcut. Downstream damage, each traceable:

1. `gstate_join` GB11 (`st->pict_zwj && cur == G_PICT`): a cluster
   `… ExtPict Extend* ZWJ ©` breaks before © (wrong), and a
   cluster *starting* with © never sets `st->pict` in
   `gstate_start`, so `© ZWJ <ExtPict>` also breaks (wrong — GB11
   says no break). Same for ®.
2. `utf8_emoji_class` is defined as `gcb_of(cp) == G_PICT ?
   UTF8_EMOJI_PICT : …` — returns 0 for ©/®, so
   `utf8_cluster_emoji` misses ©/®-initial clusters and the font
   never gets its whole-cluster GSUB attempt for e.g. ©+VS16.
3. Cluster width's ExtPict-sensitive rule (width-1 base + VS16 →
   2, `utf8.h` cluster rule; implemented via the cseg/emoji
   classification) cannot fire for ©+U+FE0F / ®+U+FE0F if it
   keys off the same classification — worker traces
   `utf8_cluster`/`cseg_result` in the bead and confirms which
   of GB11/width/artwork paths observe the misclassification
   (all three consume `gcb_of` or `utf8_emoji_class`; the report
   enumerates the actual consumers rather than assuming).
4. `utf8_break_certain`'s conservatism analysis (GB11 context)
   inherits any `gcb_of` error — re-audit it against the fix.

Why tests didn't catch it: `t_table_facts` evidently checks the
below-0x300 *GCB-property* facts (CR/LF/Control) the comment lists,
and `GraphemeBreakTest.txt` coverage of ©/® in GB11 positions is
thin/absent in the vendored corpus version — the bead's report
should state which corpus cases (if any) exercise U+00A9/U+00AE
so the gap is named, not rediscovered.

**Fix shape** (worker's recorded choice): special-case the two
code points in the `cp < 0x300` branch of `gcb_of` (mirroring the
existing `cp == 0xAD` special case in the width fast path and in
this same function) *or* drop the shortcut for the pict check
only. The two-code-point form is preferred: it keeps the hot
path branch shape, and the comment must then say *why* those two
(only ExtPict < 0x300 in the generated table — point at
`UCD_EXT_PICT[0..1]`) so a future Unicode bump that adds a third
sub-0x300 ExtPict fails the new test (below) instead of silently
regressing. Do **not** "fix" by editing `tables.h` or moving
©/® into `UCD_WIDE` (that would make bare © width 2 — a different
bug).

**Red tests first** (Law 9): (a) exhaustive classifier audit —
for every cp in `UCD_EXT_PICT`, `utf8_emoji_class(cp) ==
UTF8_EMOJI_PICT` (fails today exactly on A9/AE); (b) GB11
segmentation cases: `© + ZWJ + U+1F600`-shape and
`U+1F600 + ZWJ + ©`-shape are single clusters via
`utf8_grapheme_next`, plus the chunked `utf8_grapheme_step`
equivalence for the same bytes; (c) `utf8_cluster` width of
`© + VS16` is 2 while bare `©` stays 1 (and `®` likewise) —
the width assertion pins both directions so an over-broad fix
(moving them to WIDE) also fails. All three are red on verbatim
harvest for the reasons above (worker pastes the runs).

## 3. `indent` — `src/indent/`

PLAN disposition (§3): **harvest verbatim**. PLAN §2 lists it in
L1 after piece (`… < piece < undo/lineidx/indent/find`). Pure
query module: it computes edit *descriptions* over `piece`
snapshots/trees; L2 `edit` applies them ("Apply delete+
replacement together in the same undo group", `indent_on_close_
brace` comment — the grouping is the caller's contract).

### 3.1 Files / counts (`wc -l src/indent/*`, M)

| File | Lines | Disposition |
|---|---|---|
| `src/indent/indent.h` | 86 | Verbatim |
| `src/indent/indent.c` | 300 | Verbatim |
| `src/indent/STATUS.md` | 52 | Read only |

### 3.2 Public API (`src/indent/indent.h`, guard `EDIT_INDENT_H`)

Includes `"piece/piece.h"` + libc basics. Header contract: "Pure,
synchronous, allocation-free… No state/resources: no init/fini…
On error scalar/results are cleared, except required size on
CAPACITY." Tree calls are UI-thread (`indent_detect` on a snapshot
is any-thread).

- Constants: `INDENT_PREFIX_BYTES` (64 KiB, wired to
  `indent_detect`'s read bound), `INDENT_TYPING_BYTES` (per-
  direction inspect bound for enter/brace), `INDENT_MAX_WIDTH`,
  `INDENT_NONE`.
- `enum indent_code` (`INDENT_OK`, `ERR_ARGUMENT`, `ERR_RANGE`,
  `ERR_CAPACITY`, `ERR_LIMIT`), `indent_style`, `indent_range`,
  `indent_edit` (flexible-tail insertion description; offsets in
  the *pre-insertion* tree).
- `indent_on_enter` (LF/CRLF + current line's exact SP/TAB leader;
  newline-style cascade documented; CR|LF cursor is RANGE),
  `indent_on_close_brace` (pre-insert-`}` de-indent by one visual
  stop when the line is all SP/TAB; always returns a complete
  1-byte replacement edit), selection-preview twins
  `indent_on_enter_after_delete` /
  `indent_on_close_brace_after_delete` (query the tree with
  `[lo,hi)` logically omitted, omitted bytes never charged to the
  typing bound), `indent_bracket_match` (window-bounded own-type
  balance), `indent_trailing_ws_ranges`,
  `indent_detect` (snapshot) / `indent_detect_bytes` (vote +
  GCD width, defaults as documented: ties → spaces, width absent/
  >16 → 4 — these defaults are v1 product behaviour carried in the
  header comment; keep, don't re-derive).

### 3.3 Dependencies / Windows / globals / fixes

`indent.c` → `"indent.h"` only (transitively `piece`). Conforms.
No Windows code, no globals, **no PLAN fix**. One contract worth
preserving visibly: the `INDENT_TYPING_BYTES` bound returns
`ERR_LIMIT` with *no* output writes when a line boundary isn't
found in bound — this is the module's Law-2/typing-path budget
shape (bounded work or explicit defer-to-caller); later `edit`
must honour the deferral instead of retrying unbounded. Note it,
don't change it.

### 3.4 Tests / fuzz / bench (`wc -l`, M)

- `tests/indent_test.c` — 285. No `#include "*.c"`. Verbatim
  (needs `piece` landed; uses `piece_default_allocator` test path
  like other piece consumers — worker confirms the exact fixture
  construction).
- `fuzz/indent_fuzz.c` — 159. Indent parses line structure over
  arbitrary bytes — Law 1 satisfied. Verbatim.
- `bench/indent_bench.c` — 122. Tracked, exit 0.

## 4. `piece` — `src/piece/`

PLAN disposition (§3): **harvest + targeted fix**, the longest fix
list in L1: "write failing tests first for 17-leaf root collapse
& concurrent-snapshot checkpoint abort; fix root promotion,
staged-init cache invalidation, boundary-privatization-safe
shared add-dir, L0 fault-pin hook". PLAN §2: "piece (harvested
piece_snapshot + piece_checkpoint + boundary-privatization-safe
shared add-dir + L0 pinned fault token hook + TDD test for root
collapse)". This is the centre of Wave 1: L2 `edit`/`file`/
`journal`, plus `undo`/`lineidx`/`find`/`indent` here, all code
against it. It is also the L1 bead most likely to need splitting
(§9 offers the seam).

### 4.1 Files / counts (`wc -l src/piece/*`, M)

| File | Lines | Disposition |
|---|---|---|
| `src/piece/piece.h` | 286 | Harvest + fault-token surface (§4.5(d)); header *is* the frozen spec (P1.3/P1.3b/P1.3c amendments inline) |
| `src/piece/piece.c` | 1742 | Harvest + §4.5 fixes |
| `src/piece/piece_test.h` | 55 | Harvest mechanism (§0.3): `PIECE_TESTING`-gated observability |
| `src/piece/STATUS.md` | 102 | Read only |

### 4.2 Public API (`src/piece/piece.h`, guard `PIECE_H`)

Includes `<stddef.h>`/`<stdint.h>` only — no module header (the
allocator-hooks design is *why*: see below). The preamble is a
frozen spec with amendment history (P1.3 model, P1.3b allocator
backing + REQUIRED append coalescing + reinsert-by-reference,
P1.3c checkpoint contract, P1.4 threading). Harvest the preamble
with the code; amendments in v2 get appended in the same style
via decision notes, never edited away.

- Model: persistent piece tree over ORIGINAL (copied or mapped,
  immutable) + append-only chunked ADD buffer; `uint64_t` byte
  offsets; line = `'\n'` splits, `line_count = newlines + 1`.
- Errors: `PIECE_OK/ERR_RANGE/ERR_NOMEM`, `PIECE_MORE` (staged
  init); failure leaves content unchanged; no `abort`.
- Allocation contract (Law 2): *all* implementation memory comes
  through `piece_allocator {ctx, alloc, free}` hooks given at
  `piece_create` (sized free for pools; 16-byte alignment).
  `piece_insert`/`piece_delete`/`piece_snapshot_take` are typing-
  path: hooks must serve from prepaid pools/arenas. The header
  points at `src/base` (`edit_pool` fixed, `edit_arena` variable)
  as the backing — but the *wiring* (who creates pools sized how)
  is the consumer's: PLAN §4.1 sizes the prepaid COW node pool
  from simultaneously retained revisions × the 1920 B/edit bound
  (piece.h MEMORY BOUND, §0.2). That sizing lives wherever trees
  are created (L2/L5 in v2) — the piece bead provides the hooks
  + a test allocator, and must *state* the prepayment interface
  it expects (fixed request sizes it will make: node slabs,
  chunks, snapshot headers — enumerable from `piece.c`'s `core`
  pools) so the L2 bead can prepay correctly. This is the
  piece↔base seam; get it in writing in the decision note.
  `piece_default_allocator()` = malloc/free, TEST ONLY (keep the
  label loud; production linking it would void Law 2).
- Mapping hooks: `piece_map_hooks {ctx, acquire, release}` — one
  acquire per owner (tree + each snapshot), any-thread. This is
  the seam §4.5(d) extends.
- Lifecycle: `piece_create`, `piece_destroy` (snapshots outlive),
  `piece_reclaim` (owner-thread bounded retirement of worker-
  released snapshot debris; also serviced in fixed batches by
  mutations/queries/takes — the deferred-reclamation design is
  load-bearing for typing latency, harvest intact),
  `piece_init_copy`, `piece_init_mapped`, staged
  `piece_init_mapped_begin`/`piece_init_mapped_step` (bounded
  chunks per step, `PIECE_MORE`, destroy rolls back through
  allocator.free; arena-exclusive-reservation requirement noted
  in the comment).
- Mutation: `piece_insert`, `piece_delete` (deleted ORIGINAL
  bytes copied to add buffer so undo survives mapping loss;
  `piece_ref` with `PIECE_REF_SPANS` always-complete fallback to
  one copied span), `piece_insert_ref` (no-copy reinsert; ref
  validity = tree lifetime, with the P1.3c aborted-checkpoint
  exception class fully specified in the header — including
  which invalidity is cheaply detectable vs caller-error).
- Queries/iters: `piece_len`, `piece_piece_count`,
  `piece_line_count`, `piece_read`, `piece_line_to_byte`,
  `piece_byte_to_line`; snapshots: `piece_snapshot_take`
  (O(1) persistent root share), `piece_snapshot_retain` (atomic),
  `piece_snapshot_release` (any thread, exactly once per owner),
  `piece_snapshot_len/line_count/read/line_to_byte/byte_to_line`,
  `piece_snapshot_mapping` (borrowed hooks or NULL); zero-copy
  `piece_iter` + `piece_iter_begin[_snapshot]`,
  `piece_iter_next`, `piece_iter_next_source` (ORIGINAL offset or
  `PIECE_ADD`; increasing-order guarantee for original spans —
  L2 save/reflink logic will lean on this).
- Checkpoint (P1.3c, the atomic edit transaction):
  `piece_checkpoint_begin/commit/abort` — one active per tree,
  UI-only; commit/abort allocate *nothing* and cannot fail, abort
  restores exact pre-begin observable state incl. coalescing
  control state, *even if every allocator request after begin
  returned NULL*. Snapshots inside a checkpoint survive abort
  with their content; refs minted inside an aborted checkpoint
  are invalid (detectable subset returns RANGE). This contract
  is what L2 `edit`'s journal preflight stands on (PLAN §4.2).

Implementation map (`src/piece/piece.c`, private): persistent
16-way B+ tree (`FAN` 16, `node` SoA leaf/internal layouts,
`ent` entries); `core` (allocator, 3 node pools with slab
accounting, `add` store, `original` store, reclaim queue);
`add_store`/`add_chunk` (64 KiB chunks, `CH_SHIFT`, per-chunk
newline prefix blocks `nl_prefix`/`nl_total`, refcounted chunks,
atomic `tbl` directory, sealed-view `add_store_prune`);
`orig_store`; COW path-copying (`cow`, node `rc` atomics);
insert/delete recursion (`ins_batch_rec`, `del_rec`, leaf
`ent` replace/split helper, `root_grow`, and the delete-side
root-shrink loop §4.5(a)); reserve accounting (`branches`/
`leaves` prepayment computations before mutation — the "may
reserve" machinery that makes NOMEM leave content unchanged);
`checkpoint_snapshot_storage` (§4.5(c)); staged-init builder
(`piece_init_mapped_begin/step` + builder state); snapshot cache
(`piece_tree.cached_snapshot`, `snapshot_uncache`,
`piece_snapshot_take` cache path §4.5(b)); stats under
`PIECE_TESTING` (`piece_test_get_stats` etc., plus fixtures:
`piece_test_repeat_byte` shared-DAG huge-content builder,
`piece_test_set_add_length`, `piece_test_snapshot_set_owners`,
`piece_test_reclaim_hook`).

### 4.3 Dependencies / Windows / globals

`piece.c` → `piece/piece.h`, `piece/piece_test.h`,
`base/base.h`, `scan/scan.h`, `os/os.h`. `piece.h` → nothing.
Conforms (`scan,utf8 < piece`; utf8 unused — piece is byte-exact,
correctly). No Windows code. No file-scope mutable globals
(per-`core` state; snapshot refcounts are per-object atomics).
Threading contract (header): tree = UI-owner only; snapshots
immutable, any-thread read, any-thread release; allocator `free`
may run on the releasing thread (hook thread-safety requirement
— the base pools fronting it in production must honour that;
v1's `core` pool design with deferred reclaim queue is how, keep
the pairing in mind for TSan §4.6).

### 4.4 `piece_test.h` handling

Keep the file and its gate (`PIECE_TESTING`, auto-on under ASan,
explicit `-DPIECE_TESTING` otherwise; ordinary release builds
export nothing). v2-specific acceptance: (i) release `libtext.a`
contains no `piece_test_*` symbols (nm check in bead report);
(ii) test builds get them by compiling `piece.c` with the define
through the *test* object lane, not by a second source copy;
(iii) the two "fake state" hooks (`piece_test_set_add_length`,
`piece_test_snapshot_set_owners`) are documented in the header
as quiescence-required — TSan runs must respect that or annotate
precisely those fixtures, never blanket-suppress piece.

### 4.5 Targeted fixes, by symbol

**(a) 17-leaf root collapse / root promotion.** Located code:
the shrink loop at the tail of `piece_delete` in
`src/piece/piece.c`:

```c
t->root = cow(c, t->root);
node *sib = del_rec(c, t->root, off, off + len);
if (sib) root_grow(t, sib);
while (!t->root->leaf && t->root->cnt == 1) {
    node *old = t->root; t->root = old->v.in.ch[0]; pool_free(c, old, 1); t->height--;
}
```

with `root_grow` (new internal root over old root + sibling,
`height++`) as its inverse, `FAN` = 16 (so the 17th piece/entry
forces the first split — the "17" in the bug's name), and the
whole-tree-delete shortcut just above it (`off == 0 && len ==
total` → fresh leaf root, `height = 0`). PLAN §6 Wave 0 names
"17-leaf piece root collapse" as a known-v1-bug expected-failure
spec v2 must pass; PLAN §3 says "fix root promotion". This
dossier does **not** assert the precise defect — this lane did
not build or run v1 (brief), and the loop *reads* plausibly.
What the bead must do, in Law-9 order:

1. Deciding probe first: construct trees that grow a leaf root
   past FAN entries (root split → height 1), grow to height 2,
   then delete down across each collapse boundary (cnt 2→1 at
   each root level), verifying after *every* step against the
   flat model (`tests/piece_model.h` + full `piece_read` +
   `piece_line_count` + iterator span concatenation +
   `piece_test_get_stats.height` where available), including
   with a live snapshot retaining the pre-collapse root
   (promotion must not free or mutate snapshot-shared nodes:
   the promoted child `old->v.in.ch[0]` may be shared (rc > 1);
   `pool_free(old)` must be exactly balanced against the `cow`
   that privatised the old root — refcount arithmetic at this
   site and in `del_rec`'s merge path is suspect-space #1).
2. Boundary variants: collapse to a *leaf* root then keep
   editing (append-coalescing run state across the collapse);
   collapse inside a checkpoint then abort; delete-all
   shortcut vs incremental collapse to empty must agree.
3. Only then fix what the probe proves broken, minimally, at
   this site or in `del_rec`/`root_grow` accounting. If the
   probe shows *no* defect at the pin, the bead says so with
   evidence, keeps the probe as the regression test, and the
   coordinator updates PLAN's known-bug list — a non-reproducing
   expected-failure must not be "fixed" blind (Law 1/8).

Scope guard: the same promotion pattern exists in the test-only
`piece_test_repeat_byte` DAG builder (its own height loop) —
fixture code, not production; don't conflate a fixture bug with
the tree bug.

**(b) Staged-init cache invalidation.** Located asymmetry:
`piece_init_copy` and `piece_init_mapped` end with
`t->len = len; t->inited = 1; snapshot_uncache(t);`, while the
completion path of `piece_init_mapped_step` sets
`t->len = b->len; t->inited = 1;` *without* `snapshot_uncache`
(builder struct `b` in `piece.c`). `snapshot_uncache` drops
`t->cached_snapshot`; `piece_snapshot_take` returns
`piece_snapshot_retain(t->cached_snapshot)` on cache hit, and
checkpoint abort explicitly nulls the cache (saved-state
restore in the abort path) — i.e. every other content-setting
transition invalidates, staged completion apparently doesn't.
The deciding question for the bead: can a cached (empty-tree)
snapshot exist on a tree that then goes through staged init
(snapshot taken before `piece_init_mapped_begin`? the contract
says "Only step/destroy may be called on the staging tree until
OK" — but a snapshot taken *before* begin, on the empty tree,
then retained in cache across a successful staged init, would
make the next `piece_snapshot_take` hand back the empty
snapshot for a now-full tree). Red test: create → snapshot_take
(cache the empty snapshot) → staged-init to completion →
snapshot_take → assert snapshot len/content equals staged
content. Fix is then presumably one `snapshot_uncache(t)` (and
audit `piece_init_mapped_step`'s other completion bookkeeping
against the non-staged twins field by field — the report lists
the compared fields). If the probe shows the cache cannot be
populated on that path (some guard this dossier didn't trace),
the fix becomes defence-in-depth + regression test, recorded as
such.

**(c) Boundary-privatization-safe shared add-dir + checkpoint
abort under concurrent snapshot.** Two halves of one mechanism:

- *Current shape:* snapshot/checkpoint views get an add-store
  via `add_store_prefix` (`src/piece/piece.c`): allocates a new
  chunk-pointer table (`n * sizeof *tbl` through the allocator)
  and refcount-bumps *every* chunk in the prefix — O(chunks)
  work and an allocator call per snapshot-retain boundary.
  PLAN §4.1 wants "O(1) shared add-chunk directories" (share the
  directory itself, refcount the store) — that is what lets L2
  `edit` checkpoint+snaphot on the typing path without an
  O(file) table copy. The conversion must preserve:
  `add_store_prune` semantics on sealed views (transaction-only
  chunks reclaimable while older snapshots survive),
  `add_len` logical truncation on abort vs physical retention
  for surviving snapshots (P1.3c header, "Logical add-tail
  truncation must preserve these immutable snapshot owners"),
  and refcount correctness across any-thread snapshot release
  (chunk/store rc atomics are acq_rel today — keep or
  re-justify every ordering).
- *Boundary privatization:* `checkpoint_snapshot_storage`
  (PLAN's `piece.c:441`, §0.2) pre-copies the partially-filled
  boundary chunk into the restore view *before* the first
  in-checkpoint snapshot can observe it, flagged by
  `cp->boundary_private`, so abort never needs to allocate or
  copy ("Abort itself only swaps storage"). PLAN §4.1: preserve
  this prepayment "whenever a live snapshot can observe
  uncommitted checkpoint bytes — gated by an OOM +
  concurrent-reader test in Wave 1". Red tests first: (i)
  checkpoint with a live snapshot taken *inside* it, allocator
  failing every request from that point, abort must restore
  exact pre-begin bytes *and* the inside snapshot must keep its
  own bytes (both directions of the P1.3c contract), with a
  worker thread reading the inside snapshot during abort
  (TSan); (ii) the shared-dir conversion must keep (i) green
  and make snapshot-take's allocator traffic O(1)-in-chunks —
  measurable via a counting test allocator (alloc calls for
  take at small vs huge chunk counts, the huge case built with
  `piece_test_repeat_byte`-style fixtures or real mapped data;
  assertion is on call *count shape*, a (P)-style structural
  claim, not a timing gate). If (ii)'s O(1) assertion already
  holds for the *snapshot* path (worker verifies which paths
  call `add_store_prefix`: snapshot take, checkpoint begin,
  abort-restore — the report maps them), narrow the conversion
  to the paths that are actually O(n).

**(d) L0 pinned fault token hook.** Extend the mapping seam so
snapshots can answer "faulted?" and hold the backing against
slot reuse, without L1 naming L2 (§0.1): the concrete shape is
co-designed with L0-2 — candidates: (i) grow `piece_map_hooks`
with a `fault_poll` callback + opaque token stored per
snapshot at `piece_init_mapped`; (ii) first-class
`piece_snapshot_faulted(const piece_snapshot *)` in `piece.h`
backed by an L0 pin retained inside the snapshot. (ii) reads
better for `find`'s `poll_stop` (one call, no ctx plumbing) and
for `lineidx_bind_snapshot` (bind = retain the snapshot's pin
view). Either way: pin acquired where the mapping is installed
(L2 `file` hands token at init_mapped; piece itself must not
call `os_fault_register` — registration belongs to the mapping
owner, piece only carries/pins/polls); release exactly balanced
with `piece_map_hooks.release` per owner; tree-destroyed-but-
snapshot-alive case must keep polling correctly (snapshots
outlive trees). Red test: mapped original (test allocator +
real `os_map_file`+`os_fault_register` fixture via `os` test
patterns), snapshot, `os_fault_test_trigger`, assert
snapshot-fault query flips and pre-fault reads before the
faulting page still match; retire+slot-reuse half is L0-2's
test, this bead asserts the *piece-level* plumbing doesn't
break it (release snapshot after retire → no use-after-free
under ASan).

**(e) Everything else verbatim.** Append coalescing rules, ref
span fallback accounting (`deleted_original`, `fallback_add`
G10f fields), reclaim batching, reserve-before-mutate NOMEM
atomicity — untouched. The bead's diff manifest (like L0 work's)
lists (a)–(d) hunks and nothing else.

### 4.6 Tests / fuzz / bench (`wc -l`, M)

- `tests/piece_test.c` — 680 (core model tests),
  `tests/piece_bounds_test.c` — 402 (range/overflow/refusals),
  `tests/piece_mem_test.c` — 239 (G10f bound accounting via
  `piece_test_get_memory`), `tests/piece_mt_test.c` — 654
  (concurrent snapshot readers/releasers — PLAN §5 TSan member:
  "make check-tsan … (work, piece, journal, …)" and Wave 1 exit
  gate names piece), shared `tests/piece_model.h` — 71 (flat
  reference model; harvest with the suites). **No**
  `tests/piece_checkpoint_test.c` exists as its own file at the
  pin — checkpoint coverage lives inside the suites above and
  `fuzz/piece_checkpoint_fuzz.c` (worker maps where, in the
  report; new §4.5 tests go with their kin). No `#include "*.c"`
  in any of them (they use `piece_test.h` — the sanctioned
  channel, §4.4).
- `fuzz/piece_fuzz.c` — 219 (model-differential mutations),
  `fuzz/piece_checkpoint_fuzz.c` — 177 (checkpoint abort/commit
  under snapshots — the natural home for §4.5(c)'s fuzz op
  extension: fault-injecting allocator as a fuzz dimension).
  Law 1 satisfied richly; keep both, extend ops only for
  (a)/(b)/(c) shapes the red tests introduce.
- `bench/piece_bench.c` — 1045 plus `bench/piece_bench.args`,
  `bench/piece_reclaim_bench.c` (+ `.args`) in the v1 bench dir
  (line count for the latter not in this dossier's `wc`
  invocation — worker measures on harvest). Tracked (M) numbers;
  the §4.5(c) O(1) claim's bench side (snapshot-take cost vs
  file size) belongs here as a row, exit 0.

## 5. `undo` — `src/undo/`

PLAN disposition (§3): **harvest + targeted fix** — "preserve
burst coalescing, cap eviction, sliced replay + shared add-dir
boundary". I.e. the module ports intact; its one change is
*fallout* from piece §4.5(c) (the add-dir boundary it stands on),
plus verifying the three named behaviours survive.

### 5.1 Files / counts (`wc -l src/undo/*`, M)

| File | Lines | Disposition |
|---|---|---|
| `src/undo/undo.h` | 144 | Harvest (header is the replay/atomicity spec) |
| `src/undo/undo.c` | 492 | Harvest (+ §5.5 fallout only) |
| `src/undo/STATUS.md` | 93 | Read only |

### 5.2 Public API (`src/undo/undo.h`, guard `EDIT_UNDO_H`)

Includes `"base/base.h"` + `"piece/piece.h"`. UI-thread inverse
log over one `piece_tree` (tree must outlive log; mutations go
through the log). Header contracts to preserve verbatim:

- Return codes alias piece's (`UNDO_OK = PIECE_OK`, …) +
  `UNDO_ERR_BUSY`, `UNDO_MORE`; `UNDO_BURST_NS` (adjacent
  same-kind edits ≤300 ms apart, nondecreasing timestamps, join
  a burst — value as defined there; product behaviour, keep);
  `UNDO_RECLAIM_RECORDS` per-edit reclaim bound;
  `UNDO_STATE_BYTES` opaque cursor-state blobs (`undo_state`,
  copied unchanged — selection lives in L2 `edit`, undo just
  ferries the bytes).
- `undo_kind` (INSERT/BACKSPACE/DELETE — burst adjacency keys),
  `undo_change` (dirty prefix accounting incl. aborted-group
  rules), `undo_history` (O(1) boundary counts, no storage
  reads — typing-admission safe), `undo_stats` (G10f accounting
  incl. retired + replay scratch; `committed_bytes` vs
  `reserved_bytes` distinction documented — pages decommitted on
  free tail via L0 `os_vm_decommit` through pools).
- Public-layout `undo_log` (caller-owned, "No copies while
  initialized"; embeds two `edit_pool`s — main + `replay_pool`
  capture scratch — and the sliced-replay state:
  `replay_view`/`replay_checkpoint`/`replay_guard` snapshots,
  `partial`, `open`, `burst` flags).
- Ops: `undo_init` (reserves lazy worst-case capture capacity —
  *no undo allocation after init* is the Law-2 shape; admission
  uses `max_records + 8` slots so first-undo capture can't
  exhaust scratch), `undo_destroy`, `undo_clear`,
  `undo_break_burst`, `undo_set_cap` (whole-group eviction,
  bounded boundary visits per trim), `undo_group_begin/end`
  (nesting rejected), `undo_insert`, `undo_delete`,
  `undo_undo`/`undo_redo` (whole), `undo_undo_slice`/
  `undo_redo_slice` (operation budget + absolute monotonic
  deadline; one piece checkpoint per group; any failure aborts
  the *current group only* byte-exactly, completed groups in
  the batch stay), `undo_replay_snapshot` (borrowed pre-group
  view during partial replay — render/save read this, never the
  half-replayed tree), `undo_maintain`, `undo_get_history`,
  `undo_get_stats`.
- Hard rules in the header the harvest must not soften: nothing
  else may mutate the tree between replay slices (asserted via
  retained snapshot identity); edits/cap-changes/opposite-replay
  are BUSY while partial; "No piece add-buffer bytes are freed"
  (undo never shrinks the add buffer — piece refs stay valid;
  this line is why §4.5(c)'s pruning must stay transaction-
  scoped, §5.5).

`undo.c` additionally includes `os/os.h` (clocks for deadlines /
slice checks). FSM-lint note: partial/open/burst are `int` flags
in `undo_log`, not enums — same reading as L0 ipc §5.2 (sliced-
replay *phases* are arguably a lifecycle). Record the reading;
converting undo's replay state machine to an explicit
`-Wswitch-enum` enum is a rewrite of the module's trickiest
logic — do it only if the coordinator explicitly scopes it
(CLAUDE.md's FSM rule vs PLAN's "preserve" disposition is a
genuine tension; flag it in the bead, default to preserve +
record).

### 5.3 Dependencies / Windows / globals

`undo.h` → piece + base; `undo.c` → + os. Conforms
(`piece < undo`). No Windows code, no globals (one `undo_log` per
buffer, caller-owned).

### 5.4 Tests / fuzz / bench (`wc -l`, M)

- `tests/undo_test.c` — 623. No `#include "*.c"`. Verbatim.
- `tests/editor_undo_branch_test.c` — 106: editor-level branching
  (L5-ish real-client); not L1 acceptance (like
  `work_foreground_test.c` in L0) — returns with its clients,
  name the gap.
- `fuzz/undo_fuzz.c` — 226. Verbatim (burst/cap/slice op space —
  worker confirms sliced replay + cap thrash are in its op mix
  and says so; add missing op classes only if absent).
- `bench/undo_bench.c` — 163, **contains `#include
  "../src/undo/undo.c"`** (§0.3). Rewrite: link `libtext.a`
  against `undo.h`; whatever internals it reached for (worker
  enumerates: likely record-level counters) must come via
  `undo_get_stats`/`undo_history` or a recorded decision to add
  a stats field — *not* by keeping the include. If a needed
  counter has no public channel, that's a one-line API addition
  to propose in the bead, not a reason to break the lint.

### 5.5 Targeted adjustment: shared add-dir boundary (fallout)

Undo has no independent PLAN fix. Its exposure to §4.5(c):
sliced replay captures inverses into `replay_pool` scratch and
re-inserts deleted ORIGINAL bytes via piece add-buffer refs
across checkpoint boundaries; `undo_replay_snapshot`'s borrowed
view and the `replay_guard` snapshot are precisely "live
snapshots that can observe uncommitted checkpoint bytes"
(PLAN §4.1 checkpoint-abort invariant). Acceptance therefore
re-runs the undo suite *against the converted piece*, with two
named cases: (i) sliced undo of a group whose replay crosses a
checkpoint abort mid-slice (allocator-failure injected) —
pre-group bytes exact, history counts exact, retry succeeds;
(ii) undo/redo across the boundary chunk (edits straddling the
64 KiB add-chunk edge inside a checkpoint + abort) byte-exact.
If both already exist in `undo_test.c`/`undo_fuzz.c`, cite them;
the bead's job is proving piece's conversion didn't move undo's
invariants, plus absorbing any `piece.h` signature change from
§4.5(d) (undo touches snapshots constantly — compile fallout
only, expected zero semantic edits; report the diff).

## 6. `lineidx` — `src/lineidx/`

PLAN disposition (§3): **harvest + targeted fix** — "resumable
continuations + off-path regrowth", specified in PLAN §4.3
(non-vetoing degraded mode). This is the second-largest real
change in L1 after piece: today an over-cap edit is *refused*
and the caller recreates the index on the allocating path; PLAN
§4.3 makes the index degrade instead of vetoing. (Where the
refusal surfaces in v1: `lineidx_edit` returns -1 "Larger
replacements need an index recreated for the resulting content
on the allocating path" — i.e. the *edit pipeline* must handle
recreation synchronously or refuse the user's edit; PLAN calls
that veto the bug: ">32 MiB delete+undo" is a Wave 0
expected-failure spec.)

### 6.1 Files / counts (`wc -l src/lineidx/*`, M)

| File | Lines | Disposition |
|---|---|---|
| `src/lineidx/lineidx.h` | 184 | Harvest + degraded-state surface (§6.5) |
| `src/lineidx/lineidx.c` | 1087 | Harvest + kill `file/file.h` edge (§0.1) + §6.5 |
| `src/lineidx/STATUS.md` | 106 | Read only |

### 6.2 Public API (`src/lineidx/lineidx.h`, guard `LINEIDX_H`)

Includes `"work/work.h"` + `"piece/piece.h"`. Sparse line index:
one 16-byte entry per ≤64 KiB chunk (`LINEIDX_CHUNK`,
`_Static_assert(sizeof(entry) == 16)` in the .c), relative
lengths (no suffix shifting on edit), block-treap summaries,
built incrementally by `work` bulk jobs over immutable
`lineidx_src` span sources (piece_snapshot, mapping, flat).

- Constants: `LINEIDX_CHUNK`, `LINEIDX_MAX_LEN`,
  `LINEIDX_MSG_PROGRESS` (work_msg kind `"LIDX"`),
  `LINEIDX_EDIT_MAX_CHUNKS` (foreground admission limit, §6.5).
- `lineidx_src {ctx, len, span, release}` — span indivisibility
  + CPU-bound contract in the comment (worker vs UI sources);
  `lineidx_result {value, exact}` — the estimate contract every
  query honours; opaque `lineidx`.
- Lifecycle: `lineidx_create` (fixed capacity; ordinary reserve
  stated in `lineidx_create_reserved` comment),
  `lineidx_create_reserved` (extra chunks prepaid on the open
  path — the *only* growth mechanism today, §6.5),
  `lineidx_bind_snapshot` (fault token bind; §0.1/§6.5 — its
  comment is the §4.1 contract), `lineidx_destroy` (cancel +
  physical retirement wait via work completion).
- Build: `lineidx_build_start` (bulk, owns source release on
  success, caller keeps on -1), `lineidx_build_reserve` +
  `lineidx_build_start_prepaid` (allocation-free restart;
  preparation itself is sliced — `lineidx_build_preparing`/
  `lineidx_build_needs_poll`), `lineidx_build_start_foreground`
  (resident-source interactive jump on a foreground-enabled
  pool), `lineidx_build_prioritize` (lease-preserving upgrade —
  rides `work_prioritize`), `lineidx_build_start_owned`
  (memory-accounted variant), `lineidx_build_cancel` (logical
  only), `lineidx_poll` (bounded staging+adoption, CPU-deadline
  between entries, bounds stated in its comment),
  `lineidx_building`.
- Edit/refresh: `lineidx_edit_check` (admission dry-run
  guaranteeing `lineidx_edit`), `lineidx_edit` (cancel build,
  keep adopted counts, touched chunks become bounded unbuilt
  *edited* chunks; -1 pre-mutation on overflow/capacity/over-
  `LINEIDX_EDIT_MAX_CHUNKS` — §6.5's target),
  `lineidx_refresh` (≤1 chunk/call foreground rescan with span/
  CPU bounds in its comment).
- Queries (UI, no alloc): `lineidx_line_count`,
  `lineidx_line_to_byte`, `lineidx_byte_to_line` (exact in
  built prefix, else byte-proportion estimates),
  `lineidx_seek_line` (synchronous budgeted continuation —
  bounds in comment; partial counts persist without marking
  chunks built), worker `lineidx_seek_start_owned` +
  `lineidx_seek_result`, state/accounting
  (`lineidx_len`, `lineidx_chunk_count`,
  `lineidx_built_prefix`, `lineidx_complete`,
  `lineidx_chunk_nonascii`, `lineidx_any_nonascii`,
  `lineidx_entry_bytes`, `lineidx_mem_bytes`,
  `lineidx_foreground_work`, `lineidx_scanned_bytes`,
  `lineidx_cancel_cpu_ns` — via work completion ack, feeds
  `os_thread_cpu_ns` accounting in L0).

So "resumable query continuations" (PLAN §2) *largely exist*
(`lineidx_seek_line` + prepaid-preparation staging + sliced
refresh/poll). The bead verifies each query path L4 will call
has a budgeted form and names any that doesn't; the new work
concentrates in §6.5.

Implementation map (`lineidx.c`): `entry`/`block` treap with
incremental summaries (own/total bytes/lines, edited/nonascii/
unbuilt aggregates — `BLOCK_MAX` coalescing note "live block
capacity scales…"), result packing (`RES_DONE/RES_NA/RES_COUNT`
bit layout in work messages), `lineidx_job` (preparing state,
`file_backing *backing` borrowed until physical completion —
§0.1), build worker (bounded batches, `file_backing_faulted`
abort check, `SCAN_BLOCK` scans via `scan`), adoption in poll,
edit planner (the §0.2 `lineidx.c:858` admission line lives in
its capacity check), seek machinery (`MSG_SEEK`).

### 6.3 Dependencies / Windows / globals

Edges in §0.1's table: conforming except `→ file`. Windows: the
`last_byte`/`memrchr` branch only (§0.3). Globals: none (per-
index state; jobs are index-owned). Allocation contract
(header): create/build-start allocate on the open path;
edit/check/refresh/poll/prepaid-restart/queries **never**
allocate — fixed capacity is the mechanism, which is exactly
what makes regrowth an off-path design problem (§6.5).

### 6.4 Tests / fuzz / bench (`wc -l`, M)

- `tests/lineidx_test.c` — 1490 (the big suite; model queries,
  build/cancel/adopt, edits, seeks). No `#include "*.c"`.
- `tests/lineidx_os_test.c` — 56, **includes
  `../src/lineidx/lineidx.c`** (§0.3). Its name says it tests
  the os/fault integration — precisely the surface §0.1
  re-routes through `piece`. Rewrite against public headers +
  `--wrap` (wrap `os_fault_test_trigger`-adjacent entry or the
  piece fault query once §4.5(d) names it); if it was reaching
  statics, those cases become black-box fault cases at the
  `lineidx_bind_snapshot` level. Enumerate per-case in the
  bead report (56 lines — cheap census, no silent drops).
- `fuzz/lineidx_fuzz.c` — 190. Verbatim; add degraded-mode ops
  (§6.5) to its mix when they exist.
- `bench/lineidx_bench.c` — 1044. Tracked; keep its occupied-
  bulk/navigation-style rows — they are the (M) side of PLAN
  §4.3 point 3 ("foreground navigation makes bounded progress
  even while WORK_BULK is saturated").

### 6.5 Targeted fix: degraded mode + off-path regrowth (PLAN §4.3)

Current failure shape, precisely: `lineidx_edit` (via its
planner, admission at `lineidx.c:858` §0.2) returns -1 *before
any mutation* when replacement pieces exceed remaining fixed
capacity (`x->cap`) or `LINEIDX_EDIT_MAX_CHUNKS` chunks. The
caller (edit pipeline) must then recreate the index at the new
length on the allocating path — an unbounded, typing-adjacent
operation, or a refused edit. For a >32 MiB delete+undo the
replacement side alone busts the chunk limit (PLAN §4.3's (P)
arithmetic on the header constants).

PLAN §4.3's replacement semantics (implement exactly; these are
the acceptance clauses):

1. **Degrade, don't veto.** On such an edit the index enters a
   *named degraded state* (PLAN spells it `LINEIDX_DEGRADED` —
   an explicit enum under `-Wswitch-enum` per CLAUDE.md FSM
   rule; today there is no such state, only per-chunk
   FL_BUILT/FL_EDITED flags in `lineidx.c`). In degraded state,
   queries stop returning exact geometry from the stale table:
   gutter/minimap/scrollbar get the existing
   `{value, exact=false}` estimates (§0.2), goto-line uses the
   budgeted `lineidx_seek_line` continuation. The edit itself
   always succeeds (index bookkeeping can no longer refuse a
   user edit — `lineidx_edit_check`'s meaning changes
   accordingly: coordinator/bead records whether it is kept as
   "will stay exact" predictor or retired; L2 `edit` is the
   consumer, name the choice for it).
2. **Off-path regrowth.** A `WORK_BULK` rebuild over the
   post-edit `piece_snapshot` allocates a *replacement table
   sized to post-edit length* — allocation on the worker/open
   path, never the typing path — and swaps it in on the UI
   thread only if the revision stamp still matches. PLAN names
   the stamp `(doc_revision, fault_epoch)`: `doc_revision` is a
   new counter the index (or its owner) bumps per edit —
   define its owner in the decision note (index-internal edit
   serial is the obvious home; it must also invalidate a
   *prepaid* in-flight build the same way); `fault_epoch` rides
   the §4.5(d)/L0-2 pin poll. Swap discipline mirrors build
   adoption (generation/lease-validated via work mailboxes,
   physical completion before source release) — reuse the
   existing job machinery, don't invent a second channel.
3. **Bounded foreground progress regardless.** While degraded
   and/or bulk-saturated, `lineidx_seek_line`/refresh keep their
   per-call bounds (header comments) — the bead's occupied-bulk
   test is the L1 shadow of `editor_large_test.c:1119` (§0.2).
4. **Growth past initial reserve is tested** (PLAN verbatim):
   create at length L with default reserve, edit well past it
   (delete+insert and the >32 MiB-chunk-limit undo shapes),
   assert degrade → rebuild → exact again, byte/line answers
   model-checked at every stage, malloc guard == 0 across the
   foreground edit+query windows (release build).

Also in this bead: delete the `file/file.h` include (§0.1) —
`lineidx_bind_snapshot` keeps its signature and contract, its
guts switch to the piece fault surface; the job's
`file_backing *backing` field becomes the retained snapshot/pin
view; every `file_backing_faulted` call site (worker abort,
adoption guard, `lineidx_any_nonascii`, chunk query) switches
to the snapshot poll. Red test for the whole §6.5: the
>32 MiB delete+undo sequence (constructible cheaply over a
mapped sparse fixture or `piece_test_repeat_byte`-style DAG +
real lineidx over a flat/snapshot source — worker picks the
fixture that runs in `make check` time discipline, *without*
inventing a size budget: the test's size derives from
`LINEIDX_EDIT_MAX_CHUNKS × LINEIDX_CHUNK` + ε over, i.e. from
the header constants themselves) — against verbatim code
`lineidx_edit` returns -1 (red: edit vetoed), after the fix the
edit applies, queries degrade honestly (`exact=false` where
stale), rebuild restores exactness (green).

Non-goals: don't retune `LINEIDX_EDIT_MAX_CHUNKS` or
`LINEIDX_CHUNK` to make the red test pass (that's moving the
fence; the constants' (P) basis is PLAN-cited); don't let
degraded estimates leak as `exact=true` anywhere — the fuzz op
(§6.4) model-checks exactness flags, not just values.

## 7. `find` — `src/find/`

PLAN disposition (§3): **harvest + targeted fix** — "promote
`visit` + native ASCII fold & whole-word". PLAN §2: "find
(unified streaming visit + native ASCII fold & whole-word in one
literal engine)". Today those three things exist but live in the
wrong places (internal header, regex-expansion shim, and an L4
duplicate engine PLAN deletes); §7.5 maps the unification.

### 7.1 Files / counts (`wc -l src/find/*`, M)

| File | Lines | Disposition |
|---|---|---|
| `src/find/find.h` | 167 | Harvest + promote visit/fold/word surface (§7.5) — header calls itself "frozen"; v2 thaws it once, deliberately, via decision note |
| `src/find/find.c` | 539 | Harvest (regex engine + search/visit/stream drivers) |
| `src/find/literal.c` | 455 | Harvest + native fold/word (§7.5) |
| `src/find/literal.h` | 32 | Internal kernel header; stays internal |
| `src/find/find_int.h` | 86 | Internal meter/reader; **drop `file/file.h`** (§0.1) |
| `src/find/visit.h` | 86 | "Internal integration API" → promoted (§7.5) |
| `src/find/options.c` / `options.h` | 109 / 21 | ASCII option layer (regex-expansion); fate decided in §7.5 |
| `src/find/STATUS.md` | 123 | Read only (P1.10b fold-in record `edit-4w1.52`) |

### 7.2 Public API (`src/find/find.h`, guard `EDIT_FIND_H`)

"P1.10a frozen byte-search contract." Includes
`"piece/piece.h"`, `"work/work.h"`, `<stdatomic.h>` et al.
No allocation by any search/capture/compile call (caller
storage everywhere) — Law-2-shaped by design.

- Bounds (header-defined, keep): `FIND_MAX_OFFSETS`,
  `FIND_MAX_GROUPS`, `FIND_MAX_STATES`, `FIND_MAX_PATTERN`,
  `FIND_MAX_DEPTH`, `FIND_POLL_UNITS` (metering quantum — the
  work-unit definition is spelled out in `find_control`'s
  comment: one comparison / NFA visit / candidate advance),
  `FIND_MAX_PROGRAM_BYTES`, `FIND_MAX_SCRATCH_BYTES`,
  `FIND_UNSET`.
- `enum find_code` (`FIND_OK`, `FIND_CANCELLED`,
  `FIND_ERR_ARGUMENT/MEMORY/SYNTAX/UNSUPPORTED/LIMIT`),
  `find_source` (flat bytes XOR `piece_snapshot`),
  `find_control` (OR-ed cancellation: `work_ctx` epoch via
  `work_should_stop`, `atomic_bool` cancel, generation pair —
  with the publication-suppression rules in its comment:
  never publish partial-as-exact or cancelled results),
  `find_result` (exact total + first-stored offsets, ascending;
  cleared on any non-OK).
- Literal: `find_literal` (leftmost non-overlapping; empty
  needle = 0 matches; arbitrary bytes incl. NUL; **"no Unicode
  or normalization, no case folding"** — the frozen sentence
  §7.5 amends), `find_literal_next` (single-match streaming).
  Worst-case-linearity requirement is contractual in the header
  (SIMD filter under a linear verify budget, then Two-Way).
- Regex (ERE-lite, fully specified in the header: multiline
  anchors, dot excludes LF, negated classes include LF,
  unsupported constructs rejected not reinterpreted, limits →
  `FIND_ERR_LIMIT`): opaque `find_regex`,
  `find_regex_bytes`/`find_regex_compile` (caller storage,
  error_offset), `find_regex_prefix` (mandatory-prefix
  soundness rule for filters), `find_regex_group_count`,
  `find_regex_scratch_bytes`, `find_regex_search`
  (leftmost-longest, Thompson NFA, empty-match-per-boundary
  rule, subgroup tie policy documented — not full POSIX),
  `find_capture`/`find_match`, `find_regex_captures` (anchored
  re-run), `find_regex_next`. Compile also allocates nothing.
  This spec density is why "harvest" beats "rewrite" here:
  the header *is* the oracle for the fuzzers.

Internal surface (same module, not for L4+ until §7.5):
`find_int.h` — `meter` (poll accounting; `poll_stop` also
checks the fault query, §0.1), `reader`/`get_byte`/
`get_byte_forward` (lazy zero-copy snapshot span reader over
`piece_iter`; forward variant for Two-Way cursors),
`clear_match`, `add_result`. `literal.h` — `find_lit` plan
(rare-byte-pair SIMD filter by static frequency table, verify,
lazy Two-Way with `periodic/ell/per`, work counters),
`find_lit_init`/`find_lit_seek`, `find_literal_mode` (mode 0
production, mode 1 Two-Way-only — the differential-testing
seam; keep). `literal.c` picks AVX2 vs SSE2 scan via
`edit_cpu_has_avx2` at plan init (L0-3's fixed detect matters
here: first call may be on a worker).

### 7.3 The three internal/option headers, precisely

- **`visit.h`** ("Internal integration API; the frozen public
  find.h remains unchanged"): `find_visit` — count-everything,
  deliver-prefix + bounded-window + one wanted ordinal, via
  `emit` callback, plus `emit_mask` bulk sink for compressed
  match plans; header-inline `find_visit_add`/`find_visit_mask`
  (rank/select over SIMD masks by popcount, "never enumerate
  their gap"). Drivers declared: `find_literal_visit`,
  `find_regex_visit`, `find_regex_word_visit` (word-boundary
  filter sharing the request's readers/prefix plan/retry
  budget; rejected candidate retries at start+1 byte),
  `find_regex_stream_begin` and `find_regex_stream_step` (worker continuation in
  prepaid scratch, yields at NFA byte boundaries, exact-policy
  parity with sync API), `find_regex_work_budget`,
  `find_regex_next_budget`. Implemented across `find.c`/
  `literal.c`. This is already a unified streaming core —
  PLAN's "promote visit" is mostly *API placement*, not new
  machinery (§7.5).
- **`options.h`/`options.c`** ("Internal ASCII option layer
  shared by findui and fif, above frozen find.h. The compiled
  core remains byte sensitive"): `find_options_fold` (ASCII
  A-Z→a-z inline), `find_options_word` (`[A-Za-z0-9_]` inline),
  `FIND_OPTIONS_PATTERN_BYTES` (expansion bound, (E)-tagged in
  its comment — keep the tag), and `find_options_compile`,
  which implements case-insensitive *regex* by rewriting the
  query (`a` → `[aA]`, classes via `folded_class` in
  `options.c`) into caller scratch, then compiling the
  byte-sensitive core.
- **The L4 duplicate:** `src/findui/literal.c` ("Persistent
  ASCII-folding KMP") — a *second* literal engine living in the
  UI layer solely because the L1 engine won't fold: SIMD
  first-byte filter with both cases (`_mm_cmpeq` + OR), KMP
  continuation, whole-word checks via `piece_snapshot_read`.
  PLAN §3 deletes `findui/literal.c` in the find_session
  rewrite; the precondition is this bead giving L1's engine the
  same abilities natively.

### 7.4 Dependencies / Windows / globals

Edges per §0.1: `find → piece, work, base` conforming;
`find_int.h → file` the one violation (single call in
`poll_stop`). No Windows code. No mutable globals (programs/
scratch caller-owned; `meter` points at caller control state).
Threading: searches run on workers over retained snapshots +
shared immutable programs + per-call scratch — header states
lifetimes ("Keep snapshot, program, scratch and result alive
through acknowledgement", of the work job).

### 7.5 Targeted fix: unified streaming visit + native fold/whole-word

Design constraints from PLAN §2's parenthesis, made concrete
(worker's decision note finalises shapes; L4 `find_session` is
the consumer whose needs define "done"):

1. **Promote visit.** `find_visit` + the `*_visit` drivers +
   stream begin/step move from "internal integration API" to
   the public contract — either into `find.h` proper (thawing
   the frozen header once; cleaner for L4: one header) or a
   blessed second public header `find/visit.h` with the
   internal-only wording removed (mirrors trace's two-header
   exception, L0 §3). Either is DAG-clean; the choice is
   recorded. What must not survive: L4 including a header that
   calls itself internal. The mask-sink protocol
   (`emit_mask` owns filtering/visible counts) is
   fif/find_panel-shaped; keep it intact — promotion is
   rename/re-home, not redesign.
2. **Native ASCII fold in the literal engine.** `find_lit`
   gains a fold mode: needle stored folded, the rare-pair
   filter matches both cases of its filter bytes (frequency
   choice must consider folded frequencies — worker states how
   p1/p2 selection changes, or doesn't, with evidence from the
   bench), verification compares folded, Two-Way runs over
   folded equality (period computation on the folded needle —
   correct because fold is a per-byte equivalence; state the
   argument in the note). Worst-case-linearity contract must
   survive folding (the verify-budget→Two-Way escape is the
   mechanism; fuzz differential mode 0 vs mode 1 *with fold
   on* is the proof vehicle). Public surface: fold/word become
   parameters of the literal entries (new names or an options
   struct — worker's call, but `find.h`'s frozen "no case
   folding" sentence is amended in the same diff, and
   `find_literal`'s existing signature either stays as the
   case-sensitive spelling or is superseded with callers
   updated in later beads — there are no v2 callers yet, so a
   clean break costs nothing now).
3. **Native whole-word for literal** (regex already has
   `find_regex_word_visit` + `regex_word_boundary` in
   `find.c`): same boundary predicate (`find_options_word`
   classification: `[A-Za-z0-9_]`), same retry policy as the
   regex word path (reject → retry at start+1; accept →
   non-overlap advance). Boundary bytes come from the reader
   (previous byte may live in the preceding piece — the
   `findui/literal.c` version does a 1-byte
   `piece_snapshot_read` for it; the unified engine should use
   its span reader, and the fuzz corpus must include matches
   straddling piece boundaries with word bytes on both sides).
4. **Regex fold stays expansion-based — probably.**
   `find_options_compile`'s `[aA]` rewrite is sound and keeps
   the NFA byte-sensitive/simple; folding *inside* the NFA
   (fold at `get_byte`/class-test) is the alternative. PLAN's
   words ("native ASCII fold & whole-word in one literal
   engine") scope nativity to the *literal* engine; regex
   case-insensitivity via expansion already works and is
   fuzz-covered. Bead keeps `options.c` (re-homed as public
   with visit, or as the find-internal compile helper behind a
   public `match_case` parameter — decide with fif/findui
   consumers in mind) unless the worker demonstrates a defect
   in expansion (e.g. expansion blowing `FIND_MAX_PATTERN`/
   state limits on plausible queries — if demonstrated, that
   repro scopes the deeper change, Law 1).
5. **Delete the upward edge.** `poll_stop`'s
   `file_snapshot_faulted` → the piece snapshot fault poll
   (§4.5(d)); `find_int.h` drops `file/file.h`. Behaviour at
   fault: search stops (cancelled-shaped, `m->stopped`) as
   today — never read zero-filled fault pages as content.
   Red test mirrors §4.5(d)'s: fault mid-search over a mapped
   snapshot terminates with cancellation, not bogus matches
   in the zero suffix.

Red tests first (Law 9): differential vs a brute-force oracle
in the test itself (fold+word on/off × flat/snapshot sources ×
piece-boundary-straddling cases), red on verbatim code because
the API/behaviour doesn't exist (compile-fail counts as red
only if the bead *also* pastes the behavioural spec run against
`findui/literal.c`-equivalent expectations encoded as expected
outputs — better: port `findui/literal.c`'s test expectations
from `tests/findui_test.c`'s literal cases as the oracle data,
cited); green after. Plus: `find_visit` windowing equivalence
(total/wanted/visible identical across result-api, visit-api,
stream-api for the same queries) — partially covered today,
worker maps existing coverage before adding.

### 7.6 Tests / fuzz / bench (`wc -l`, M)

- L1-keepable: `tests/find_test.c` — 308 (frozen-contract
  cases), `tests/find_bestof_test.c` — 418,
  `tests/find_simd_filter_verify_test.c` — 343 (filter/Two-Way
  differential — the linearity contract's teeth),
  `tests/find_timeout_test.c` — 68 (cancellation/metering),
  `tests/find_bench_test.c` — 56 (bench harness self-check;
  classify with the bench). Worker censuses each like L0's
  ipc split: anything reaching into `findui/`/`editor/` moves.
- Rewrite-required: `tests/find_p1r_test.c` — 179, includes
  both `../src/find/literal.c` and `../src/find/find.c`. Its
  access needs (presumably kernel internals: `find_lit` plan
  fields, mode forcing) must be re-homed onto `literal.h`'s
  already-internal-but-linkable surface (`find_lit_init/seek`,
  `find_literal_mode`) linked from `libtext.a` — `literal.h`
  even says "(tests may include it)" — or, for genuine statics,
  dropped with per-case reasons. Census in the report.
- Not L1: `tests/findui_test.c` — 1448 (L4 session/panel),
  `tests/config_find_test.c` — 178 (config real-client). Only
  their literal fold/word *expectations* are mined as oracle
  data per §7.5.
- `fuzz/find_fuzz.c` — 290. Verbatim + fold/word op dimensions
  (§7.5) + fault-stop case (§7.5.5) as a unit (not fuzz) test.
- `bench/find_bench.c` — 339 (+ `find_bench.args`,
  `bench/find_probe.h`, `bench/find_supervise.h` helpers in the
  bench dir — worker measures/harvests the set). Fold-mode rows
  added so the filter-frequency choice (§7.5.2) has (M)
  evidence; exit 0, power-stamped. PLAN §3 fif note ("shared
  find meter") refers to reusing this metering in L4 `fif` —
  no L1 action beyond keeping `meter` semantics stable.

## 8. Cross-cutting: v1 fixes after `c95b6eb` the harvest must keep

**None in L1 production code.** `git log --oneline
c95b6eb..HEAD -- src/scan src/utf8 src/indent src/piece src/undo
src/lineidx src/find` at the pin returns *empty* (M, this lane's
run): all seven modules are bit-identical between PLAN's base
commit and `6fec2e8`. The post-PLAN activity (46 commits, L0
dossier §6) touched L0 (`os` THP + `os_host_name`, `ipc`
activation), platform/editor layers, and docs only.
Consequences:

- PLAN's L1 claims need no post-PLAN correction; every "known
  bug" it lists for L1 (17-leaf collapse, ©/® classification,
  lineidx veto) reproduces-or-not against the pin exactly as
  written against `c95b6eb` — the deciding probes in §4.5/§6.5
  are unaffected by drift.
- L1 test/bench files in the same range: exactly two trivial
  touches (M: pathspec log over this dossier's §1–§7 test/fuzz/
  bench inventories) — `210938b` adds one line (unused-sink fix)
  to `bench/lineidx_bench.c`, and `eac58d0` (`edit-177r.6`,
  Windows dependency-module wiring) adds four lines to
  `tests/indent_test.c` (a `_WIN32` include branch for
  `win/dependency_vm.h` — dropped with the rest of `tests/win/`
  in the harvest). Neither changes any expectation; both ride
  along automatically at the pin.
- Harvest rule unchanged: always from the pin `6fec2e8`, never
  floating v1 `main` (L0 dossier §6) — for L1 today they'd
  coincide, which is exactly when the discipline matters least
  and costs nothing.

## 9. Proposed bead split — L1 part of Wave 1

Same conventions as the L0 dossier §7 shared block (one bead per
worker, Law 9 red-green pasted evidence, `make check` incl.
lints + isolated `--whole-archive` link of `libtext.a` against
L0 only, no `#include "*.c"`, temp-dir-only tests, decision
notes per bead, power-stamped tracked benches, no invented
numbers). Labels `L1-1`… are dossier-local. PLAN §6 Wave 1 exit
gate items owned by this layer: "Failing-then-passing regression
tests for 17-leaf `piece` root collapse, concurrent-snapshot
checkpoint abort under OOM, and >32 MiB delete+undo with
`lineidx` degraded off-path regrowth past reserved capacity" +
"`make check-tsan` green on `work`, `piece`, `journal`,
`savectl`, `app_session`" (italicised members are other
layers'). Each maps to a bead below; none is optional garnish.

Order: `scan` + `utf8` first (zero module deps; parallel-safe,
disjoint files), then `piece` (needs scan, L0-1, L0-3 headers,
and L0-2 for its fault hook), then the `piece`-consumers
`undo`/`indent` (parallel-safe with each other), `lineidx` (needs
piece fault surface + L0-5 work), `find` (needs piece + work;
its fault-poll switch needs the same piece surface as lineidx).

### L1-1 — Harvest `scan` verbatim

- **Scope:** `src/scan/{scan.h,scan.c}`, `tests/scan_test.c`,
  `fuzz/scan_fuzz.c`, `bench/scan_bench.c` (gate→tracked-number
  reconciliation §1.4), decision note (bench-gate change only).
- **Deps:** build skeleton only. Unblocks L1-3 (piece),
  L1-6 (lineidx).
- **Acceptance (additional):** differential + protected-page
  cases green under ASan (guard-page fixtures are the point —
  they must stay capable of *failing*: report shows the fixture
  mechanism, e.g. the mprotect layout, not just green);
  fuzz-smoke entry; bench prints + exit 0 with (M) stamp.

### L1-2 — Harvest `utf8` + ©/® `G_PICT` fix

- **Scope:** `src/utf8/{utf8.h,utf8.c,tables.h}`,
  `tests/utf8_test.c` (+ GraphemeBreakTest fixture),
  `tests/utf8_tables_test.c` (lane classification §2.4),
  `fuzz/utf8_fuzz.c`, `bench/utf8_bench.c`, decision note.
  Not: `tools/ucdgen.py` + UCD data (Wave 0 vendor/tools work;
  this bead records the byte-identical-regeneration obligation).
- **Deps:** build skeleton; fixture data from vendor checkout.
  Unblocks L4 layout/font (Wave 2+); nothing in Wave 1 blocks on
  it except completeness.
- **Acceptance (additional):** §2.5's three red tests
  (classifier audit over all of `UCD_EXT_PICT`, GB11 clusters,
  ©/®+VS16 width both-directions) red-then-green pasted;
  GraphemeBreakTest + every-code-point widths green;
  chunking-equivalence (step vs one-shot) green incl. the new
  cases chunked; `tables.h` untouched by the fix (diff proves
  generator output bit-identical to pin); report states which
  `utf8.c` consumers of `gcb_of` were audited for the fix's
  blast radius (`gstate_*`, `utf8_emoji_class`,
  `utf8_break_certain`, cseg width path).

### L1-3 — Harvest `piece` verbatim (tree, snapshots, checkpoint)

- **Scope:** `src/piece/{piece.h,piece.c,piece_test.h}`,
  `tests/{piece_test,piece_bounds_test,piece_mem_test,
  piece_mt_test}.c`, `tests/piece_model.h`,
  `fuzz/{piece_fuzz,piece_checkpoint_fuzz}.c`,
  `bench/piece_bench.c` (+ args), decision note (prepayment
  interface enumeration §4.2; `PIECE_TESTING` lane wiring §4.4).
  Explicitly **without** §4.5(a)-(d) fixes — those are L1-4 —
  except that this bead *lands the deciding probes* for
  (a)/(b) as tests marked expected-fail with the red outputs in
  its report (so L1-4 starts from evidence, Law 9 across the
  split; if the coordinator prefers one worker for piece
  whole, merge L1-3+L1-4 and keep the probe-first order inside).
- **Deps:** L0-1 (`os`), L0-3 (`base` headers/backing), L1-1
  (`scan`). Unblocks L1-4…L1-7, L2.
- **Acceptance (additional):** four suites green (san lane);
  `make check-tsan` green for `piece_mt_test` (annotations only
  per §4.4(iii), each rationalised); release `nm` shows no
  `piece_test_*`; Law-2 window test: typing-path ops
  (insert/delete/snapshot_take under a prepaid test allocator)
  count 0 mallocs with guard proven active (release build);
  G10f memory suite green with its bound arithmetic quoted from
  `piece.h`, not restated; (a)/(b) probe verdicts recorded
  (reproduces / does-not-reproduce at pin, with runs).

### L1-4 — `piece` targeted fixes (root promotion, staged cache, shared add-dir, fault hook)

- **Scope:** `src/piece/piece.c` (+ `piece.h` fault surface),
  the §4.5 tests in the piece suites/fuzzers, decision note
  (add-dir sharing design + ordering audit; fault API final
  names for lineidx/find/file consumers).
- **Deps:** L1-3; **L0-2** (fault lease final symbols).
  Unblocks L1-6/L1-7 fault switches, L2 `file`/`edit`.
- **Acceptance (additional):** per-fix red→green pasted (§4.5
  (a) collapse probe outcomes incl. snapshot-retained and
  checkpoint-abort variants; (b) staged-cache test; (c) OOM +
  concurrent-reader checkpoint abort test (Wave 1 gate
  wording) + take-cost O(1)-in-chunks counting-allocator
  evidence + bench row; (d) mapped-fault plumbing test via
  `os_fault_test_trigger`); full piece suites + fuzzers still
  green; diff manifest = (a)–(d) hunks only; TSan re-run green
  after add-dir conversion (this is the change most likely to
  introduce a race — the conversion's ordering argument is in
  the decision note, TSan `piece_mt_test` is its check).

### L1-5 — Harvest `undo` (+ `indent` verbatim, same worker-slot shape)

Two small verbatim modules; propose as **two beads** (Law 3 is
one bead per worker — bundling two modules into one bead is the
coordinator's call; listed separately so they can run in
parallel):

- **L1-5a `undo`. Scope:** `src/undo/{undo.h,undo.c}`,
  `tests/undo_test.c`, `fuzz/undo_fuzz.c`, `bench/undo_bench.c`
  (**rewritten** off `#include "undo.c"`, §5.4), decision note
  (FSM-reading §5.2, bench internal-access resolution).
  **Deps:** L1-4 (add-dir boundary final form; falls back to
  L1-3 if L1-4's probe verdicts make (c) a no-op — coordinator
  sequences after L1-4 report either way).
  **Acceptance:** suites/fuzz green against converted piece;
  §5.5's two named checkpoint-boundary cases present-or-added
  with citations; sliced-replay abort atomicity case (failure
  mid-group → byte-exact pre-group state + counts) present;
  bench links `libtext.a` only (lint proves it);
  `editor_undo_branch_test` gap named.
- **L1-5b `indent`. Scope:** `src/indent/{indent.h,indent.c}`,
  `tests/indent_test.c`, `fuzz/indent_fuzz.c`,
  `bench/indent_bench.c`.
  **Deps:** L1-3 only. **Acceptance:** verbatim suites green;
  `ERR_LIMIT` no-writes-on-limit case explicitly present
  (assert outputs untouched, not just the code); typing-bound
  windows over piece-boundary-straddling lines covered.

### L1-6 — Harvest `lineidx` + degraded mode/off-path regrowth + fault re-route

- **Scope:** `src/lineidx/{lineidx.h,lineidx.c}`,
  `tests/lineidx_test.c`, `tests/lineidx_os_test.c`
  (**rewritten** off `#include "lineidx.c"`, census §6.4),
  `fuzz/lineidx_fuzz.c` (+ degraded ops),
  `bench/lineidx_bench.c`, decision note (degraded FSM enum,
  revision-stamp ownership, `lineidx_edit_check` fate, swap
  discipline).
- **Deps:** L1-1, L1-4 (fault surface), L0-5 (`work` incl.
  foreground lane + `work_prioritize`), L0-1. Unblocks L4
  viewport/minimap, Wave 1 large-file gate behaviours.
- **Acceptance (additional):** §6.5 red (>cap edit vetoed at
  verbatim code, pasted) → green (edit applies; degraded
  queries inexact-flagged; bulk rebuild restores exact;
  revision-mismatch swap rejected — test forces a stale
  rebuild); growth-past-reserve explicitly (PLAN wording);
  malloc-guard 0 over foreground edit+query windows on release
  build; occupied-bulk bounded-navigation case (seek makes
  progress while bulk saturated); `file/file.h` gone (DAG lint
  is the proof); fuzz model-checks `exact` flags through
  degrade/rebuild cycles; bench (M) rows incl. regrowth case.

### L1-7 — Harvest `find` + unified visit/native fold & whole-word

- **Scope:** `src/find/{find.h,find.c,literal.c,literal.h,
  find_int.h,visit.h,options.c,options.h}` (final public/internal
  split per §7.5.1), `tests/{find_test,find_bestof_test,
  find_simd_filter_verify_test,find_timeout_test,
  find_bench_test}.c` (censused), `tests/find_p1r_test.c`
  (**rewritten** off both `.c` includes, census §7.6),
  `fuzz/find_fuzz.c` (+ fold/word dimensions),
  `bench/find_bench.c` (+ helpers/args, fold rows), decision
  note (header thaw, visit placement, options.c fate, fold
  linearity argument).
- **Deps:** L1-4 (fault poll for `poll_stop`; bead can land the
  engine before that switch only if `find_int.h` compiles
  without `file` — i.e. no: sequence after L1-4), L0-3 (fixed
  CPU detect), L0-5 (`work_ctx` control). Unblocks L4
  find_session/fif (Wave 3) and their deletion of
  `findui/literal.c`.
- **Acceptance (additional):** frozen-contract suites green
  (case-sensitive results bit-identical to v1 expectations —
  the thaw must not drift existing semantics; SIMD-vs-Two-Way
  differential green with fold on and off); fold/word oracle
  differential red→green (§7.5) incl. piece-boundary-straddling
  word cases and empty-needle/word edge cases; visit/stream/
  result API equivalence cases; fault-mid-search cancellation
  case (§7.5.5) with zero-suffix non-matching asserted;
  `find.h` amended sentences listed in the decision note
  (what "frozen" clauses changed and why); bench fold rows (M).

### Handed-off / tracked-elsewhere (report, don't fix)

- `editor_large_test.c:1119` occupied-bulk navigation and the
  full >32 MiB delete+undo *editor* scenario: L5/Wave 2
  integration; L1-6 provides the unit-level shadows.
- `findui/literal.c` deletion: L4 find_session bead (Wave 3),
  preconditioned on L1-7; until then v1 file is reference/oracle
  only.
- `piece` pool *sizing* (PLAN §4.1 retained-revisions ×
  1920 B/edit prepayment, per-buffer snapshot concurrency cap):
  L2/L5 tree-owner beads; L1-3 only enumerates request shapes.
- Unicode version bumps beyond 15.1.0 tables: own future bead
  with generator + full revalidation (§2.1); the ©/® fix's
  exhaustive-audit test (L1-2) is the tripwire that makes such
  a bump safe.

VERDICT: L1 is harvestable at pin 6fec2e8 with zero post-c95b6eb drift (§8), structured as two verbatim leaves (scan §1, indent §3), one located one-function bug (utf8 gcb_of's <0x300 shortcut orphaning ©/® from UCD_EXT_PICT, §2.5), the piece centre split into verbatim harvest then four probe-first fixes (root promotion, staged-init cache, shared add-dir with boundary privatization, L0 fault-token hook, §4.5), undo as preserve-and-prove fallout (§5.5), and two modules blocked on killing their upward file/file.h edge via that fault hook (lineidx §6 plus PLAN §4.3 degraded-mode/off-path regrowth replacing the lineidx.c:858 veto; find §7 plus visit promotion and native fold/whole-word absorbing findui/literal.c) — seven beads L1-1…L1-7 (L1-5 split a/b), gated on L0-1/L0-2/L0-3/L0-5 from the L0 dossier, with exactly three #include "*.c" rewrites (lineidx_os_test, find_p1r_test ×2 includes, undo_bench) and no Windows code or unlisted globals anywhere in the layer.
## Correction note (2026-11-23, coordinator, from docs/decisions/wave1-piece-pool.md)

- §0.2 and §4.2 use v1's 1,920 B/edit as a physical multiplier. It is a (G) allowance (amendment A4); size the pool
  per docs/decisions/wave1-piece-pool.md.
- §0.1 and §4.5(d) describe a proposed OS pin. The accepted fault-id design has no pins: readers hold a backing
  reference (amendment A5, docs/decisions/wave1-os-fault-lease.md).
- §4.5(c) places the add-dir walk "per snapshot retain"; at the pin it happens in `piece_checkpoint_begin`
  (`add_store_prefix`), see amendment A7.
- (2026-11-23) The piece and undo sections are superseded where docs/decisions/wave1-piece-pool.md (signed) says so:
  400 B single node class, 24/16 fanouts, extent-owned pool, synchronous admission, height check at 31, exact batch
  claim, span-sequence undo inverse (amendments A13, A14).
