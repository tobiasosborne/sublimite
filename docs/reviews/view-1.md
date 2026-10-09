16 findings: **6 BLOCKER, 9 MAJOR, 1 MINOR**.

The existing release and ASan/UBSan `view_test` binaries pass; the release allocation guard is active. No files were changed. Read-only restrictions prevented rebuilding, and ptrace was denied, so the reproductions below are source-derived arguments, not newly executed failing tests.

## 1. BLOCKER — End on a trailing empty line moves into the previous line

Location: [src/view/view.c:37](/home/tobias/Projects/editor/src/view/view.c:37), [src/view/view.c:338](/home/tobias/Projects/editor/src/view/view.c:338).

`line_end()` strips a newline before its computed endpoint without checking whether that newline belongs to the requested line.

Concrete repro: initialize `"a\n"`, issue Ctrl+End, then End. The cursor starts at EOF, offset 2, on the empty second line. `piece_line_to_byte(tree, 2)` returns 2; `line_end()` sees the preceding LF and returns 1. End consequently moves to the first line’s newline. `"a\r\n"` similarly moves from offset 3 to offset 1. Shift+End incorrectly selects the preceding newline.

**Fix:** obtain the requested line’s start and strip terminators only while `end > start`. Add trailing-empty-line cases for LF, CRLF, repeated empty lines, and Shift+End.

## 2. BLOCKER — Approximate column fallback scrolls away from the cursor

Location: [src/view/view.c:158](/home/tobias/Projects/editor/src/view/view.c:158), [src/view/view.c:264](/home/tobias/Projects/editor/src/view/view.c:264).

The column fallback returns the column of the scanned prefix. `P_FOLLOW` then treats that lower bound as the actual cursor column and changes `hscroll` accordingly. This does not satisfy the documented cursor-follow behavior.

Concrete repro: use the existing 200,000-byte ASCII fixture with 20 columns. Install its exact checkpoint callback and issue Ctrl+End: `hscroll` becomes 199,981. Type `"a"`. The command suppresses checkpoints because it edited the tree, scans a bounded prefix for the column, and moves `hscroll` back into that prefix. The cursor remains at offset 200,001, far outside the rendered viewport. The existing test checks callback suppression, but never checks cursor visibility afterward.

This also affects movement near the end of long lines without checkpoints.

**Fix:** do not apply a prefix column as an exact cursor column. Preserve/update a known column through local edits, reuse certified checkpoints preceding the edit, and provide a coordinated bounded fallback that actually includes the cursor. Test the rendered cursor after movement and typing on long lines.

## 3. BLOCKER — Cancelling post-edit repair leaves an invalid viewport

Location: [src/view/view.c:230](/home/tobias/Projects/editor/src/view/view.c:230), [src/view/view.c:281](/home/tobias/Projects/editor/src/view/view.c:281).

Post-edit repair puts the selection at zero but leaves `first_line`, `first_byte`, and `hscroll` unchanged. `view_cancel()` only clears `busy`, exposing those stale fields as completed state.

Concrete repro:

1. Use `"a"` followed by 100,000 combining marks, then `"\nb"`, with one viewport row.
2. Issue End, then Shift+Ctrl+End. The selection covers `"\nb"`; the viewport starts at the second line.
3. Delete. The deletion commits, and replaying the large preceding cluster returns `VIEW_MORE`.
4. Cancel.

The former second-line start is now beyond EOF. Passing this state to layout produces `LAYOUT_ERR_ARG` at `layout.c:70`. The documented cancellation path accepts the edit but provides unusable viewport state. The successful-insertion/delete-failure path at lines 314–315 has the same stale-viewport problem.

**Fix:** establish a coherent fallback viewport when installing the temporary selection at zero, including error exits. Cancellation after an edit must leave a valid line start and visible cursor. Add cancellation and partial-failure layout integration tests.

## 4. BLOCKER — A no-op Delete at EOF can move the cursor to zero

Location: [src/view/view.c:220](/home/tobias/Projects/editor/src/view/view.c:220), [src/view/view.c:329](/home/tobias/Projects/editor/src/view/view.c:329).

`remove_range()` starts boundary repair even when `hi == lo`. It resets both endpoints to zero despite making no edit.

Concrete repro: use a single cluster consisting of `"a"` plus 100,000 combining marks. Move to EOF and press Delete. `S_NEXT` immediately returns EOF; the empty deletion then starts replaying the entire cluster from line start. The call returns `VIEW_MORE`, with `change.changed == false` and both endpoints zero. Cancelling leaves the cursor at zero permanently, although Delete at EOF should have done nothing. An empty TYPE with no selection follows the same repair path.

There is another unnecessary endpoint scan: Left or Backspace at offset zero enters `S_PREV` and scans the first cluster before recognizing that the destination is zero.

**Fix:** short-circuit empty mutations and document-boundary no-ops before segmentation or repair. Preserve the selection and viewport. Test long-cluster Left/Backspace at zero, Delete at EOF, and empty TYPE, including cancellation.

## 5. BLOCKER — Layout cannot display the valid CRLF cursor stop

Location: [src/layout/layout.c:348](/home/tobias/Projects/editor/src/layout/layout.c:348), [src/layout/layout.c:451](/home/tobias/Projects/editor/src/layout/layout.c:451).

View correctly places the cursor before the CR of a CRLF cluster. Layout skips that CR and later checks for a newline cursor only at the LF offset.

Concrete repro: `"a\r\nb"`, followed by End, gives cursor offset 1. Layout draws `a`, skips the CR at offset 1, and ends the row at LF offset 2. Its `l->pos == l->cursor` check fails, and no cell receives the cursor. The same occurs after Right moves from `a` to the CRLF stop.

The prescribed integration—passing view’s cursor directly to `layout_set_cursor()`—therefore loses the cursor on ordinary CRLF files.

**Fix:** retain the logical CRLF start when rendering the end-of-line cursor. Do not translate view state to an offset inside CRLF. Add view-to-layout cursor assertions for LF, CRLF, lone CR, and EOF.

## 6. BLOCKER — Layout truncates view’s horizontal scroll beyond 4 GiB

Location: [src/view/view.h:27](/home/tobias/Projects/editor/src/view/view.h:27), [src/layout/layout.h:74](/home/tobias/Projects/editor/src/layout/layout.h:74), [src/layout/layout.h:141](/home/tobias/Projects/editor/src/layout/layout.h:141).

View stores `hscroll` as `uint64_t`; both layout’s viewport and runtime store it as `uint32_t`. There is no representation-preserving integration for the allowed file sizes.

Concrete argument: on a 5,000,000,000-byte ASCII line, an exact checkpoint permits Ctrl+End to calculate `hscroll = 4,999,999,921` for 80 columns. Assigning that value to `layout_viewport.hscroll` truncates it to 705,032,625. Layout renders a different region and cannot show the cursor at EOF. This remains broken even with exact columns and completed indexing. G1 explicitly covers files through 10 GB.

**Fix:** carry 64-bit horizontal document columns through layout’s public viewport, runtime state, and arithmetic. Add a large-column integration test that verifies preservation and cursor visibility.

## 7. MAJOR — Unsliced line queries can scan the complete file before returning MORE

Location: [src/view/view.c:299](/home/tobias/Projects/editor/src/view/view.c:299), [src/view/view.c:351](/home/tobias/Projects/editor/src/view/view.c:351).

Every command eagerly queries the current line’s end, including TYPE and document-boundary commands. Vertical commands additionally request the exact total line count. These calls occur outside the resumable scanner.

The current synthesized piece implementation still resolves uncounted mapped entries synchronously: `ent_nl()` scans their bytes at `piece.c:348`, and `piece_line_count()` resolves the whole tree.

Concrete repro: initialize a mapped `log_1g.txt` tree with unresolved counts, then press Down at offset zero. The total-line query scans the 1 GB file to determine its 8,947,842 lines. On `oneline_1g.txt`, even typing at offset zero first searches for the next line through the complete file. No `VIEW_MORE` or input check can interrupt this work. A separate completed `lineidx` does not help: view has no line-query adapter.

**Fix:** make queries demand-driven and use a bounded index/query interface. Return pending work before a global scan, and perform bulk indexing on snapshots through `src/work`. Add unindexed large-file tests and per-call timing rows.

## 8. MAJOR — Replacing a large selection performs an unbounded UI-thread copy

Location: [src/view/view.c:223](/home/tobias/Projects/editor/src/view/view.c:223), [src/view/view.c:312](/home/tobias/Projects/editor/src/view/view.c:312).

Selection deletion is one synchronous `piece_delete()` call. Passing `NULL` for the reference does not eliminate its copying: the frozen contract requires deleted original bytes to be retained, and the current implementation uses a temporary reference and copies them.

Concrete repro: Ctrl+A on a mapped 1 GB file, then type one character. The typing call inserts that character and synchronously copies the old 1 GB selection into add-buffer storage before returning. `v->scanned` does not record this work. With insufficient reserve, the command instead fails after committing the insertion.

The documented “opaque piece operations” exception does not exempt this path from G1 or the 0.5 ms UI-slice rule.

**Fix:** provide a bounded bulk-replacement operation with reservation and explicit progress semantics, keeping UI ownership of mutation and using snapshots for worker preparation. Verify large-selection replacement, responsiveness, and allocation failure.

## 9. MAJOR — The scan budget does not enforce the 0.5 ms UI-slice contract

Location: [src/view/view.h:9](/home/tobias/Projects/editor/src/view/view.h:9), [src/view/view.c:116](/home/tobias/Projects/editor/src/view/view.c:116).

One call can process roughly 65 KiB of a pathological cluster through successive window refills. There is no elapsed-time check or smaller per-call decoding limit.

The UTF-8 header’s documented slow-chain cost is approximately 15 ns/byte; its suggested 4,096-byte budget is approximately 60 µs. View permits about sixteen times that work per call—approximately 0.96 ms under that planning model, before other work. That exceeds §0.2’s 0.5 ms slice requirement. This arithmetic is an inference from repository evidence, not a fresh measurement.

Checking `scanned <= 65536` cannot establish a wall-time bound.

**Fix:** use a conservative per-call segmentation budget and a deadline checked between bounded chunks. Measure command and continuation latency separately on the required quiet fixtures.

## 10. MAJOR — Returned changes cannot be integrated with the existing undo API

Location: [src/view/view.c:223](/home/tobias/Projects/editor/src/view/view.c:223), [src/view/view.c:307](/home/tobias/Projects/editor/src/view/view.c:307), [src/undo/undo.h:1](/home/tobias/Projects/editor/src/undo/undo.h:1).

View mutates the tree directly. Undo requires every mutation of its tree to pass through the log and exposes no API for recording an already completed mutation. View also discards deleted-byte references.

Concrete repro: initialize an undo log, then use view to delete `"b"` from `"abc"`. The returned tuple `(1, 1, 0)` cannot register that deletion with undo. Calling `undo_delete()` afterward deletes `"c"`; doing nothing leaves the deletion absent from history. Direct view edits also fail to invalidate undo’s redo history.

The instruction to feed the returned tuple to “layout/undo integration” is therefore insufficient for undo with the current interfaces.

**Fix:** route view mutations through an editor-supplied mutation interface backed by undo, or separate movement/edit planning from mutation execution. Test typing, replacement, deletion, grouping, redo invalidation, and failures through the actual integration.

## 11. MAJOR — Argument errors leave a stale successful change in the output

Location: [src/view/view.c:291](/home/tobias/Projects/editor/src/view/view.c:291), [src/view/view.c:284](/home/tobias/Projects/editor/src/view/view.c:284).

Both APIs validate arguments before clearing `*change`. The public contract and STATUS instruct callers to inspect changes on errors without excluding argument errors.

Concrete repro: successfully type `"x"` using a reusable `view_change`. Then call TYPE with `text == NULL` and `len == 1`, using the same output. The call returns `VIEW_ERR_ARG`, but `change.changed` and the preceding insertion tuple remain set. An editor following the stated error contract can apply or journal that insertion twice.

**Fix:** clear a non-null output before validating the other arguments. Define argument-error output explicitly and test output reuse after success, MORE, busy rejection, and invalid calls.

## 12. MAJOR — The benchmark cannot fail for a timing regression

Location: [bench/view_bench.c:53](/home/tobias/Projects/editor/bench/view_bench.c:53).

Every timing row passes `(0, 0)` to `bench_report()`. The harness defines zero gates as disabled, so any finite latency passes if samples are present. Only allocation failures affect the benchmark’s final result.

Concrete argument: adding an arbitrary delay to every command leaves every timing row successful. STATUS already records multi-second rows with `pass=1` and exit zero. Those measurements were on the earlier reference kernel and are not current performance evidence, but they demonstrate the disabled failure mechanism.

There is also no per-continuation timing check, deep-file typing row, substantial selection replacement, or required controlled worker-contention fixture.

**Fix:** retain TRACK reporting for unsuitable measurement conditions, but enforce applicable necessary G1/G3 limits and per-call slice limits on quiet controlled runs. Add deep-file, replacement, and contention cases. The 50 µs estimate should remain an estimate unless explicitly promoted to a gate.

## 13. MAJOR — The fuzzer has no independent movement or selection oracle

Location: [fuzz/view_fuzz.c:63](/home/tobias/Projects/editor/fuzz/view_fuzz.c:63), [fuzz/view_fuzz.c:93](/home/tobias/Projects/editor/fuzz/view_fuzz.c:93).

The model takes cursor and anchor from the implementation before each command. It independently checks edit bytes and destinations, but movement only has to produce some valid boundary. Preferred column, selection direction, viewport state, and approximation behavior are never independently checked.

Concrete example: the `"a\n"` Ctrl+End/End error in finding 1 passes these checks because offset 1 remains a valid boundary. Subsequent edits use that incorrect implementation cursor as the model’s starting point. A vertical command that always moved to zero could likewise pass the movement checks.

Initial content is capped at 1,020 bytes and typing adds at most 15 bytes per operation, so the 256-step driver cannot generate the large clusters used by the resumable-path tests.

**Fix:** maintain independent expected cursor, anchor, preferred column, and viewport state. Add structured inputs for long clusters, continuation phases, cancellation, and checkpoint-backed movement.

## 14. MAJOR — External-edit rebasing and restored-state invariants lack contract tests

Location: [src/view/view.h:67](/home/tobias/Projects/editor/src/view/view.h:67), [docs/decisions/P3.2.md:26](/home/tobias/Projects/editor/docs/decisions/P3.2.md:26), [tests/view_test.c:244](/home/tobias/Projects/editor/tests/view_test.c:244).

The decision document explicitly assigns external-edit normalization to the caller. The public header does not describe that obligation, endpoint affinity, or restoration validation, and neither test driver exercises external edits or restored state. View offers no bounded normalization operation preserving both selection endpoints.

Concrete required cases:

- Insert a three-byte character before the cursor in `"ab"`: retaining offset 1 puts it inside UTF-8.
- Delete `[1,4)` from `"abcdef"` with cursor 5 and anchor 2: rebasing should move the surviving endpoint to 2 and resolve the deleted endpoint at the edit boundary; merely clamping both to EOF selects different text.
- Delete a newline between a base and combining marks: byte rebasing alone leaves an endpoint inside the newly joined cluster.

These are caller obligations under the current decision, rather than evidence that automatic rebasing was promised. Their absence leaves a critical editor integration responsibility unverified.

**Fix:** document the full obligation publicly and provide/test a bounded edit-notification and state-restoration boundary, with explicit affinity, cluster repair, preferred-column reset, viewport normalization, and pending-work handling.

## 15. MAJOR — Allocation-failure and successful-prefix contracts are untested

Location: [tests/view_test.c:13](/home/tobias/Projects/editor/tests/view_test.c:13), [tests/view_test.c:30](/home/tobias/Projects/editor/tests/view_test.c:30), [fuzz/view_fuzz.c:98](/home/tobias/Projects/editor/fuzz/view_fuzz.c:98).

The suite supplies large arenas and requires final success. The fuzzer does the same. Neither deliberately fails a piece allocation or exercises the documented insertion-success/delete-failure path.

Concrete untested contract: replace a large original selection, allow insertion to succeed, then fail allocation required to retain the deleted bytes. View returns an error with a committed insertion prefix, resets the selection, and leaves viewport fields stale. The current suite checks none of the resulting content, change tuple, busy state, viewport, or next-command behavior.

**Fix:** add a deterministic failing allocator and cover insertion failure, deletion failure, replacement-prefix failure, retry/continuation, and subsequent commands. Assert the exact committed prefix and coherent state on every exit.

## 16. MINOR — Benchmark initialization can start inside a grapheme and UTF-8 unit

Location: [bench/view_bench.c:42](/home/tobias/Projects/editor/bench/view_bench.c:42).

The benchmark claims to start two real clusters into the file, but segments only a 256-byte prefix. The one-shot UTF-8 API treats that prefix as complete input.

Concrete repro: begin a corpus with `"a"` plus 200 combining acute marks. Its first cluster occupies 401 bytes. The prefix cuts a multibyte mark; the initialization loop can place the cursor at byte 256, inside both the original cluster and that UTF-8 unit. The benchmark then accepts commands based only on return code and work count, without checking endpoint validity.

**Fix:** establish initial stops using complete resumable segmentation over the source, or choose a certified fixture boundary. Validate benchmark endpoints before and after measured commands.

| # | Severity | Location | One line |
|---|---|---|---|
| 1 | BLOCKER | `src/view/view.c:37` | End leaves a trailing empty line for the preceding newline. |
| 2 | BLOCKER | `src/view/view.c:264` | Prefix-column fallback scrolls away from the actual cursor. |
| 3 | BLOCKER | `src/view/view.c:281` | Cancelled repair exposes stale, potentially out-of-range viewport state. |
| 4 | BLOCKER | `src/view/view.c:220` | No-op deletion can return MORE and lose the cursor on cancellation. |
| 5 | BLOCKER | `src/layout/layout.c:348` | Layout skips the cursor stop before CRLF. |
| 6 | BLOCKER | `src/layout/layout.h:74` | Layout truncates view’s horizontal scroll beyond 4 GiB. |
| 7 | MAJOR | `src/view/view.c:299` | Exact line queries can scan the complete mapped file synchronously. |
| 8 | MAJOR | `src/view/view.c:223` | Large-selection replacement copies deleted bytes without yielding. |
| 9 | MAJOR | `src/view/view.h:9` | The byte budget does not enforce the 0.5 ms slice contract. |
| 10 | MAJOR | `src/view/view.c:307` | Direct mutations cannot be registered through the existing undo API. |
| 11 | MAJOR | `src/view/view.c:291` | Argument errors retain a stale successful change tuple. |
| 12 | MAJOR | `bench/view_bench.c:53` | All timing gates are disabled. |
| 13 | MAJOR | `fuzz/view_fuzz.c:63` | Fuzzing adopts implementation cursor state instead of checking motion semantics. |
| 14 | MAJOR | `tests/view_test.c:244` | External edits and restored-state normalization lack contract coverage. |
| 15 | MAJOR | `tests/view_test.c:30` | Failure and successful-prefix behavior are never deliberately exercised. |
| 16 | MINOR | `bench/view_bench.c:42` | A truncated prefix can seed the benchmark inside a cluster and UTF-8 unit. |