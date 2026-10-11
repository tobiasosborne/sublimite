# edit-d50 continuation report

Worktree only. No `git`, `bd`, subagents, main-tree writes, X connection, or
Sublime process was used. The coordinator owns display/IPC verification and
the Sublime comparison drive.

The requested daily-use commands now have executors for the current
single-selection model. The earlier worker already implemented indent/unindent,
duplicate/delete/move/join lines, upper/lower case, transpose and the mark,
kill/yank and case Ctrl+K chords; those fixtures passed before this continuation.
This continuation adds toggle comment, go to line, Ctrl+D's first word selection,
Ctrl+K Ctrl+C centering and Ctrl+K Ctrl+D single-selection occurrence skipping.
**The broader bead remains incomplete for full Sublime parity:** syntax scopes,
multiple selections and oversized edit continuations remain prerequisites.

## Changes in this continuation

- `src/view/edits.c`, `src/view/view.h`: language comment tokens, common-indent
  line comments, spaced/compact removal, block wrapping/removal, word selection
  and bounded single-selection literal occurrence skipping. All preparation is
  immutable and allocation-free. Source/output bounds reuse `VIEW_WINDOW`.
- `src/editor/input.c`: comment/word/skip dispatch through the existing plan,
  history and retained-notice path; hard-line and visual-row centering; an
  editable go-to-line prompt with live preview, Enter acceptance, Escape
  restoration, overflow handling and soft selection history.
- `src/editor/private.h`, `src/editor/editor.c`, `src/editor/paint.c`,
  `src/editor/buffers.c`: prepaid prompt state, index-seek continuations, prompt
  input routing, status-row rendering and cleanup on activation/source change.
  Prompt text never enters the piece tree. Seeks use `lineidx_seek_line` and
  continue between bounded editor turns; approximate offsets are never used.
- `src/keys/keys.c`, `src/keys/keys.h`: cleared the newly executable bindings'
  unconditional flags. `KEYS_ARG_SINGLE_SELECTION` explicitly identifies the
  supported subset of Ctrl+D and Ctrl+K Ctrl+D; its bit derives from the next
  free position above `KEYS_ARG_SINGLE_LINE`.
- `tests/editor_test.c`, `tests/keys_test.c`: independent action/capability
  fixtures, forward/backward selections, undo/redo, wrapped centering, language
  comments, cold-index go-to-line, prompt editing and guarded allocation checks.
  The old panel-notice fixture no longer treats Ctrl+G as unavailable.
- `fuzz/view_fuzz.c`: immutable/capacity/selection probes for the new preparations.
- `src/{keys,view,editor}/STATUS.md`, `docs/decisions/edit-d50.md`, this report,
  `HANDOFF.md`, `docs/worklog/2026-10-11.md`: capability and handoff documentation.

No typing-path malloc was added. Edit commands reuse the editor's existing
arena-backed staging/undo storage and bounded stack windows. Prompt storage is
embedded before input; its decimal capacity derives conservatively from the
bit width of `uint64_t`, plus the NUL terminator. A decimal digit needs at least
one bit, so this bound covers every representable decimal line number.

## Red and green, by requested action

Already implemented actions retain their genuine earlier red runs below; their
passing baseline was not artificially broken. Every new executor was preceded
by an executed failing editor test and independently failing key-flag tests.
Exact diagnostic numbers are compiler source locations/action enum identifiers,
not budgets. Fixture offsets are (G), derived from literal prefix lengths;
comment tokens and default tab width are (G), transcribed from the installed
Sublime package's `comment.py` and language `Comments.tmPreferences`.

| Action | Red evidence | Green evidence |
|---|---|---|
| Indent/unindent | Previous worker's `default editing: indent selected lines`, `unindent selected lines`, `shift tab`: FAIL below | `san--default-edits.log`, `rel--default-edits.log`: PASS |
| Duplicate line | Previous worker's duplicate line/backward-selection FAIL below | Same default-edits logs: PASS |
| Delete line | Previous worker's delete line/selected-lines FAIL below | Same default-edits logs: PASS |
| Move line up/down | Previous worker's up/down/final-line FAIL below | Same default-edits logs: PASS |
| Join lines | Previous worker's join FAIL below | Same default-edits logs: PASS |
| Toggle comment | `edit-d50-comments-red.log`: every initial fixture FAIL; `edit-d50-comments-keys-red.log`: ctrl+slash flag mismatch | `san--default-comments.log`, `rel--default-comments.log`: PASS |
| Upper/lower case | Previous worker's uppercase selection/lowercase word FAIL below | Default-edits and default-repeat logs: PASS |
| Transpose | Previous worker's transpose FAIL below | Default-edits logs: PASS |
| Go to line | `edit-d50-goto-red.log`: prompt glyph assertion fails; `edit-d50-goto-keys-red.log`: ctrl+g flag mismatch | Default-goto and default-goto-cold logs, both configurations: PASS |
| Ctrl+D first word | `edit-d50-word-red.log`: alpha, identifier, Unicode and EOF cases FAIL; key flag mismatch in `edit-d50-word-keys-red.log` | Default-word logs, both configurations: PASS |
| Ctrl+K daily text chords | Previous worker's case/kill/mark red runs below; new center hard/wrapped rows FAIL in `edit-d50-center-red.log`; new skip next-selection FAIL in `edit-d50-skip-red.log`; key mismatch in `edit-d50-chords-keys-red.log` | Default-center, default-skip, default-states, default-soft-lines and default-repeat logs: PASS |

All filenames in the table refer to `build/edit-d50-continuation/`. Red commands:

```sh
make build/san/tests/editor_test build/san/tests/keys_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --default-comments
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --default-goto
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --default-word
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --default-center
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --default-skip
ASAN_OPTIONS=detect_leaks=0 build/san/tests/keys_test
```

Each red command failed before its corresponding implementation. Representative
output (full logs retained in the directory above):

```text
default comment: comment caret: FAIL
default comment: uncomment caret: FAIL
default comment: block selection: FAIL
default comment: unblock inner caret: FAIL
default word: alpha beta: FAIL
default word: fooBar_baz: FAIL
default word: naïve café: FAIL
default word: abc: FAIL
default center: hard lines: FAIL
default center: wrapped rows: FAIL
```

Additional genuine red/green fixes: `edit-d50-comment-edge-red.log` failed CSS
and HTML caret placement and HTML shared-space removal. Wrapping a whole line
now preserves the original inner caret, while an empty block insertion leaves
the caret between tokens. Overlapping spaced HTML tokens use the stripped
variant. The quoted C delimiter and all-blank line fixtures passed too. The
mixed-tab fixture initially used inferred indentation width; it was corrected
to pin the installed default tab width. That fixture correction is not claimed
as an implementation bug. `edit-d50-goto-old-panel-red.log` shows the stale
Ctrl+G unavailable-action expectation before updating that fixture.

Green commands repeat the same editor entry points in sanitizer and release
builds and run both `keys_test` and `view_test`. `focused-results.json` records
all outcomes. Each editing fixture checks exact text and selection, one grouped
undo/redo command (G: the shortcut contract), and guarded allocations. Selection
commands create soft history without text revision or text undo entries.

## Validation

- `make all`: PASS. The GCC build exposed potentially uninitialized comment
  result fields; they were initialized and the entire release build passed.
- Focused release/sanitizer checks: **74 passed, none failed** (M)[AC];
  derivation: count all records in `focused-results.json`, partition by return
  code. Each configuration has 37 records, including action entry points,
  keys/view suites and the null allocation fixture.
- Headless sanitizer sweep: **48 passed, none failed** (M)[AC];
  derivation: count and partition `headless-results.json` by process return
  code. Selection is recorded in `headless-selected.json`; display/IPC binaries
  and the full native GL suite were excluded. Display-named pure fixtures
  (`scroll_display_test`, alignment/input/guard/XI2) use headless mocks.
- Standalone sanitizer `ipc_test`: fails at Unix socket initialization with
  `errno=EPERM` (`Operation not permitted`), confirming the earlier sandbox
  finding. Exact output is in `ipc-test.log`. This is a separate environment
  failure; the socket-free sweep above does not include IPC. No bypass was used.
- Release allocation fixture:

```text
editor_test: null 10000 keys mallocs=0 guard=active
```

The event count is (G), inherited unchanged from `allocation_test()`; measured
allocations/guard state are (M)[AC], from the fixture's guard callbacks. Every
new command fixture also asserts zero allocations with the release guard
active. ASan's malloc interception is inert in this existing harness.

- View fuzzer: PASS, corpus replay only with ASan/UBSan. The new probe covers
  source immutability, output canaries and resulting-selection bounds.
  `-runs=0` requests replay, not a random duration campaign. Seed derivation:
  each literal fixture in `build/edit-d50-continuation/make-seeds.py` is multiplied by the full
  `uint8_t` choice space (`2**8`); libFuzzer additionally visits its empty input.
  Full result is in `view-fuzz-replay.log`.
- Keys and view benches: PASS, TRACK mode. `keys-bench.log` and `view-bench.log`
  include power/load stamps and measured rows. Keys uses its unchanged default
  event count; view uses the minimum accepted sample argument, derived from
  `bench/view_bench.c`'s argument check. Percentiles derive from those samples;
  no new timing gate or budget was introduced. Editor's full benchmark requires
  display initialization, so it was left to the coordinator.

Power at the functional batches was `BAT0=Not charging`, recorded before launch
in `headless-stamp.json` and `focused-stamp.json`; `[AC]` follows the existing
benchmark harness's classification of that status. No timing conclusion is
claimed from concurrent shared-machine test load. LeakSanitizer stays disabled
in this sandbox; the coordinator must repeat with leaks enabled.

## Still unsupported and why

`remaining-flags.json` records every unconditional `UNIMPLEMENTED` binding from
`src/keys/keys.c`, with its exact action and shortcut. For daily Ctrl+K use:

- Ctrl+K Ctrl+] reindent: needs syntax indentation rules and scope context;
  ordinary indent/unindent remains available.
- Ctrl+K Ctrl+V paste history: needs retained clipboard-history selection UI;
  ordinary paste and the implemented kill/yank top remain available.
- Folding/unfolding/tag-attribute chords: need fold ranges and visibility state.
- Inline-diff/revert chords: need the saved-baseline hunk/change model.
- Sidebar/pane/group chords: need their window/pane UI.

Ctrl+D first-word selection and Ctrl+K Ctrl+D single-selection replacement are
explicit subsets, not a claim of multiple-caret parity. Additional Ctrl+D
expansion preserves the current text/selection/history and reports unavailable.
The current view and undo payload have one cursor/anchor pair.

Toggle comment supports the filename languages enumerated in `comments_for()`.
Unknown/plain-text files have no comment token and are a no-op, as the installed
script is without syntax comment preferences. Block detection is local and
lexical, skips ordinary quoted strings/line comments and recognizes spaced and
stripped tokens. Embedded/custom syntax preferences, cross-window block scopes,
raw-string/nested-language distinctions and native selection affinity still
need syntax integration and the coordinator's comparison. These are material
limits, not complete parser-backed Sublime parity.

Go to line accepts unsigned decimal line numbers, previews exact positions,
clamps beyond the last line and accepts/cancels independently of text undo.
Relative line/column and other Goto overlay modes are outside this executor.
Center refuses incomplete/approximate row/index answers. Comment/word/skip and
other edit plans retain the existing `VIEW_WINDOW`/`VIEW_SCAN_BOUND` refusal
limits; skip currently searches a document fitting that prepaid window. Large
edit/occurrence continuations remain unfinished and refuse without mutation.

The earlier audit still applies to remaining paragraph wrapping, previous-line
selection, syntax/smart selection, snippets/tags, multiple-selection operations,
macros, bookmarks, search UI and the other retained window/project/build/spelling
commands. No unrelated foundations were built in this continuation.

## Outside-scope findings / environment

- Build artifacts and sources had future modification timestamps after the
  previously documented clock change. The initial new-test command executed a
  stale binary. A clean build and normalization of future source mtimes fixed
  build reproducibility; only subsequent genuine red runs are claimed. The old
  build directory was removed, so historical pasted evidence below remains,
  while its old build-log paths no longer exist in this worktree.
- Standalone headless IPC still receives EPERM on Unix socket initialization;
  this was exercised and reported, not fixed. The prior worker's display/IPC/socket restrictions and benchmark notes are
  preserved below. This continuation did not run those display suites or try
  to bypass sandbox sockets. The coordinator must run them and the native
  Sublime comparison on the resulting build.

---

## Previous worker report (historical evidence)

### edit-d50 worker report

Label: **src:tobias**. Worktree only; no `bd`, `git`, subagents or main-tree writes.

**Status: incomplete; do not close this bead.** Implemented the single-selection
fast paths below, but unfinished text commands and large-command continuations
remain. The [decision/audit](../decisions/edit-d50.md) lists every originally
flagged Linux binding, its installed Sublime command/arguments, final capability
flag, semantics and limits. The coordinator’s Sublime comparison is still required.

## Changed files

- `src/keys/keys.c`, `src/keys/keys.h`: capability flags and their meaning; independently updated `tests/keys_test.c` expectations.
- `src/view/view.h`, new `src/view/edits.c`: pure, bounded preparation functions for duplicate/delete/move/join lines; before/after line insertion; soft/hard boundary deletion; indent/unindent; scalar transpose; Unicode case/word/subword behavior; stable line sort; bracket selection/navigation; mark/yank/overwrite support.
- New `src/view/case_tables.h`, new `tools/view_case_tables.py`: reproducible Unicode mappings, case context and word properties; no locale/global mutable state.
- `src/editor/input.c`, `src/editor/private.h`: dispatch, explicit undo/journal replacement groups, normalized plan selections, prepaid soft history, repeat records, kill top, mark/overwrite state, generic retained notices and trusted-history-anchor following.
- `src/editor/buffers.c`, `src/editor/recovery.c`: prepaid soft history and source-replacement cleanup of transient state.
- `src/editor/clip.c`: current-line copy/cut for an empty selection, keeping the original caret in undo.
- `tests/editor_test.c`: exact byte/cursor/anchor outcomes, forward/backward selections, wrapped/unwrapped edit cases, grouped undo/redo, no-ops, Unicode/CRLF, mixed indentation, soft-history eviction, far-anchor continuation avoidance, repeat ingress/frame behavior, capability notices and allocator coverage.
- `fuzz/view_fuzz.c`: plan bounds, output canary, source immutability and resulting-selection invariants.
- `src/editor/STATUS.md`, `src/view/STATUS.md`, `src/keys/STATUS.md`, `HANDOFF.md`, `docs/worklog/2026-10-12.md`, this report and the decision: current scope, limitations and handoff.

Storage is reserved before input. New edit plans produce a contiguous replacement
and selection, applied as an explicit undo group. Related delete/insert operations
are one user undo step (G: the requested command contract). Existing typing bursts
remain supported. Completed history anchors are followed without a needless
normalization continuation; newly transformed selections repair both endpoints.

## Red run

Written and run before implementing these edit actions:

```sh
make build/san/tests/editor_test
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/editor_test --default-edits
```

Observed failure, (M)[AC], BAT0=Charging; load1=35.50 from `/proc/loadavg` before
launch. Numeric diagnostics are source locations from `__LINE__` or action/flag values derived from the `keys_action` enum and `KEYS_ARG_*` definitions, not invented timing budgets. Test byte offsets are (G), derived from literal prefix
lengths in `tests/editor_test.c`.

```text
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:75: FAIL memcmp(bytes, text, n) == 0
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:75: FAIL memcmp(bytes, text, n) == 0
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:75: FAIL memcmp(bytes, text, n) == 0
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:75: FAIL memcmp(bytes, text, n) == 0
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:75: FAIL memcmp(bytes, text, n) == 0
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:75: FAIL memcmp(bytes, text, n) == 0
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:75: FAIL memcmp(bytes, text, n) == 0
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
editor_test:75: FAIL memcmp(bytes, text, n) == 0
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
default editing: duplicate line: FAIL
default editing: duplicate backward selection: FAIL
default editing: delete line: FAIL
default editing: delete selected lines: FAIL
default editing: move line up: FAIL
default editing: move line down: FAIL
default editing: move final line up: FAIL
default editing: join lines: FAIL
default editing: insert line after: FAIL
default editing: insert line before: FAIL
default editing: delete hard BOL: FAIL
default editing: delete hard EOL: FAIL
default editing: delete EOL chord: FAIL
default editing: indent selected lines: FAIL
default editing: unindent selected lines: FAIL
default editing: shift tab: FAIL
default editing: transpose: FAIL
default editing: uppercase selection: FAIL
default editing: lowercase word: FAIL
default editing: sort insensitive: FAIL
default editing: sort sensitive: FAIL
```

## Green run

After implementing and refining the same actions:

```sh
ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/editor_test --default-edits
```

(M)[bat], BAT0=Discharging; the final focused batch started at load1=2.35 from
`/proc/loadavg`. Each action fixture also runs using the existing wrapped and
unwrapped configuration encodings, and checks undo, redo, exact selection and
zero guarded allocations. Release runs enforce the allocator guard; sanitizer
malloc interception is inert, as reported by the existing harness.

```text
default editing: duplicate line: PASS
default editing: duplicate backward selection: PASS
default editing: delete line: PASS
default editing: delete selected lines: PASS
default editing: move line up: PASS
default editing: move line down: PASS
default editing: move final line up: PASS
default editing: join lines: PASS
default editing: join selection whitespace endpoint: PASS
default editing: insert line after: PASS
default editing: insert line before: PASS
default editing: delete hard BOL: PASS
default editing: delete hard EOL: PASS
default editing: delete EOL chord: PASS
default editing: indent selected lines: PASS
default editing: unindent selected lines: PASS
default editing: shift tab: PASS
default editing: unindent mixed space/tab: PASS
default editing: transpose: PASS
default editing: transpose newline: PASS
default editing: uppercase selection: PASS
default editing: lowercase word: PASS
default editing: uppercase Unicode word boundary: PASS
default editing: sort insensitive: PASS
default editing: sort sensitive: PASS
default editing: uppercase expansion: PASS
default editing: lowercase final sigma: PASS
default editing: duplicate CRLF EOF: PASS
default editing: insert CRLF EOF: PASS
```

## Additional red/green evidence

Each additional behavior was tested before its implementation/fix. Diagnostic
locations derive from the compiled test source. These are functional outcomes;
no duration is claimed. The mark test initially needed another Right after
collapsing its selection; the chord-repeat test needed the expected backward
selection. The soft-row fixture was calibrated against the renderer’s existing
gutter, break-space and row-end geometry. Those fixture corrections are not
claimed as editor fixes.

`state-red.log` (red):

```text
editor_test:75: FAIL editor_view(e).selection.cursor == cursor
editor_test:373: FAIL expect(e, text, sizeof "foo" - 1u) == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:385: FAIL expect(e, "xooBar_baz (abc)\ndef", sizeof "x" - 1u) == 0
editor_test:398: FAIL editor_view(e).selection.anchor == 0
editor_test:74: FAIL editor_length(e) == n
editor_test:417: FAIL expect(e, "ooBar_baz (abc)\ndef", 0) == 0
editor_test:75: FAIL editor_view(e).selection.cursor == cursor
editor_test:429: FAIL expect(e, text, sizeof "fooBar_baz (abc)" - 1u) == 0
editor_test:75: FAIL editor_view(e).selection.cursor == cursor
editor_test:433: FAIL expect(e, text, sizeof "fooBar_baz (abc" - 1u) == 0
default state: subword selection: FAIL
default state: overwrite: FAIL
default state: set/select/swap/clear mark: FAIL
default state: delete mark/yank: FAIL
default state: goto bracket: FAIL
default state: select brackets: FAIL
```

Final green is captured in `build/edit-d50/san--default-states.log` and the final focused batch.

`edge-red.log` (red):

```text
default editing: lowercase final sigma: FAIL
default editing: duplicate CRLF EOF: FAIL
default editing: insert CRLF EOF: FAIL
editor_test:75: FAIL memcmp(bytes, text, n) == 0
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
```

Final green is captured in `build/edit-d50/edge-green.log` and the final focused batch.

`soft-red.log` (red):

```text
editor_test:75: FAIL editor_view(e).selection.cursor == cursor
editor_test:451: FAIL expect(e, "abc", 0) == 0
```

Final green is captured in `build/edit-d50/san--default-soft.log` and the final focused batch.

`noop-red.log` (red):

```text
editor_test:450: FAIL e->buffer->revision == revision
```

Final green is captured in `build/edit-d50/san--default-noop.log` and the final focused batch.

`line-copy-red.log` (red):

```text
editor_test:74: FAIL editor_length(e) == n
editor_test:457: FAIL expect(e, "alpha\nbetaalpha\n", sizeof "alpha\nbetaalpha\n" - 1u) == 0
```

Final green is captured in `build/edit-d50/line-copy-green.log` and the final focused batch.

`repeat-red.log` (red):

```text
editor_test:74: FAIL editor_length(e) == n
editor_test:617: FAIL expect(e, "a\nbetaa\nbeta", 2u * (sizeof text - 1u)) == 0
```

Final green is captured in `build/edit-d50/repeat-green.log` and the final focused batch.

`chord-repeat-red.log` (red):

```text
editor_test:75: FAIL memcmp(bytes, text, n) == 0
editor_test:459: FAIL expect(e, "ABC\nDEF", sizeof "ABC\nDEF" - 1u) == 0
```

Final green is captured in `build/edit-d50/rel--default-repeat.log` and the final focused batch.

`transpose-red.log` (red):

```text
default editing: transpose newline: FAIL
editor_test:75: FAIL memcmp(bytes, text, n) == 0
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
```

Final green is captured in `build/edit-d50/green.log` and the final focused batch.

`selection-red.log` (red):

```text
default editing: join selection whitespace endpoint: FAIL
default editing: uppercase Unicode word boundary: FAIL
editor_test:75: FAIL editor_view(e).selection.cursor == cursor
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
```

Final green is captured in `build/edit-d50/green.log` and the final focused batch.

`anchor-red.log` (red):

```text
editor_test:465: FAIL e->action == EDITOR_ACTION_NONE
```

Final green is captured in `build/edit-d50/san--default-history-anchor.log` and the final focused batch.

`soft-capacity-red.log` (red):

```text
editor_test:74: FAIL editor_length(e) == n
editor_test:461: FAIL expect(e, "xabc", sizeof "x" - 1u) == 0
```

Final green is captured in `build/edit-d50/san--default-soft-capacity.log` and the final focused batch.

`mixed-indent-red.log` (red):

```text
default editing: unindent mixed space/tab: FAIL
editor_test:74: FAIL editor_length(e) == n
editor_test:311: FAIL expect(e, test->after, strlen(test->after_cursor_prefix)) == 0
```

Final green is captured in `build/edit-d50/green.log` and the final focused batch.

Additional final PASS output:

```text
default state: subword selection: PASS
default state: overwrite: PASS
default state: set/select/swap/clear mark: PASS
default state: delete mark/yank: PASS
default state: goto bracket: PASS
default state: select brackets: PASS
default state: soft undo/redo interleaves selection and text: PASS
default state: evicted text markers redo before their selection records: PASS
default editing: undo preserves a trusted far anchor without a view continuation: PASS
default editing: unchanged unindent retains revision/history: PASS
default editing: repeat retains chord arguments and groups undo: PASS
default editing: soft-row and hard-line kill macros have distinct boundaries: PASS
default editing: oversized command and every retained capability notice preserve text/selection/history: PASS
default editing: copy/cut current line retains caret and undo selection: PASS
editor_test: null 10000 keys mallocs=0 guard=active
```

The release allocation fixture’s key count is (G), inherited from
`allocation_test()`; measured allocations and guard state are (M)[bat], derived
from its allocator callbacks. The repeat regression checks the input sequence
and submitted-frame count increase by one (G: one physical shortcut), and undo
restores the original backward selection.

## Further red/green checks

The independent flag expectations were changed before clearing the stale flags:

```text
keys_test: FAIL ctrl+shift+t expected action=145 flags=0 value=0; got action=145
tests/keys_test.c:299: assertion failed: 0
```

The missing soft-row boundary behavior was seen before implementing the visual-row macros; the final fixture includes the existing break-space boundary:

```text
editor_test:74: FAIL editor_length(e) == n
editor_test:460: FAIL expect(e, "ab ghi jkl", sizeof "ab" - 1u) == 0
```

Final soft-row, mixed-prefix, capacity and distant-anchor checks passed in both build configurations. Red/green allocation counters are assertions in the tests; release’s active allocator output is pasted above.

## Validation

- `make all`: PASS, GCC release with the required strict C flags.
- `make check`: attempted on the safe display with `ASAN_OPTIONS=detect_leaks=0`; stops in `cli_test` because Unix socket binding is denied. No sandbox/display workaround was used.
- Final sanitizer and release focused entry points: PASS. Each configuration passed all 31 entry points (M)[bat], derived by counting its focused-results JSON records and checking every return code. Full per-option output and return codes are in `build/edit-d50/{san,rel}-focused-results.json`.
- The individual sanitizer sweep is recorded in `build/edit-d50/suites-final/`; final summary is below.
- View fuzzer built with clang ASan/UBSan and replayed all constructed seed inputs without an oracle/sanitizer failure. No random campaign or duration gate is claimed.

Fuzzer replay, (M)[bat], BAT0=Discharging, launch load1=2.35:

```text
#1025	INITED cov: 3468 ft: 5880 corp: 115/2494b exec/s: 0 rss: 54Mb
#1025	DONE   cov: 3468 ft: 5880 corp: 115/2494b lim: 58 exec/s: 0 rss: 54Mb
Done 1025 runs in 0 second(s)
```

Replay derivation: command-literal source fixtures multiplied by the unsigned
byte choice space (`2**8`, from `uint8_t`), plus libFuzzer’s initial empty input.
The seed count is computed from the files under `build/edit-d50/fuzz-corpus`;
`-runs=0` requests corpus replay only. The printed elapsed value is libFuzzer’s
coarse observation, not a duration or budget chosen by this worker.

## Benchmarks

Keys and view benches completed. Editor’s null serial workload completed with
zero allocations and a structurally valid G1 row, then the benchmark failed at
native backend initialization because the display socket is denied. TRACK only;
no Sublime latency comparison or hardware gate verdict is claimed.

Commands and power/load stamps are recorded in `build/edit-d50/*_bench.log`.
The view sample count is (G), derived from the minimum accepted by the existing
benchmark. Keys and editor event counts are (G), their unchanged harness defaults.
Measured rows derive percentiles from those collected samples.

Measured final rows (M)[bat]; power/load and sample derivations are included in the rows or preceding harness-default explanation. Units and bounds are inherited from the existing benchmark; no budget was introduced here.

```text
BENCH keys_lookup TRACK n=1000000 p50=0.029 us p99=0.065 us (M)[bat] load1=2.97 gate_p99<=1.000 us(G) pass=1
STREAM (M)[bat] load1=2.97 matches=436925 prefixes=250575 cancelled=125575 misses=186925 mallocs=0 guard=active gate_mallocs=0(G)
ALLOCATIONS /tmp/edit-corpus/unicode.txt mallocs=0(M)[bat] load1=2.97 gate=0(G) guard=active pass=1
BENCH name=editor_null_serial_injection_T4_G1 n=10000 required_n=10000 p50=767171 p99=1216968 ci95_p50=[766710,767756] ci95_p99=[1178130,1262573] gate_p50=1000000 gate_p99=2000000 dropped=0 (M)[bat] power=Discharging load1=2.97 verdict=TRACK
G1 null workload=serial keys=10000 completed=10000 arrivals_active=0 coalesced=0 allocations=0 mutations=10000 journal=10000 structural=OK mode=TRACK (M)[bat]
editor_bench:506 failed: editor_open(&e, &cfg, &b) == 0
```

## Full sanitizer sweep and socket failures

Final individual sanitizer sweep: **66 binaries = 49 passed + 17 socket-dependent failures** (M)[bat]. Derivation: count records in `suites-final/results.json`, partition by observed process return code. Launch BAT0=Discharging; load1=2.35 from `/proc/loadavg`. All socket-free suites passed. GL’s pure suite passed with native diagnostics skipped, so native GL still requires coordinator verification.

| Failed suite | Sandbox-dependent part | First diagnostic (compiled source location) |
|---|---|---|
| `cli_test` | Unix IPC bind | `cli_test:215: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK` |
| `editor_close_test` | X/display setup | `editor_close_test:33: RED editor_open(&e,&cfg,&b) == 0` |
| `editor_large_test` | Unix IPC bind | `edit-2ty action=7 PageUp input_submit_ns=176608 (M)[bat]=T4-T0 bound_ns=500000 (G)=EDITOR_SLICE_NS gate=TRACK-saneditor_large_test:215: FAIL ipc_server_init(&server, runtime) == IPC_OK` |
| `editor_p4_test` | Unix IPC bind | `editor_p4_test:540: FAIL ipc_server_init(&server, runtime) == IPC_OK` |
| `editor_test` | Native X setup and IPC child fixtures | `editor_test:1745: FAIL waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0` |
| `file_kill_test` | X/display setup | `file_kill_test:198: FAIL rc == 0` |
| `ipc_test` | Unix IPC bind | `FAIL tests/ipc_test.c:130: init_rc==IPC_OK` |
| `raster_test` | X/display setup | `raster_test:1125: FAIL plat_init(&pl, &pc) == PLAT_OK` |
| `refproto_test` | X/display setup | `refproto_test:62: FAIL refproto_display_open(d,"refproto_test",true) == 0` |
| `refwin_test` | X/display setup | `See full log` |
| `refwin_timeout_test` | X/display setup | `refwin_timeout_test: RED missing input-stage diagnosis; actual log:` |
| `x11_clip_review_test` | X/display setup | `FAIL: connect DISPLAY=:99` |
| `x11_clip_test` | X/display setup | `x11_clip_test: FAIL (plat_init after private Xvfb startup)` |
| `x11_identity_test` | X/display setup | `x11_identity_test:25: FAIL plat_init(&p, &cfg) == PLAT_OK` |
| `x11_live_test` | X/display setup | `FAIL tests/x11_live_test.c:118: §22 colormap protocol error returned -1` |
| `x11_order_test` | X/display setup | `x11_order_test: FAIL fixture initialization` |
| `x11_stall_test` | X/display setup | `FAIL: live display initialization` |

The non-display `ipc_test` also fails: this sandbox denies generic Unix socket binding. It is an additional environment exception, not a passing non-display suite. Mixed editor suites completed their headless cases before the socket-dependent failures. Coordinator execution is required for every listed failure.

## What remains

The complete list is in the decision. In particular: previous-line selection,
paragraph wrapping, reindent, paste-and-indent, comments/scope/smart selection,
multiple-caret occurrence/line/column commands, syntax snippets/tag completion,
macro recording/replay, change reversion and bookmark/search-dependent behavior
are still unsupported. Panel/window/project/build/completion/diff/folding/spelling/
font UI stays flagged, consumed and counted. Large commands exceeding the
existing view windows, a full kill top or approximate visual-row queries are
refused without modifying text/selection/history and show the retained notice.
These limitations mean this bead does **not** yet meet the full requested
text-command golden-master coverage.

The coordinator must rerun display/IPC tests and LeakSanitizer outside this
sandbox, compare the native selection/clipboard/bracket/subword/Unicode details
against Sublime, and run the next Astra drive on the same resulting build.
No Sublime process was launched here.

## Findings outside this work

- The sandbox denies generic Unix `bind`, not just X connections. A writable-temp-path probe returned `PermissionError: [Errno 1] Operation not permitted`; IPC-containing suites need coordinator execution too. This was reported, not bypassed.
- `keys_bench` still has a latency-based failure exit despite declaring TRACK mode; inspected existing behavior, left unchanged under the one-bead rule. Its run passed here.
- Editor benchmark native initialization fails rather than skipping unavailable display backends; the successful null workload and subsequent failure are both retained.
- Existing wrap row-end treatment includes the break space in the calibrated fixture. This bead uses the published visual boundary; the coordinator must compare it with Sublime. No layout change was made.
- Parser/scope and multiple-selection prerequisites were not built opportunistically; their missing commands remain visibly unsupported. Other existing file/UI behavior was not rewritten.
