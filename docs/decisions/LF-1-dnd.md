# LF-1 · `dnd` harvest (L4, early under A21)

Source pin `6fec2e8` (`/home/tobias/Projects/sublimite-v1-oracle`, read as
evidence only): `src/dnd/dnd.{c,h}`, `tests/dnd_test.c`, `fuzz/dnd_fuzz.c`.
Bead scope and qualification: [wave1-L2-L5-harvest.md](wave1-L2-L5-harvest.md)
§8; early start: amendment A21 in
[wave0-plan-amendments.md](wave0-plan-amendments.md) (everything `dnd`
includes — `base`, `ipc`, `utf8` — has landed; no display code; no globals).

Landed: `src/dnd/dnd.h`, `src/dnd/dnd.c`, `tests/dnd_test.c`,
`fuzz/dnd_fuzz.c`, seeds under `fuzz/corpus/dnd/`, and the two sorted rows
in `tests/lanes.mk` (`tests/dnd_test.c := san`, `fuzz/dnd_fuzz.c := fuzz`).
`mk/layers.mk` already listed `dnd` in `L4_MODS`. The XDND protocol test
(`tests/x11_dnd_test.c` in v1) is display code and stays with the x11 bead,
as §8 says.

## The module in one paragraph

`dnd_parse_uri_list` turns one `text/uri-list` drop payload into an
`ipc_request` whose path strings are copied into the caller's `base_arena`.
**Invariant:** the result is all-or-nothing at the payload level and
best-effort at the entry level. A payload the editor must never partially
open (raw NUL anywhere, arena exhaustion part-way) returns an error with
both outputs cleared and the arena mark exactly where the caller left it;
a merely bad entry (foreign host, malformed escape, query, invalid UTF-8,
oversize, past the IPC path count) is skipped and counted, so the caller
can show how much of the drop it ignored. Kept paths are absolute, valid
UTF-8, strictly shorter than `IPC_PATH_CAP` (P: the cap in
`src/ipc/ipc.h` includes the trailing NUL), at most `IPC_MAX_PATHS` of
them (P: the same header's array bound), and they sit back-to-back in the
arena with no gaps — the fuzzer pins that byte layout. Nothing is
allocated except through `base_arena_alloc`, and there is no filesystem
access, no state and no init/fini.

## Kept mechanisms, and why simpler does not work

1. **Validate into a stack buffer, copy only kept entries.**
   Each entry is decoded, length-checked and UTF-8-checked in a stack
   `char path[IPC_PATH_CAP]`; only an entry that survives — and is not
   past the path count — is copied into the arena. Simplest alternative:
   decode straight into the arena and rewind on failure. Counter-example:
   a drop of valid entries past `IPC_MAX_PATHS` must be *counted as
   overflow*, which requires fully validating entries that will never be
   stored; decoding them into the arena first would need a per-entry
   mark/rewind dance and could exhaust a small arena on entries the
   caller asked only to count. The stack buffer makes "validate" and
   "store" separate decisions and keeps the arena gap-free by
   construction.

2. **Raw-NUL pre-scan of the whole payload.**
   One `memchr` for `'\0'` runs before any allocation; a hit is
   `DND_INVALID` for the entire payload. Simplest alternative: treat the
   line containing the NUL as one more skipped entry. Counter-example:
   `/good\n/bad\0tail` — a raw NUL cannot occur in `text/uri-list`, so
   its presence means the selection is not the text the caller thinks it
   is (wrong target type, binary data); opening the `/good` half would
   act on a payload whose second half was silently reinterpreted. Whole
   payload rejection is also what lets the error path promise "arena
   mark unchanged" without any rollback bookkeeping.

3. **Atomic rollback on arena exhaustion.**
   The entry mark is taken once; a failed `base_arena_alloc` resets to
   it, clears both outputs and returns `DND_NO_SPACE`. Simplest
   alternative: return the entries that fit. Counter-example: the caller
   sized its arena for the drop it saw; silently opening a prefix of the
   user's dragged files looks exactly like success. Exhaustion is the
   caller's sizing bug to surface, not data to truncate.

4. **Skipped/overflow counted to the end of the payload.**
   Parsing never stops early: `count + skipped` equals the number of
   non-empty, non-comment entries, and `overflow` counts only entries
   that were *valid* but past the bound (the fuzzer checks both).
   Simplest alternative: stop at the bound. Counter-example: the counts
   exist so the UI can say what it ignored; stopping early cannot
   distinguish "two extra files" from "two extra files and one foreign
   URI", and makes the count depend on entry order.

5. **Authority accepted by exact comparison, not parsed.**
   The authority between `file://` and the next `/` must be empty,
   `localhost`, or the caller's hostname, compared ASCII-case-
   insensitively over its exact length. There is no userinfo/port/UNC
   sub-parser. Simplest alternative: strip any authority and keep the
   path. Counter-example: `file://foreign/p` would open the local `/p` —
   a drop from a network-share view must not be reinterpreted as a local
   file. Exact-length comparison also rejects `thinkpad.evil`,
   `user@thinkpad` and `thinkpad:80` (P: a port digits fixture; local
   file authorities carry no port) for free, because none of them *is*
   the hostname.

6. **Percent-decoding for the URI form only; bare paths literal.**
   A line starting with `/` is copied byte-for-byte (so `%`, `?`, `#`
   survive); the `file:` form rejects raw `?`/`#` (query/fragment are
   not paths), malformed escapes and a decoded NUL, while accepting
   percent-encoded CR/LF, which is precisely how a filename containing
   those bytes travels in a URI. Raw CR is rejected in both forms: the
   only CR a line may carry is the CRLF terminator, stripped by the line
   splitter (and only when an LF actually follows — a trailing lone CR
   at end of payload is a raw CR inside the entry and is rejected).
   Simplest alternative: one decoding rule for both forms.
   Counter-example: a bare filename `/tmp/100% ready?#` (v1 test case,
   kept verbatim) would be mangled or rejected by URI rules, and a URI
   `file:///tmp/a%23b` would keep a literal `%23` under bare rules. The
   first byte already distinguishes the two producers, so the branch
   costs nothing.

7. **Strict UTF-8 validation after decoding.**
   The decoded bytes are walked with `utf8_decode` and any invalid unit
   rejects the entry. Simplest alternative: pass bytes through — Linux
   filenames are byte strings. Counter-example: every consumer of an
   `ipc_path` (tabs, titles, the wire format, session files) treats
   paths as UTF-8 text; `file:///tmp/%FF` admitted here becomes mojibake
   or a rejected request three modules later, with no count at the place
   that could have explained it. Validation is also what rejects the
   overlong and surrogate escapes (`%C0%AF`, `%ED%A0%80`) that a
   byte-wise decoder would otherwise smuggle in.

8. **Coordinates are the IPC origin, other request fields cleared.**
   Each kept path gets line/col one-based origin (P: IPC coordinates are
   one-based, `src/ipc/ipc.h`), and `cwd`, stdin, wait/new-instance and
   the activation strings are zero. Simplest alternative: leave the
   coordinates zero for "no position". Counter-example:
   `ipc_wire_encode` ASSERTs every path's line and col are nonzero
   (`src/ipc/wire.c`), so a zero would turn the first forwarded drop
   into an abort in a different module. "No position" and "top of file"
   are the same user-visible place; the origin is the honest encoding.

## Cut, and why

- **All `_WIN32` code** (drive-letter URIs, UNC authorities,
  backslash conversion; the two-pass authority decode that existed only
  to serve UNC). v2 is Linux-first with Windows second-class and no
  Windows build in the MVP (CLAUDE.md Law 5); v2 `ipc` already dropped
  the Windows endpoint the same way. Dead code that no lane compiles is
  a fossil (anti-decay rules), and on Linux the behaviour is unchanged:
  a foreign authority is rejected, exactly as v1's POSIX branch did. If
  a Windows port ever happens, v1 at the pin is the evidence to re-derive
  it from.
- **Bad-argument returns became ASSERTs (A15).** v1 returned its
  `invalid` code for NULL `arena`/`out`/`counts`, an uninitialized arena
  and NULL `data` with nonzero size. No drag payload can produce any of
  those — only a caller bug can — so v2 states them as preconditions and
  checks them with the tree's one `ASSERT` from `os.h`, the same shape
  `ipc_wire_decode` already uses (`src/ipc/wire.c`). This is also why the
  initialized-arena check is up front rather than left to
  `base_arena_alloc`'s own ASSERT: an empty or all-invalid payload never
  allocates, so without the up-front check a call on a zeroed arena
  would silently succeed and the caller would believe its (empty) paths
  borrow arena storage. What remains a return code is exactly what the
  outside world can cause: `DND_INVALID` for a raw NUL in the payload,
  `DND_NO_SPACE` for arena exhaustion. The four v1 test expectations of
  `invalid` for bad arguments became five `assert_death` cases in
  `tests/dnd_test.c` (the uninitialized arena is the fifth (P: four
  argument cases from v1 plus the arena-state case v1 folded into the
  same branch)); every v1 *parsing* expectation is unchanged.
- **Names adapted to v2, nothing else.** `edit_arena*` →
  `base_arena*`, `edit_malloc_guard_*` → `base_malloc_guard_*` (the v2
  `src/base/base.h` spellings), result values `dnd_ok`/`dnd_invalid`/
  `dnd_no_space` → `DND_OK`/`DND_INVALID`/`DND_NO_SPACE` (the uppercase
  value convention of the sibling headers, e.g. `ipc_result`,
  `find_code`), header guard `DND_DND_H`. The exported symbol keeps its
  name: `dnd_parse_uri_list`.
- **`<assert.h>` in the fuzzer.** v1's fuzzer used libc `assert`; v2 has
  one ASSERT for the tree and its fuzzers trap with a diagnostic
  (the `CHECK` shape of `fuzz/indent_fuzz.c`). The fuzzer's checks are
  otherwise v1's, plus the two activation fields v2's `ipc_request`
  added, which `paths_equal`/`cleared` now pin to NULL.
- **No bench.** v1 shipped none for `dnd` and the bead's scope lists
  none; Law 1 wants the test and (it parses) the fuzzer, both present.
  Drop parsing is bounded by the IPC caps above and is not on the
  typing path, though it is Law-2-shaped anyway (arena only; the test
  holds a malloc-guard window over its decoding case).

## Line accounting

Method (M, counted 2026-11-23): `wc -l` for the v1 and v2 totals; for
"dropped before diff", the `_WIN32`-only ranges of v1 `dnd.c` identified
by reading its preprocessor blocks; for "harvested", lines still equal
after deleting those ranges and applying only the mechanical renames
listed above (`edit_arena`→`base_arena`, `edit_malloc_guard`→
`base_malloc_guard`, the three result values, `assert(`→`CHECK(` in the
fuzzer), counted with a line diff. "Written/changed" is the v2 side of
that diff minus the harvested lines (P: subtraction of the measured
counts beside it). The scratch counting script and diff live under
`build/` of the worker checkout, not in the tree.

| File | v1 lines (M) | dropped before diff (M) | v2 lines (M) | harvested (M) | written/changed (P) |
|---|---|---|---|---|---|
| `src/dnd/dnd.c` | 162 | 45 (`_WIN32` only) | 117 | 107 | 10 |
| `src/dnd/dnd.h` | 42 | 0 (P: it had no platform code) | 46 | 32 | 14 |
| `tests/dnd_test.c` | 241 | 0 | 294 | 236 | 58 |
| `fuzz/dnd_fuzz.c` | 137 | 0 | 142 | 135 | 7 |

So of the v1 module proper, the cut is the forty-five `_WIN32` lines
(M, above); everything else that changed in `dnd.c`/`dnd.h` is the
argument check becoming ASSERTs, the `os/os.h` include they need, and
comment rewording around them. The new writing is
mostly test-side: the five death cases and their fork plumbing use the
shared `tests/assert_death.h`, and the guard/assertion lines that pin
v2's two extra `ipc_request` fields. Every v1 parsing test case —
lists, hosts, decoding, invalid entries, the entry-count and
path-length limits including both boundary sizes, storage atomicity,
copy ownership and the raw-NUL payload — is present with its v1 expected
output intact; the only dropped test lines are the four bad-argument
`invalid` expectations and the arena-mark check that only those calls
needed (M: the five (P) v1-side dropped test lines of the diff above,
four calls plus their trailing check).

## Verification (2026-11-23, [AC] Charging, load1 4.31 before `check` (M))

Red first, per Law 9: with header, test and lane rows in place and
`dnd.c` a stub carrying only the preconditions and cleared outputs, the
five death cases passed and the suite stopped at its first parsing case
— `dnd_test:43: FAIL dnd_parse_uri_list(BYTES(crlf), ...) == DND_OK`,
exit one (M: process exit statuses and suite output of the runs named
here). Green: the same binary with the harvested implementation prints
`dnd_test: all parser cases passed`, exit zero, in both the sanitizer
build and the release build; the release run is the one where the
decoding case's malloc-guard window is live (`base_malloc_guard` is
inert under the sanitizers by design, `src/base/base.h`), and it counts
zero allocations (M).

Acceptance commands, each preceded by `date`, all exit zero (M):

- `HOME=$PWD/build/home CONTRACT_CHECK_DISPLAYS=:170-:179 nice make -j2 check`
  — includes `check-lints` (layer DAG, symbol DAG, isolation, layout,
  no-`.c`-includes), the isolated layer links (`dnd.o` whole-archive in
  L4 against lower layers only), `check-san` with `dnd_test` among its
  twenty-seven binaries (M: the lane's own summary line), `check-law2`
  and the contract harness.
- `nice make -j2 check-tsan` — nine threaded suites (M: its summary
  line); `dnd` itself is single-threaded and stateless, in the `san`
  lane only, as the brief's lane rows specify.
- `nice make -j2 fuzz-smoke` — eleven fuzzers replay their seeds (M:
  its summary line); `dnd_fuzz` replays the eight seed files under
  `fuzz/corpus/dnd/` (the zero-byte seed included (P: the file count on
  disk); libFuzzer reports the seven non-empty ones as its corpus
  (M: its seed-corpus line)).

Beyond the acceptance set, one scratch fuzz run in a throwaway corpus
copy under `build/` ran two hundred thousand generated inputs with no
finding (M: libFuzzer's final run count; an exploratory local count, not
a budget — the committed gate remains seed replay at `-runs=0`, which
invents no iteration number, per the Makefile's own comment).
