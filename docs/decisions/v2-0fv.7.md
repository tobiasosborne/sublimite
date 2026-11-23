# v2-0fv.7 · L1 piece, part 7a: the frozen API, its model and its suite

This bead writes the piece API before any tree exists (Law 9 at module scale). The deliverables are
`src/piece/piece.h`, the flat model `tests/piece_model.h`, the model-based suite `tests/piece_test.c` and the fuzzer
`fuzz/piece_fuzz.c`. The tree behind the API is v2-0fv.7b. Its acceptance specification is
[wave1-piece-pool.md](wave1-piece-pool.md) (signed). v1 at pin `6fec2e8` was used as evidence only.

The advisor froze the Coordinates, Reading, Editing, Snapshots, Checkpoint and Maintenance sections at 2bfcce5, with three
edits (§3, §4, §5) and the line-count rulings R7 and R1+2a–e (§6, §9). The Lines section (§9) is a draft under review.

## 1. The model of a text

**Invariant.** A text is a sequence of bytes, and each byte has a source coordinate in ORIGINAL or in ADD. Every observable
is a function of that coordinate sequence and the two stores: the length, the bytes, and the *description*. The
description is the coordinate sequence cut into maximal runs whose offsets rise by one within one store. It is canonical,
so it does not depend on how the tree stores its runs. ADD offsets are logical and contiguous. An append of n bytes takes
`[a, a + n)`, with `a = piece_add_length`. Only a checkpoint abort lowers the ADD length.

**Simplest alternative:** v1's `piece_ref`. It describes deleted bytes as at most 8 ADD spans, after copying any deleted
ORIGINAL bytes into ADD. **Counter-example:** signed decision 10(b). A one-span 1 GiB delete then copies 1 GiB, where a
16 B span suffices. Because the description is canonical, any two implementations of the header must produce identical
spans. That is what makes the span-sequence inverse testable against a flat model.

## 2. Edits, admission and the checkpoint

| Element and invariant | Simplest alternative | Counter-example / disposition |
|---|---|---|
| Three edit primitives. `piece_replace` copies bytes. `piece_replace_spans` inserts by reference. `piece_capture` copies a range into ADD and returns its span. Each is atomic, including the ADD length. | Separate insert and delete, or "append to ADD" followed by "insert by reference". | Typing over a selection would need two admissions and a checkpoint just to be atomic. With a split append, a refused insert leaves orphan ADD bytes and a moved ADD mark, so the height-refusal case "compare the ADD mark before and after" fails. |
| Every call that can need storage admits itself. It computes its claim from the current shape, then funds it from the core, the reserve, retired storage and base, in that order, before it changes anything. No state is stored. | An `admit(claim) → token` call, then `execute(token)`. | Another edit between admit and execute changes the height, so the claim goes stale. A forgotten execute leaks the claim. Either way a second state machine is needed. Compound commands get atomicity from the checkpoint, whose abort cannot fail. |
| The tree is the checkpoint handle. At most one checkpoint is open per tree. | v1's `piece_checkpoint *` handle, consumed exactly once. | The "consumed handle used again" class of caller bug exists only because a handle exists. |
| `piece_view` serves the reads of both a live tree and a snapshot. | v1's paired functions, `piece_len` / `piece_snapshot_len` and so on. | v1 had 37 functions with a duplicate of every query. The view gives one read API, and lineidx, find and save take one type. |
| A take with no successful non-empty replace and no abort since the previous take returns the same snapshot. It claims nothing and cannot fail. | A fresh header on every take. | The spec's "repeated unchanged takes would consume unbounded headers". The cache-hit rule is observable, so the model and the suite check it. |
| The core holds one backing reference, released by the core's last owner on that thread. | v1 acquired the backing once per snapshot. | Snapshots already own the core, so a per-snapshot reference repeats a count that already exists. |
| A live-view iterator records the revision. `next` after an edit, without a seek, is an ASSERT. | An unchecked lifetime rule, as in v1. | A use-after-edit read returns stale bytes from a freed chunk silently. The check costs one comparison per run. |

**Fault ids.** piece never registers or polls on behalf of a reader. `piece_backing` carries the view, its `os_fault_id`
and the owner's acquire and release. A reader brackets its reads with two polls (os.h). The Lines queries are the exception:
under F5 they poll once themselves.

## 3. Edit 1, then R7: the error domain and the length bound

`PIECE_ERR_RANGE` means the height limit and nothing else. `PIECE_MAX_LENGTH = 2^60` is an invariant that every open and
every edit ASSERTs of its result.

**Why it holds (P).** Every descriptor lies inside one `PIECE_CHUNK`-aligned chunk, so a text of length L needs at least
L / 65,536 descriptors. Each takes at least 16 B, the signed leaf entry. `os_vm_reserve` maps with no address hint. Linux
x86-64 then places user mappings below 2^47 under 4-level paging, which gives L < 2^47 / 16 × 2^16 = 2^59. 5-level paging
allows about 2^56 / 16 × 2^16 = 2^68, so there the ASSERT is what holds the bound. Every newline sum is therefore below
2^60, and bit 63 of a branch sum is free for the unknown flag by construction (Astra 7).

ADD's bytes are all stored, so the ADD length stays below the address space. A span-sum overflow in `piece_replace_spans`
is a precondition.

## 4. Edit 2: the reserve's thread contract

Spec "Extent ownership" says only that "the shared reserve needs serialization only for whole-extent handoff, refill and
teardown". The mechanism is chosen here.

**Invariant.** Every extent is in exactly one of three places: attached to a live core, on the reserve's free list, or
returned to base. Every draw, every refill and every return to base happens on the UI thread. Storage comes back from
other threads on two lock-free paths:

1. **While a tree is open.** The last release of one of its snapshots, on any thread, pushes that revision's storage onto
   the core's retirement queue. This is a multi-producer, single-consumer stack: one compare-and-swap per push. The UI
   drains the queue (`piece_reclaim`, and admission before it reports NOMEM).
2. **After close.** The thread that drops the core's last owner owns everything left in it. That thread walks the
   storage, calls the backing's `release`, and pushes the core's extents onto the reserve's *inbox* as one chain, with
   one compare-and-swap. The UI takes the whole inbox with one atomic exchange before any draw. There is a single
   consumer and the exchange takes all, so there is no ABA problem.

`piece_reserve_destroy` requires every tree closed and every release call returned. The cores count lives in the reserve
and drops after the push.

- **Simplest alternative 1: a mutex around the free list.** Counter-example: workers run below the UI's priority. A
  worker preempted inside the critical section blocks the UI's admission on the typing path for a scheduler quantum.
  That is priority inversion inside the Law 2 / G1 window, which an O(1) critical section does not prevent.
- **Simplest alternative 2: post the orphaned core to the UI.** Counter-example: the UI then pays for walking the
  orphan's nodes and unmapping its view. Take a closed 10 GiB tab whose find job finishes later: the UI would retire
  7,284 slots (P, spec packed table) and unmap 10 GiB on behalf of a worker.

## 5. Edit 3: no aliasing in `piece_replace`

`bytes` must not point into the tree's own storage. This is not checkable, because ADD is not one range. No named caller
needs aliasing. Enter's indentation run (indent.h, "inserts ... the run's bytes at lo, by reference or by copy") goes in
by reference with `piece_replace_spans`, or from a copy of at most `INDENT_BLANK_BYTES` + 1 bytes. Duplicating a selection
is by reference too. 7b therefore has one fewer case: no read of a chunk that the same call may be extending.

## 6. Carried counts (R1+2a, b, e)

`piece_span` stays 16 B. It is the description's unit and the journal's unit. A cached count must never reach disk.

`piece_span_nl` is an optional parallel record of 8 B (P: two 32-bit counts) that only undo keeps.
`piece_iter_next_span` fills it when asked, and `piece_replace_spans` takes the array, or NULL. Descriptors follow their
store's chunk grid, so a span from chunk i to chunk j is three parts: a head in chunk i, whole chunks i+1 .. j−1 counted
from the store's chunk totals, and a tail in chunk j. The record holds the head and tail counts, each at most 65,536 or
`PIECE_NL_UNKNOWN`. NULL (journal replay, recovery) inserts the spans' mapped ends unknown, and resolve heals them.

**Undo.** The literal threshold stays at 16: literal exactly when 16 × spans > length. The in-memory inverse entry is
**24 B** (16 B span + 8 B record). The journal carries the 16 B span alone.

**`piece_iter_next_span` gained an `end` argument.** A clipped span needs its tail count computed at the clip. A caller
who clips a returned span afterwards would hold a stale tail count.

### Counter-example to R1+2c's pair bound (for the advisor)

R1+2c states: "A one-span delete plus its undo together read ≤ 65,536 B (the undo reinserts at an entry boundary and cuts
nothing)." Its premise is that both counts exist at description time at no cost. They do only where the described
range's ends fall on descriptor boundaries. Undo describes `[off, off + len)` before the delete. When off lies inside a
counted descriptor D, the head count is the newlines in the part of D from off on. That needs a scan of D's shorter part,
at most 32,768 B. The delete then cuts D at off and repeats exactly that scan.

The pair with a reinsert-at-boundary undo therefore reads up to 2 × 32,768 (describe) + 2 × 32,768 (delete) = 131,072 B
(P). That is A13's signed bound, not the tightened 65,536. Options:

- **(A)** Accept 131,072 for describe + delete + undo. The per-call bounds stay as written. This option keeps one
  description path, and the gate is the signed one.
- **(B)** `piece_replace` returns the counts of the deleted range's two edge parts through an optional out-parameter, and
  the describing iterator leaves an end inside a counted descriptor for the delete to fill. One scan per end serves both
  the tree and the inverse, giving 65,536. This changes the frozen `piece_replace`.
- **(C)** The describing iterator marks such an end unknown without reading. This gives 65,536, but undo loses exactness
  at those two ends until resolve, which breaks "carried counts keep undo/redo exact".

Recommendation: **(A)**. The header currently states per-call bounds that are consistent with (A).

## 7. The model and how a client links it

`tests/piece_model.h` is a flat model of exactly this API: no pool and no tree.

- **Text.** A canonical array of coalesced spans over two stores. ORIGINAL is the private copy, or the borrowed mapped
  view. ADD is one contiguous buffer, grown by copy.
- **Snapshots.** A snapshot copies the span array and the ADD prefix, so an abort that lowers the ADD length can never
  change a snapshot's bytes.
- **Checkpoints.** A checkpoint saves the span array and the ADD length at begin. Abort swaps them back and allocates
  nothing.
- **Edits.** Every edit builds its result into new storage before it installs it, so a refusal changes nothing.
- **Storage.** All storage comes from `base_arena` (that is, `os_vm_reserve`), so a test that refuses base refuses the
  model too.
- **Lines.** These queries are not modelled yet (§9). The model defines them as ASSERTs so that a client which calls them
  fails loudly.

**One link mechanism.** `tests/piece_model.c` defines `PIECE_MODEL_IMPL` and includes `tests/piece_model.h`. Its object
`build/<cfg>/tests/piece_model.o` defines every `piece.h` symbol. A suite whose binary lists that object as a prerequisite
links it ahead of `libtext.a`, so the archive's `piece.o` is never selected. A reference that would select it is a
duplicate-definition link error, so the mistake cannot pass silently. `piece_test` and `piece_fuzz` link the model until
7b lands. 7b deletes their Makefile line and adds the `PIECE_TEST_HOOKS` variant of `piece.c`.

## 8. The suite: what any implementation must pass, and what is 7b's

`tests/piece_test.c` and `fuzz/piece_fuzz.c` check the implementation under test against the model through observables
only: length, bytes, description, ADD length, snapshot identity and results. While the model *is* the implementation, the
differential half compares two instances of the same code. It still exercises the adapter, ownership, snapshot isolation
and the ADD rules. The directed tests carry literal expectations, which check the model itself.

**Base refusal.** Injected with `-Wl,--wrap=os_vm_reserve`, the only reservation path in base. Refusal is armed only around
the call under test, so the reference model is never refused. Asserted:

- no NOMEM while base grants storage;
- every NOMEM changes nothing;
- calls that allocate nothing succeed under total refusal with zero reservations;
- the same request succeeds once base grants again;
- three refusals that every implementation must produce: reserve create, an open of n > 0 bytes, and a 1 MiB insert right
  after open (16 ADD chunks (P), more than the top-up's single boundary chunk).

**Height limit.** The refusal depends on hidden shape, so the shared suite checks only the half that holds for any
implementation, under the test hook `piece_test_set_height_limit`. Every RANGE leaves everything unchanged, the next
fitting edit succeeds, and reopening repacks. The model's hook means "no shape: never refuses".

**Real-tree-only, for 7b:**

- the derived refusal counts: height 1 at the 25th prepended run, height 2 at 205 = 25 + 15 × 12 (P), and the 408-run
  build followed by an interior delete;
- exact claims one slot short;
- extent accounting;
- read counting.

## 9. Lines (draft, under review)

The ruling keeps counts in the tree: the signed layout and fanouts stand, and the tree answers line queries. A separate
offset-keyed line table would have to be shifted by every edit. v1's 512-chunk cap, its degraded mode, its regrowth and
finding 1 all come from that.

The draft header implements the ruling and Astra's accepted findings:

- **Annotation (R7).** A monoid of (bytes, newlines, unknown). The unknown flag is bit 63 of a branch sum, free by
  construction. A leaf uses the sentinel `UINT32_MAX`.
- **What is counted when (R1+2c, d; F6).** Edits count what they create. A counted cut scans its shorter half, at most
  32,768 B. An unknown cut stays unknown. A partial unknown entry becomes counted only through the table, during
  resolve. The last short chunk is whole.
- **Reads (F8).** Bounds are stated per call. Query reads are counted apart from edit reads.
- **Faults (F5).** Every mapped scan is bracketed by polls, and tainted counts are discarded. Each query polls once more
  itself and answers inexact with lower bound 0 at a nonzero epoch. Exactness is promised for a trusted backing only.
- **Resolve (F3, F4).** A UI-only, copy-on-write, document-order sweep with a saved cursor. It is charged per node and per
  entry visited, misses included. It is admitted like an edit, returns NOMEM retryably, and runs before
  `piece_replenish`. A call that replaces the root invalidates the snapshot cache and requires a seek on live iterators.
  It restarts on adoption, on abort and on a reinsertion without counts. Completeness is scoped to the drained live root
  and to snapshots taken afterwards.
- **Select (F8).** Line origin 0. Past the last line the result is the length. The empty line after a trailing LF starts
  at the length. When blocked, select returns the first blocking unknown entry's start and the exact count before it,
  without scanning further unknown entries.

**My first draft wrote resolved counts into shared nodes in place, as atomics.** That was superseded by F3. With
copy-on-write, snapshots keep their answers, and nodes stay immutable once shared.

### lineidx: a module, or functions beside their callers?

**Functions.** After this ruling, lineidx has no state and no invariant of its own:

- the chunk table lives in piece's core;
- revision adoption, the degraded state machine and regrowth are gone.

What remains is a counting loop and an estimate:

1. **The counting loop.** A loop over `scan_count` per chunk, bracketed by the fault poll. It is keyed by piece's chunk
   grid and reads a `piece_backing`, so it is `piece_lines_count` in this header. Scheduling it belongs to the backing's
   owner (L2 `file`, or `app_buffers`): submit it to the bulk pool and adopt its batches. That owner already owns the
   mapping's lifetime and change detection, so it is the one that must stop counting on reload. The job's lifetime is
   work's lease.
2. **The estimate used while a prefix is unknown.** A pure function of the exact counted prefix, the unknown bytes and
   an average line length. Its callers are L4: viewport, gutter, minimap, goto line. It belongs in viewport's header.

A module for those two would add a header, a lifecycle and a DAG node, and carry no state. v2-0fv.18 then shrinks to a
bead for the job and the estimate, inside those owners.

## 10. v1 exports left behind

- `piece_create`, `piece_destroy`, `piece_init_copy`, `piece_init_mapped`: replaced by `piece_open_copy`,
  `piece_open_mapped` and `piece_close`. A tree is always initialized, so "init only on an empty, never-edited tree"
  cannot be misused.
- `piece_init_mapped_begin`, `piece_init_mapped_step`: open builds O(len / 64 KiB) descriptors and reads no byte. No
  observed lag needs slicing (Law 1), and a staged open can be added later if a trace asks for it.
- `piece_allocator`, `piece_default_allocator`: replaced by base-backed admission (spec "Synchronous admission"). There
  are no hooks and no malloc wrapper.
- `piece_map_hooks`, `piece_snapshot_mapping`: replaced by `piece_backing`, which carries the fault id (A5), and
  `piece_view_backing`.
- `piece_insert`, `piece_delete`: replaced by `piece_replace`.
- `piece_ref`, `PIECE_REF_SPANS`, `piece_insert_ref`: replaced by `piece_span`, `piece_replace_spans` and `piece_capture`
  (signed 10(b)).
- `piece_len`, `piece_snapshot_len`: replaced by `piece_length(view)`.
- `piece_read`, `piece_snapshot_read`: the iterator copies out. No caller needs contiguous bytes that it cannot get from
  runs.
- `piece_line_count`, `piece_line_to_byte`, `piece_byte_to_line` and their `snapshot_` twins: replaced by the Lines
  section, `piece_lines_before` and `piece_lines_start`, which carry exact flags.
- `piece_piece_count`: internal shape. It becomes a white-box test hook in 7b.
- `piece_iter_begin`, `piece_iter_begin_snapshot`: replaced by `piece_iter_init(view, off)` and `piece_iter_seek`.
- `piece_iter_next_source`, `PIECE_ADD`: replaced by `piece_iter_next_span`. "ORIGINAL spans in increasing order" no
  longer holds (A14 c).
- `piece_checkpoint` handle: the tree is the handle (§2).
- `piece_reclaim(t, budget)`: kept with the same meaning. Queries and takes no longer hide reclamation batches.
- The `PIECE_OK`/`PIECE_ERR_*` int macros and `PIECE_MORE`: replaced by the `piece_err` enum. `MORE` left with the
  staged open.

## 11. Files

- `src/piece/piece.h`: the API.
- `src/piece/piece.c`: the stub until 7b. It defines every symbol, refuses every call that can allocate, and ASSERTs the
  rest (no object can exist). It keeps the transitional `piece_app_id_len` for `src/edit/edit.c` and
  `tests/layer_L1_test.c`. Its `PIECE_TEST_HOOKS` variant defines the height hook.
- `tests/piece_model.h`: the reference and stand-in.
- `tests/piece_model.c`: the one TU with `PIECE_MODEL_IMPL`.
- `tests/piece_harness.h`: the differential state machine, the refusal wrap, the memfd mapping fixture and the span
  helper, shared by the suite and the fuzzer. Every use of `piece_span`/`piece_span_nl` in the tests goes through
  `ph_spans`, so a change to the record stays local.
- `tests/piece_test.c` and `fuzz/piece_fuzz.c`, with seeds in `fuzz/corpus/piece/`.
- `tests/lanes.mk`: `tests/piece_test.c := san tsan`, `fuzz/piece_fuzz.c := fuzz`.
- `Makefile`: the link rules also take `%_model.o`; the refusal wrap; the two model lines that 7b deletes.

## 12. Evidence (2026-11-23, [bat] Discharging, load 2.6–3.1, `nice`, `-j2`)

- **Red against the stub.** `build/san/tests/piece_test.o` linked with the stub's `piece_test_hooks.o` and no model:
  exit 1, 26 failures. Every death case fails, because the child's setup is refused and it exits 3. Every directed and
  random case fails at its open.
- **Green against the model.** The san, tsan and rel builds all exit 0 with `piece_test: ok`. The random walk runs 48
  seeds of 200 operations, with base refused on one operation in eight. It makes 5,344 fallible calls that returned OK
  and 284 that returned NOMEM under refusal, and none returned RANGE. `t_height` saw 0 refusals over 400 prepends at
  limit 1: the model has no shape.
- **Mutations.** Five broken copies of the model were linked as the implementation, against the unchanged model as the
  reference. Each one fails the suite:
  - no snapshot cache;
  - abort keeps the ADD length;
  - `next_span` ignores `end`;
  - the description never merges;
  - capture off by one.
- **Fuzz.** 8 seeds. An exploratory `-max_total_time=60` run made 10,557 runs with no crash. That is a check, not a
  budget.
- **Lanes.** `make check`, `make check-tsan` (7 threaded suites) and `make fuzz-smoke` (9 fuzzers) all exit 0.
