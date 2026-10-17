# edit-ptn: seeded repair and line metadata never count from byte 0 on lazy files

## Root cause (on this tree)

1. Every seeded cluster scan (restore endpoint repair `repair_begin`, typing
   repair, delete repair, bulk repair, Left/Backspace `S_PREV`, word-left at a
   line start) began at `near_seed(target)`, which proved a boundary only from
   an adjacent ASCII pair in the 512 bytes before the target and otherwise
   returned 0: the scan then segmented the whole prefix from byte 0. A CJK,
   emoji or other non-ASCII window has no ASCII pair, so one key on a 1 GiB
   lazy file scanned about 1 GiB cooperatively. The old probe (line_query
   bytes only) could not see it.
2. Home, End, word left/right (and the word deletes, which run those motions),
   click select-word and select-line ran `setup_command`, whose line metadata is
   `line_at(cursor)` / `line_start` / `line_end`: a line_query counting LFs from
   the nearest certified anchor, which is byte 0 while the index is incomplete.

## Decision

1. **Context-free boundary proof** (`utf8_break_certain`, new in `src/utf8`):
   1 iff UAX #29 breaks between units x and y for every left context of x. The
   only context-dependent rules are GB9b (x Prepend), GB9c (x Linker/Extend),
   GB11 (x ZWJ) and GB12/13 (x and y RI); with those excluded, plus CR LF,
   y Extend/ZWJ/SpacingMark and the Hangul GB6-8 pairs, the pair rule alone
   decides. Soundness is tested against `utf8_grapheme_next` over every left
   context of length 0..3 drawn from a 19-unit alphabet covering each class
   (`tests/utf8_test.c t_break_certain`). It subsumes the old ASCII-pair rule.
2. **near_seed** decodes the same 512-byte window (P: the existing window) plus
   up to 3 bytes past the target so the last unit decodes whole, from the first
   non-continuation byte (UTF-8 self-synchronizes, so the units equal the
   scanner's), and returns the nearest certain break before the target. When
   this call's charged budget cannot hold the window it returns MORE instead
   of 0; a fresh slice always holds it (P: 515 + 264 < VIEW_SCAN_BOUND 4096).
3. **Resumable seek phase** `P_SEEK` replaces every `scan_begin(near_seed(..))`
   site: EOF/BOF targets are their own seed (as repair_begin did); otherwise the
   window; otherwise walk back to the previous LF (edit-jf9's `relative_start`,
   a certified line start). Byte 0 is used only when it is the nearest proof.
4. **Byte-relative line metadata** (`P_REL_META`, `view_relative_command` now
   accepts Home, End, word left/right, select word, select line): line start by
   walking back to LF, line end / next line start by walking forward to LF,
   with the same CRLF and whole-line-extension rules as `line_end` and the
   select-line branch. The editor routes these (and the deletes' motions, and
   click/drag select word/line) relative under the existing `relative_repair`
   predicate (`!wrap && lazy && !lines_exact`); other buffers run the previous
   calls. No line query runs and no estimate enters the exact caches (jf9).
   The index builder and cache certification rules are unchanged.

## Probe

`view.line_scanned` keeps its contract (line_query bytes only; view_test and
view_fuzz assert zero on the relative paths). New `view.seek_scanned` counts
every other byte read to locate a boundary: near_seed windows, seeded
repair/seek cluster scans and byte-relative LF walks. editor_large_test's
`ptn_case` asserts their sum per key.

## Bound (per key, lazy unwrapped file before the index completes)

- line_query bytes: 0 on the relative paths.
- Seed: at most 515 bytes (P: window above) when a certain break lies in the
  512 bytes before the target, which holds for any text with an ASCII pair, LF,
  CJK, emoji or Hangul syllable pair there. Otherwise the walk costs the
  distance back to the previous LF, then the forward scan the same distance.
- Line metadata and following: the length of the cursor's line (walk back to
  LF, column scan), as edit-jf9/edit-dz0 record.
- **Single giant line (brief item 2), stated rather than seeded from a lineidx
  chunk:** following still walks the cursor's line. A 1 GiB line costs about
  2^30 / 3840 = 279,620 slices per walk (P: relative_start reads 256-byte
  quanta while scanned + 264 <= 4096, i.e. 15 reads = 3840 bytes per call),
  the brief's 2^30 / 4096 = 262,144 being the charged-work ceiling. The seed
  itself stays within the 515-byte window unless the line is made of
  context-dependent units (marks, ZWJ, RI runs). Seeding from the index's
  partial chunks would need the jf9 certification rules and is not done here.

## Measured (release, bulk lane held, `tests/editor_large_test.c ptn_case`)

See docs/worker-reports/edit-ptn-s8.md for the red and green prints. Red:
CJK key scanned 18,392,701 bytes before the 10 s settle watchdog fired
(EDITOR_IDLE_NS, (E) watchdog only); Home counted 960,241,920 bytes before
the watchdog. Green: 1540 and 512 bytes, settles of a few ms (M)[BAT0 "Not
charging", load1 about 56].

The long-top-line case (brief (b)) did not reproduce: the top's line-start
proof starts at `first_byte`, which is already a line start, so it reads one
256-byte quantum and near_seed keeps its budget (1538 bytes before the fix).
It stays as a regression case. The new MORE-instead-of-0 rule covers the
budget-exhaustion mechanism the brief described in any case.
