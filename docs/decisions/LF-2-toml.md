# LF-2 · `config/toml` harvest (L4, early under A21)

Source pin `6fec2e8` (`/home/tobias/Projects/sublimite-v1-oracle`, read as
evidence only): `src/config/parser.c` (330 lines (M)), `src/config/config.h`
(parser halves of it), `src/config/private.h`, the parser cases of
`tests/config_test.c` (`parser_test`) and `fuzz/config_fuzz.c`. Bead scope
and qualification: [wave1-L2-L5-harvest.md](wave1-L2-L5-harvest.md) §8;
early start and the no-pool ruling: amendment A21 in
[wave0-plan-amendments.md](wave0-plan-amendments.md).

Landed: `src/config/toml.h`, `src/config/toml.c`, `tests/config_toml_test.c`,
`fuzz/config_toml_fuzz.c`, seeds under `fuzz/corpus/config_toml/`, and the
two sorted rows in `tests/lanes.mk` (`tests/config_toml_test.c := san`,
`fuzz/config_toml_fuzz.c := fuzz`), and one grant in
`tools/system-symbols.txt` for v1's libm import (`powl` config-only, since
libm may set the calling thread's errno). v1's three `strcpy` sites became
bounded `memcpy` of the already cap-checked length plus its NUL, re-checked
at the copy, so the tree gains no unbounded-copy import (owner send-back,
2026-11-24). `mk/layers.mk` already listed `config`
in `L4_MODS`. The rest of v1 config — settings glue (`settings.c`), theme,
runtime/reload, paths — is not in this bead; it lands with the settings bead
on cmd/layout.

## Does v1's parser allocate? No — so there is no arena parameter

A21's fork: "If v1's parser allocates nothing (it reports into caller
storage or through callbacks), it is harvested that way with no pool; if it
needs memory for the parsed result, the caller passes a base arena and the
parser owns nothing."

v1's parser allocates nothing (M, read of `parser.c` at the pin): all state
is one stack `struct parser` (`parser.c:7-19`) with fixed arrays, results go
through the `config_visit_fn` callback with strings borrowed from the stack
text buffer, and there is no malloc/arena/pool call anywhere in the file.
PLAN §2's "private pool" is `runtime.c`'s reload arena, not the parser's
(§8). So this harvest is A21's first branch: **no arena parameter, no pool,
no allocation**. What v1 sized by constants, the caller now sizes by
storage, because those constants derived from cmd's action count
(`CONFIG_ENTRY_CAP = KEYS_ACTION_COUNT + 10`, `config.h:14`), which does not
exist in v2 yet (Law 4; §8: "legitimate only if the parser takes its
capacities from the caller").

## The unit in one paragraph

`config_toml_parse(bytes, len, scratch, visit, ctx, error)` parses v1's TOML
subset and visits each assignment under its full dotted path.
**Invariant:** delivery is transactional against parser errors — the
document is validated whole before the first visit call, so on any syntax,
limit, value or argument error the visitor has not run and the caller's
output is byte-for-byte untouched. All storage is the caller's
`config_toml_scratch`: two name buffers, one decoded-text buffer, flat
seen/declared name tables, a scopes array, and a string-pointer array, each
sized by `config_toml_caps`. Borrowed paths/strings live only for the
callback. Includes: `config/toml.h`, `utf8/utf8.h`, libc
(`string`/`math`/`float`) — nothing above L1, no x11/plat/view/keys/work.

## Kept mechanisms, simplest alternatives, defeating counter-examples

1. **Two passes: validate, then deliver.** The identical deterministic
   parse runs once with the visitor suppressed and once with it enabled, so
   pass 2 can fail only through the visitor itself.
   - Simplest alternative: v1's single pass, visiting each assignment as it
     parses (`parser.c:316`). Rejected by the bead's contract: v1's own
     header admits "a failed parse may already have called the visitor"
     (`config.h:35-37`); transactional safety lived one layer up, in
     `config_parse` staging into a fresh `config_settings`
     (`settings.c:122-130`). That staging type is settings glue and does
     not exist here.
   - Defeating counter-example for one pass: `a = 1\nb = ` — assignment `a`
     is delivered, then the missing value fails; the caller's output has
     been touched by a failed parse. The unit test's `transactional()`
     pins this exact document.
   - Cost: the document is parsed twice. Config sources are bounded by the
     caller's `source_cap` and parsed off the typing path; no number is
     claimed for the cost (it is 2× (P) of a linear scan whose constant is
     unmeasured, and no gate rides on it).
   - Residual: a visitor returning nonzero stops delivery mid-pass-2 and
     its code becomes the result (v1 semantics, `parser.c:317`); earlier
     deliveries stand. A caller needing all-or-nothing against its own
     errors stages into fresh storage, exactly as v1's `config_parse` did.
     The unit test's `visit_errors()` pins code propagation.
2. **Caller-sized scratch instead of v1's fixed arrays.** `seen`/`declared`
   become flat `entry_cap × name_cap` char regions; `text`, `table`, `path`,
   `scopes`, `strings` likewise caller storage.
   - Simplest alternative: keep v1's constants with literal values
     (text 4096 (P: Linux PATH_MAX, `config.h:6-10`), name 29 (P:
     `sizeof("keymap.split_selection_lines")`), entries/bindings from the
     action count). Rejected: the action count is cmd's, undesigned in v2;
     inventing it now violates Law 4 (§8 says so in terms).
   - Defeating counter-example for fixed caps: the settings bead lands with
     more actions than any number chosen today, and a valid user config
     binding all of them fails with LIMIT in a unit that has no business
     knowing the count. With caller caps, the bead that owns the count
     sizes the scratch and carries the derivation.
   - `source_cap` replaces `CONFIG_SOURCE_CAP` (a derived product of the
     same constants, `config.h:16-20`) the same way: the caller states the
     largest document it will accept; the limit is checked before any byte
     is read (v1's order), so a fictitious over-long length is safe.
3. **First-error-wins with byte offsets, verbatim from v1** (`fail()`,
   `parser.c:20-24`). Simplest alternative — report the furthest or last
   error — would diverge from the differential oracle: v1's runtime shows
   the user this offset. The unit test pins four hand-traced offsets (bad
   byte index 3 (P: fixture index), NUL-in-comment index 1, empty segment
   index 2 in `a..b=1`, header junk index 4 in `[a] junk`, leading-zero
   check at index 6 in `a = 01 junk` — the last also proving a later fault
   never overwrites the first).
4. **Whole-document UTF-8/control pre-validation, verbatim** (`parser.c:
   283-290`), including inside comments, NUL never EOF. Alternative —
   validate only what the grammar touches — would let invalid bytes hide in
   comments and resurface as display text; v1's tests pin the rejection
   (`{'#', 0}` and `0xff` in a literal string).
5. **v1's numeric conversion, verbatim**: manual int64 accumulation with
   pre-multiply overflow checks and the long-double mantissa/exponent path
   (`parser.c:185-244`), no `strtod`/`strtoll`.
   - Simplest alternative: libc conversion. Rejected: locale dependence
     (v1's contract is "no locale", `config.h:37`), and `strtod` accepts
     forms this subset rejects (`nan`, hex floats — both in v1's bad list).
   - Rounding is v1's, knowingly (G: inherited behaviour, kept for the
     differential): accumulation in long double scaled by `powl(10, e)` is
     not correct rounding and can land one ulp from `strtod`. Measured
     example (M: this algorithm vs `strtod`, glibc `powl`, 2026-11-24):
     `0.002877` parses to `0x1.791819d2391d6p-9` here, one ulp above the
     correctly rounded value; `tests/config_toml_test.c` pins both the
     parsed value and its inequality with the literal. No consumer
     compares floats exactly — v1's float consumer is a magnitude (font
     points, `settings.c:63-67`) — so the ulp is cosmetic until a bead
     needs correct rounding and says so.
   - Only change: the exponent bound is `LDBL_MAX_10_EXP + token length`
     (long arithmetic) where v1 wrote `+ CONFIG_TEXT_CAP` (P: the cap was
     an upper bound of the token length; the actual length is the tighter
     form of v1's own derivation, "plus decoded token length",
     `parser.c:232-234`). The number token reuses the text scratch (a
     numeric value decodes no strings; v1 bounded the token by the same
     cap).
6. **Duplicate/prefix rules (`remember()`), verbatim**, including the
   dotted-key intermediate-table and later-parent-declaration cases
   (`parser.c:148-174`), exercised by v1's bad list (`a.b=1\n[a]`,
   `[a]\nb.c=1\n[a.b]`, `[a.b]\nx=1\n[a]\nb.c=2`). No simpler restatement
   was attempted: these eight-ish cases are exactly where a rewrite loses
   a v1 edge the differential may not name.

## Cut, and why

- **`config_toml_parse_worker` and its cooperative cancellation**
  (`parser.c:17-18, 27-36, 271-330`; `private.h`). It polled
  `work_should_stop` every `VIEW_SCAN_BOUND` (4096 (P: v1 `view.h:9`))
  bytes — a view-layer constant imported through `config.h`, the coupling
  this bead exists to sever, and `work` is outside this unit's allowed
  includes (base/utf8/os). Nothing in v2 calls a worker variant yet; the
  settings/runtime bead that first parses on a worker re-derives its own
  poll interval and adds the variant then. v1's `tests/config_worker_test.c`
  cancellation case is therefore **not** ported (reported, not silently
  dropped). `CONFIG_ERR_BUSY` goes with it.
- **`CONFIG_ERR_IO`, settings, theme, palette, runtime, paths, `actions.inc`**
  — settings glue (see above), not parser.
- **v1's `config_parse` staging wrapper** — superseded at this layer by
  two-pass delivery (mechanism 1); the settings bead may still stage
  against its own value errors.
- **Error-code surface**: `CONFIG_TOML_ERR_{SYNTAX,LIMIT,VALUE,ARG}` only.
  Argument violations return `ERR_ARG` rather than ASSERTing, preserving
  v1's error contract at this boundary (`parser.c:275`; pinned by v1's
  `config_parse(NULL, 1) == CONFIG_ERR_ARG` case). This is a deliberate,
  recorded exception-shape: the checks are O(1) (P: a fixed number of
  pointer/cap tests), and hostile-length arguments from file-reading
  callers are exactly what v1 handled by return code.

## Differential evidence

`tests/config_toml_test.c` ports every parsing case of v1's `parser_test`
with v1's expected values: the six-assignment good document (int64 extrema,
`1.2_5e+2 == 125.0`, snowman escape, literal backslash, arrays), the
42-entry (M: counted in the fixture) bad list, invalid UTF-8 and NUL cases,
NULL/empty arguments, and the exhaustive single-byte mutation sweep
(45-byte (M) seed × 256 octets (P: `UINT8_MAX + 1`)). Where v1 asserted
limits through its constants (text/name/entry/source caps), the port
asserts the same boundary shape — cap−1 fits, cap fails — through small
caller caps (8/4/16/2/64 (P: chosen so each boundary is a ±1 step from a
stated number)). New beyond v1: the strings-array cap boundary, four
error-offset pins, the transactional recorder (byte-identical output after
a valid-prefix failure), visitor-error propagation, and scratch/argument
validation. v1's non-parsing cases (settings values, keymap lookup,
runtime watch/reload, trailing-whitespace and menu defaults, palette) are
not parsing and were not ported.

`fuzz/config_toml_fuzz.c` ports `config_fuzz.c`'s parser half: result
equals `error.code`, offset within input, borrowed strings within caps —
plus the transactional invariant (visitor never fails ⇒ any error means
zero visits) under both v1-magnitude caps and the small boundary caps.

## Line accounting (M, `wc -l` at landing)

- v1 `parser.c`: 330. Harvested into `src/config/toml.c`: 369 total, of
  which the parse core (helpers through `value()`) tracks v1 line-for-line
  except name/cap substitutions; new writing is the scratch plumbing,
  the `parse_pass`/two-pass wrapper and argument validation (≈75 lines
  (E: counted by section, ±10), including the bounded-copy re-checks of
  the send-back round); cut from v1 ≈25 lines (E): cancellation
  poll, worker entry, `config.h`/`work` includes.
- `src/config/toml.h`: 115 (new; the type/error/visit declarations are the
  parser halves of v1 `config.h:21-42` re-cut onto caller caps).
- `tests/config_toml_test.c`: 344 (new; v1 `parser_test` is lines 35-99 of
  `config_test.c` (M), the rest is scratch wiring and the new pins above).
- `fuzz/config_toml_fuzz.c`: 81 (v1's is 34 (M), settings half removed,
  small-caps pass added).
