# Wave 0 known-bugs specifications (lane M3)

Pinned v1 oracle: `/home/tobias/Projects/sublimite-v1-oracle` detached at
`6fec2e8` ("Session 30: worklog and HANDOFF after the away block").
PLAN_V2 §6 (line 168) names four bugs for expected-failure specs; its
line numbers were written against `c95b6eb` and are re-resolved to
symbols at `6fec2e8` below. v1 was not built or run; all claims are
from source reading and `git log/show/diff` on the oracle.

Status: complete (all four sections + summary + verdict).

## Bug 1 — 17-leaf piece-tree root collapse

**Status at 6fec2e8: NOT CONFIRMED as a live bug; no v1 artifact names
it.** `grep -rn "17-leaf\|root collapse"` over the oracle's `src/`,
`tests/`, `docs/`, `.beads/issues.jsonl` finds nothing (the sibling
contract-inventory lane reached the same result, its row X1).
`src/piece/` is byte-identical between `c95b6eb` (PLAN's base) and
`6fec2e8` (`git diff --stat c95b6eb 6fec2e8 -- src/piece` is empty),
so whatever the council read is still textually present — but the
claim must be treated as an unverified council assertion until the
deciding test below is run against the oracle binary. This section
pins the locus, derives the 17, records which suspect invariants
were checked and cleared by reading, and designs the deciding test.

### Derivation of 17 (Law 4)

`src/piece/piece.c:30` `#define FAN 16u` (P: node fanout; leaf holds
`b[FAN], nl[FAN], x[FAN]`, branch holds `ch[FAN]`, lines 54–55).
17 = FAN + 1: the first count that cannot fit in one node. Two
readings, both at the same boundary:

- 17 *pieces* in a leaf root (height 0): the 17th entry forces
  `splice` to split (line 720 `if (total <= FAN)` fails) and
  `ins_batch` to promote a new branch root via `root_grow`
  (lines 783–788, 794–796).
- 17 *leaves* under one root branch: a branch holds at most FAN
  children, so the 17th leaf forces height 2.

The reverse transition is the "collapse": after a delete leaves a
branch root with one child, `piece_delete` promotes the child
(lines 1445–1447):

```c
t->root = cow(c, t->root);
node *sib = del_rec(c, t->root, off, off + len);
if (sib) root_grow(t, sib);
while (!t->root->leaf && t->root->cnt == 1) {
    node *old = t->root; t->root = old->v.in.ch[0]; pool_free(c, old, 1); t->height--;
}
```

### Suspect invariants checked by reading (all in `src/piece/piece.c`)

1. **Collapse frees the old root with `pool_free`, not
   `node_unref`** (line 1446), so the child `ch[0]` keeps the
   reference the branch owned — an ownership transfer. Sound only
   because line 1442 `t->root = cow(c, t->root)` guarantees the root
   is exclusively owned (`cow`, lines 359–372: rc==1 returns as-is,
   else copies and unrefs the shared node) and no snapshot can point
   at the copy. A snapshot taken *before* the delete holds the
   pre-COW root (line 1579–1580). Cleared by reading; still worth a
   snapshot-live variant in the test because the argument is
   refcount-subtle.
2. **`del_rec` discards split siblings from boundary recursion**:
   lines 949/953 `node *s = del_rec(...); (void)s;`. Safe only if
   prefix/suffix deletes never split. They cannot: in a leaf, a
   suffix delete has `hi == en1`, so `rep` gets at most the left
   fragment (`nr <= 1`, lines 915–925) while `ndel >= 1`, hence
   `splice` total `cnt - ndel + nr <= cnt <= FAN`; branch levels
   inherit this by induction. Cleared by reading.
3. **Exact-fit pool reservations at the FAN boundary.**
   `pool_get` only asserts availability (line 250, "every mutation
   reserves before changing content"), so an undercount in
   `reserve_insert`/`reserve_delete` (lines 326–349) is a crash, not
   an error return. The worst cases fit *exactly*: insert at
   height 1 with root `cnt == FAN` reserves 3 branches (line 345)
   and can spend exactly 3 (COW root + `splice` branch sibling at
   total 17 + `root_grow`); delete at height 1 with root
   `cnt == FAN` likewise reserves 3 (line 331) against COW root +
   split-on-delete sibling + `root_grow`. No slack — the deciding
   test must run under ASan with assertions enabled so an
   undercount fails loudly.
4. **`splice` split arithmetic** (lines 736–747): total 17 splits
   `l = (17+1)/2 = 9`, right sibling 8; `tmp[2*FAN]` and the
   `EDIT_ASSERT(total <= 2*FAN)` hold for every caller
   (`ins_batch_rec` builds at most `REF_BATCH + 2 = FAN` replacement
   entries, lines 754–767, `REF_BATCH = FAN - 2`, line 42).
   Cleared by reading.

### Minimal repro as a test design

Vehicle: **v2 unit test against the public header**
(`src/piece/piece.h`), run as a *deciding probe* — first compiled
against the v1 oracle's `piece.c` in the contract harness to settle
GREEN-vs-XFAIL, then carried into v2's `tests/piece_test.c`. A
black-box contract test cannot observe tree shape (height, root
kind) and would need ~17 non-coalescing inserts through keystrokes
to reach the same state unreliably; the header API reaches it in
six calls. (Append coalescing, piece.h lines 51–58, means sequential
typing will *not* create 17 pieces; the test must break the run
between inserts, e.g. alternate insert offsets.)

1. `piece_create` with a counting allocator; `piece_init_copy` of a
   17-byte alphabet fixture, or empty + inserts.
2. Build exactly 17 pieces in a leaf root: 17 one-byte
   `piece_insert` calls at offset 0 (each prepends, breaking the
   append run; `piece_piece_count == 17` after — assert). This
   crosses `splice` split + `root_grow` (height 0 → 1).
3. Verify full content via `piece_read` + `piece_iter_next_source`
   and `piece_line_count`/`piece_byte_to_line` against a naive
   model after every step.
4. Take `piece_snapshot_take`; keep it live across step 5.
5. Delete pieces until the root must collapse: `piece_delete` the
   first 9 bytes (left leaf after split holds 9 pieces; deleting
   them plus `maybe_merge` at `cnt < 8`, line 899, drives the root
   to `cnt == 1` → collapse loop, height 1 → 0). Verify live tree
   *and* the step-4 snapshot byte-for-byte; release snapshot.
6. Re-insert across the boundary (grow again), verify; then
   `piece_checkpoint_begin`, repeat the collapse delete,
   `piece_checkpoint_abort`, verify pre-delete content restored
   (checkpoint abort is the PLAN-paired suspect, §3).
7. Variant B (17 leaves): `piece_init_mapped` a fixture of
   17×FAN = 272 (P) 64 KiB chunks (`CH_SIZE = 1<<16`, line 32;
   `build` makes one piece per chunk, lines 1166–1174, so
   272 pieces → 17 leaves → height-2 root), delete the first
   16 leaves' bytes, verify collapse to height ≤ 1 by content.

**What v1 does / v2 must do:** unknown until the probe runs — that
is the finding. v2 must pass all steps with exact content, counts,
and snapshot isolation; if the probe is GREEN on the oracle, this
bug row closes as "council claim not reproduced" and the test lands
as a plain regression test, not an XFAIL.

## Bug 2 — Post-rename save conflict

**Status at 6fec2e8: STILL PRESENT** (traced below by symbol).
`src/savectl/` is byte-identical `c95b6eb..6fec2e8`;
`src/file/file.c` changed only by `930f7c4` (NTFS named streams,
Windows save path), which does not touch the identity flow below.

### Root cause

The save worker already does the right detection — and the UI-side
completion then installs the wrong BASE anyway.

1. `transaction_commit` (`src/file/file.c:984`) renames the temp
   over the target (line 1006) and captures the new identity from
   an fd dup'ed *before* the rename (observer, lines 1005,
   1010–1017), so `a->out_id` names *our* renamed inode even if the
   pathname is replaced a microsecond later.
2. The savectl worker `worker`, OP_SAVE branch
   (`src/savectl/savectl.c:375–381`), then captures the journal
   BASE **by pathname** and compares:

```c
if (r->replaced && s->options.journal && r->id.metadata_valid) {
    r->journal_error=journal_capture_base(s->path,&r->base);
    if (!r->journal_error && (r->base.device!=r->id.dev ||
        r->base.inode!=r->id.ino || r->base.size!=r->id.size ||
        r->base.mtime_ns!=r->id.mtime_ns || r->base.ctime_ns!=r->id.ctime_ns))
        r->file_error=FILE_ERR_CHANGED;
}
```

   `journal_capture_base` opens the *path*
   (`src/journal/journal.c:364–373`), so after a foreign
   rename-over in the rename→capture window, `r->base` describes
   the **foreign** file, `r->id` describes ours, they differ, and
   `r->file_error=FILE_ERR_CHANGED` is correctly raised.
3. But `savectl_tick` (`src/savectl/savectl.c:801–804`) installs
   finish state **without gating on `r->file_error`**:

```c
if (op==OP_SAVE && r->replaced && s->options.journal && *r->base.captured_path && !r->journal_error) {
    s->saved_base=r->base; s->saved_base.path=s->saved_base.captured_path;
    s->needs_finish=true;
}
if (r->file_error || r->journal_error) {
    if (r->replaced && r->id.metadata_valid) s->baseline=r->id;
    if (r->file_error==FILE_ERR_CHANGED || ...) changed(s);
```

   Net state: `needs_finish=true`, `saved_base` = the foreign
   file's BASE, `baseline` = our (now-unlinked) inode's identity —
   an inconsistent pair, with the finish door open.
4. The host contract says finish uses that BASE
   (`src/savectl/savectl.h:155`: "Use savectl_saved_base for new
   BASE"), and `journal_save_finish`
   (`src/journal/journal.c:1284`) re-verifies only
   `journal_check_base(saved)` — pathname vs `saved_base`. The
   foreign file *is* `saved_base`, so the check passes, finish
   succeeds, and `savectl_tick` lines 816–817 run `saved(s)`:
   buffer marked clean while disk holds foreign bytes and the
   buffer holds ours. Closing the tab then loses our text with no
   dirty prompt (data loss, Law 8 triage class).

Secondary gap, same bug family: with `options.journal == NULL`
the OP_SAVE verification at line 375 is skipped entirely and
`savectl_tick` lines 809–815 mark saved from `r->id` alone — a
post-rename foreign replacement is undetected until the next
OP_CHECK/`savectl_file_event`.

Control case that already works (keep it working): foreign
replacement *after* needs_finish but before finish is caught by
`journal_check_base` at `journal_save_finish` (BASE_CHANGED →
`changed(s)`), because there `saved_base` was captured from our
own file before the replacement.

### Minimal repro as a test design

Vehicle: **v2 unit test against the public header**
(`src/savectl/savectl.h`, fixture style of v1
`tests/savectl_test.c`), because the race window (rename →
pathname capture) is only openable deterministically through the
existing durability step hook (`savectl_options.step`,
`FILE_STEP_RENAMED` in `src/file/file.h:324`, fired at
`src/file/file.c:1021` after rename, before dirsync — hence before
the worker's capture). A black-box contract test cannot hit that
window reliably; add one only as a coarse companion (external
replace immediately after Ctrl+S, assert conflict banner + buffer
stays dirty) without depending on it for the red/green signal.

1. Fixture: target file `"old"`, journal with previous BASE
   captured from it, tree edited to `"old!"` (existing
   `ck4_directory_recovery` in `tests/savectl_test.c:1127` is the
   template; it covers identity/dirsync failure at the same hook
   point but never a foreign replacement).
2. Step hook: on `FILE_STEP_RENAMED`, write `"foreign"` to a temp
   sibling and `rename()` it over the target.
3. `savectl_save`, wait for controller idle.
4. **v1 observed (by trace):** model shows
   `file_error==FILE_ERR_CHANGED` *and* `needs_finish==true`, with
   `savectl_saved_base()` naming the foreign generation
   (size/inode of `"foreign"`). Calling `savectl_finish` with a
   CURRENT checkpoint built on `savectl_saved_base()` succeeds →
   `SAVECTL_SAVED && !modified`, contents(target)=="foreign",
   buffer=="old!" — silent data loss on close.
   **v2 must:** never enter a finishable state from a CHANGED
   post-rename verification. Per PLAN_V2 §4.4: transition to
   conflict-cancel only — `needs_finish==false`,
   `savectl_saved_base()==NULL`, `can_recover_cancel==true`,
   buffer stays `modified`, retained original/token kept for
   `savectl_recover_cancel` with a self-contained snapshot
   checkpoint; disk keeps `"foreign"`, buffer keeps `"old!"`.
5. Variants: (a) no-journal controller, same hook → v2 must still
   surface external-modified (verify pathname vs fd-captured id
   unconditionally); (b) control: foreign replace after
   needs_finish, before `savectl_finish` → finish must fail
   BASE_CHANGED and stay recoverable (already true in v1);
   (c) post-cutoff typing: edits accepted after the rename must
   survive in the CURRENT checkpoint under conflict-cancel
   (PLAN §4.4 "preserves post-cutoff edits").

## Bug 3 — `LINEIDX_EDIT_MAX_CHUNKS` delete+undo above the incremental cap

**Status at 6fec2e8: STILL PRESENT.** `src/lineidx/` is
byte-identical `c95b6eb..6fec2e8`; the only `src/editor/input.c`
change in that range is `1229669` (edit-b6ai.18, wrapped goto
clamp), unrelated to the admission path below.

### Derivation of the threshold (Law 4)

- `LINEIDX_CHUNK 65536u` — `src/lineidx/lineidx.h:35` (P: one
  16-byte index entry per ≤64 KiB chunk, header lines 3–4).
- `LINEIDX_EDIT_MAX_CHUNKS 512u` — `src/lineidx/lineidx.h:38`.
- Admission, `plan_edit` in `src/lineidx/lineidx.c:836–861`:
  `pieces = ceil(newlen / LINEIDX_CHUNK)` (line 855) where
  `newlen` is the *repacked touched range* after the edit
  (`end - first.byte - del + ins_len`, line 854, with adjacent
  short chunks absorbed, lines 843–852); refusal iff
  `pieces > cap - kept || pieces > LINEIDX_EDIT_MAX_CHUNKS`
  (line 858).
- So the MAX_CHUNKS veto fires iff `newlen > 512 × 65,536 =
  33,554,432 B = 32 MiB` (P). `newlen == 32 MiB` exactly gives
  `pieces == 512`, admitted by this term (the fixed-capacity term
  `cap - kept` can still refuse: capacity is fixed at create —
  `lineidx_create` reserves the open length's chunks + 64,
  header lines 67–69 — and no foreground path ever grows it).

### Root cause: the index vetoes the edit, asymmetrically

A large **delete** is admitted: `newlen` collapses to the
residual bytes of the boundary chunks. Its **undo** is an insert
of the deleted range: `newlen ≈ ins_len`, so any deleted range
over 32 MiB makes undo inadmissible. The veto is enforced in the
editor before any mutation:

- `check_index_edit` (`src/editor/input.c:376–382`) maps
  `lineidx_edit_check` failure to `EDITOR_ERR_CAPACITY` +
  `rejected_commands++`.
- `replay_history` (undo/redo entry, `src/editor/input.c:712–724`)
  checks the *whole* delta first (line 718:
  `check_index_edit(e, d.off, redo ? d.old : d.add, redo ? d.add : d.old)`);
  on failure it returns before the sliced replay
  (`undo_undo_slice`, line 695) ever runs, and `history_cursor`
  never moves — every retry refuses identically.
- The same whole-delta check gates ordinary inserts
  (`mutate_text:437`, paste/replace via `editor_replace_begin:456`)
  and `changed()` re-checks through `lineidx_edit` itself
  (`input.c:63`, failure → `EDITOR_ERR_HISTORY`).
- No editor path recreates or regrows the index on capacity
  refusal: `lineidx_create` appears only at open
  (`src/editor/buffers.c:280,332`) and reload/recovery
  (`src/editor/recovery.c:427`). The header admits the design gap:
  "Larger replacements need an index recreated for the resulting
  content on the allocating path" (`lineidx.h:131–132`) — but no
  caller does that recreation.

User-visible effect: delete a >32 MiB selection in a large file
(works), press Ctrl+Z (**correction, 2026-11-23, reproduced on the pinned build: the editor exits with status 1 and prints `sublimite: error -105`**, see docs/decisions/v2-he7.15.md and amendment A8; this note's original reading was "nothing happens, forever"), and the only
recovery is reload-from-disk — if the buffer is instead saved,
the accidental deletion becomes permanent. Redo of a >32 MiB
insert and paste/replace-all producing a >32 MiB contiguous
replacement fail the same way.

### Minimal repro as a test design

Two vehicles, deliberately: a **v2 unit test** against
`src/lineidx/lineidx.h` to pin admission arithmetic at the exact
boundary, and a **black-box contract test** for the user-visible
delete+undo sequence (the index-internal threshold is not
observable from outside, but "Ctrl+Z restores the bytes" is).

Unit (lineidx level, no editor):

1. `x = lineidx_create(40 MiB)` (so fixed capacity, 640+64 (P)
   chunks, cannot be the refusing term in step 2).
2. `lineidx_edit_check(x, 0, 33,619,968, 0)` — delete of
   513 chunks (`513 × 65,536`, i.e. 32 MiB + 64 KiB): expect 0
   (admitted; `newlen` = one residual boundary chunk at most).
   Apply with `lineidx_edit` → 0.
3. `lineidx_edit_check(x, 0, 0, 33,619,968)` — the undo insert:
   **v1 returns -1** (`pieces ≥ 513 > 512`: insert plus the
   residual boundary chunk it repacks with). Boundary control:
   insert of `33,554,432` (exactly 512 chunks, offset chosen so
   residual + insert ≤ 32 MiB) returns 0 in v1 — assert both
   sides of the boundary so the derivation, not a round number,
   is what's tested.
4. Capacity-term control: `lineidx_create(0)` then an insert of
   65 chunks' growth (65 × 65,536) — refused by `cap - kept`
   (64-chunk reserve) far below the 32 MiB term; v2 must not
   refuse it either.

Contract (binary level): open a generated 40 MiB fixture
(derived: > 33,619,968 B so a whole-file selection exceeds the
cap), Select All, Delete, Ctrl+Z, save-as/compare bytes.

**v1 does:** step 3 refused; contract Ctrl+Z is a no-op.
**v2 must (PLAN_V2 §4.3):** the edit is never vetoed by the
index. Over-cap edits put the index into a degraded state —
queries return `exact=false` estimates
(`lineidx_result`, header line 55) instead of stale exact
geometry — and an off-path rebuild on `WORK_BULK` against a
post-edit snapshot allocates a table sized to the post-edit
length (zero typing-path malloc, Law 2), swapped in only on
`(doc_revision, fault_epoch)` match. Test assertions: undo
succeeds and bytes match the model; during rebuild,
`lineidx_line_count`/`line_to_byte` return `exact=false`; after
poll drains, `exact=true` and counts match a naive scan; G1 /
malloc-guard instrumentation shows no allocation on the
edit/undo foreground path.

## Bug 4 — Wayland clipboard copy

**Status at 6fec2e8: FIXED** by `73beeb4` (edit-b6ai.9), the
direct parent of the pin; `git log 73beeb4..6fec2e8` is only the
session-30 worklog commit, so nothing reverted it. The contract
suite must record this row **GREEN-expected on the v1 oracle**,
not as an expected failure — PLAN_V2 §6's list predates the fix
(it was written against `c95b6eb`, where the bug is present:
`73beeb4`'s pre-image of `src/editor/clip.c` shows the broken
call).

### Root cause in v1 at c95b6eb

The editor's clipboard publish entry, `publish_owned`
(`src/editor/clip.c:106`), called the X11 backend directly on
POSIX:

```c
int rc = x11_clip_set_buf(&e->platform, which, b);   /* pre-73beeb4 */
```

`x11_clip_set_buf` (`src/x11/clip.c:373`) starts with
`x11_clip *c = CL(p)`, where `CL(p)` is
`x11_private(p) ? (x11_clip *)x11_private(p)->clip : NULL`
(`src/x11/clip.c:135`) and `x11_private` returns
`p->backend_state` **without checking the backend**
(`src/x11/private.h:31–32`). On a Wayland `plat`, `backend_state`
is a `wayland_state*`; reading it as `x11_plat` takes whatever
lies at the `clip` field offset as an `x11_clip*` — the v1
decision note words it as "reads Wayland state as X11 state"
(oracle `docs/decisions/edit-b6ai.9.md`). Best case
`PLAT_ERR_FAIL` (copy silently does nothing), worst case
dereferencing garbage. Either way no `wl_data_source` was ever
created, so Wayland clipboard *copy* (and cut's publish half)
never reached the compositor. Paste was unaffected:
`plat_clip_request` in `src/wayland/dispatch.c` already
dispatched by backend.

### The fix at 6fec2e8 (evidence)

- `publish_owned` now calls backend-neutral `plat_clip_set_buf`
  (`src/editor/clip.c:130`); it is the only editor caller
  (`grep plat_clip_set_buf src/` → declaration
  `src/x11/clip.h:59`, definition `src/wayland/dispatch.c:105`,
  this call site).
- `plat_clip_set_buf` dispatches on `p->backend`
  (`src/wayland/dispatch.c:105–109`): Wayland →
  `wayland_plat_clip_set_buf`, else the unchanged
  `x11_clip_set_buf`.
- `wayland_plat_clip_set_buf` (`src/wayland/wayland.c:841–851`)
  validates `which ∈ {PLAT_CLIP_CLIPBOARD, PLAT_CLIP_PRIMARY}`,
  treats PRIMARY-without-the-global as best-effort OK, and calls
  `claim` (`src/wayland/wayland.c:810–840`), which creates a
  `wl_data_source` (or `zwp_primary_selection_source_v1`),
  offers `text/plain;charset=utf-8` + `text/plain`, and claims
  the selection with the last input serial
  (`wl_data_device_set_selection(..., s->serial)`), adopting the
  editor's buffer with no extra copy.
- Residual edges (not this bug): `claim` fails when
  `!s->serial` (no input serial yet — same precondition the
  older `wayland_plat_clip_set` had); receive still accepts only
  the two `text/plain` types. Both are noted in the worker
  report (oracle `docs/worker-reports/edit-b6ai.9-s29.md`).

### Minimal repro as a test design

Vehicle: **black-box contract test** at the compositor boundary
— the behavior (a data source appears, offers the two mime
types, serves the copied bytes on `send`) is exactly what a
compositor observes, and v1 already has the fixture pattern
(headless mock compositor, oracle `tests/wayland_mock.h`;
`primary_plat_test` / `primary_editor_test` in
`tests/wayland_test.c:604,708`, with the worker report recording
the pre-fix red assertion at `wayland_test.c:613`).

1. Start the editor (contract harness, Wayland backend forced)
   against the mock compositor advertising `wl_data_device` +
   `zwp_primary_selection_device_manager_v1`; deliver a seat
   serial (any key/pointer event).
2. Open a fixture, select a known string, Ctrl+C.
   **c95b6eb v1:** mock sees no data source (editor called into
   X11 state). **6fec2e8 v1 / v2 must:** mock receives a source
   offering both mime types; mock requests the fd and reads the
   exact selected bytes. Cut (Ctrl+X) repeats the same publish
   and also deletes the selection only after publication.
3. Selecting text claims PRIMARY; mock middle-click paste
   delivers it back at the caret.
4. Absent-global variant: compositor without the primary
   manager — Ctrl+C still succeeds (PRIMARY publish is
   best-effort OK), CLIPBOARD round-trips.
5. Paste direction control: mock owns the selection, Ctrl+V
   inserts its bytes (already worked pre-fix via
   `plat_clip_request`; keep it green).

Verification level to record in the suite: all of the above is
mock-verified in v1 ("Not tested against a real compositor",
worker report; v1 worklog 2026-11-03 puts the real-Hyprland
checklist on edit-b6ai.5, still open per oracle `HANDOFF.md`).
The v2 Wave 2 gate (PLAN §6: "Wayland clipboard copy/paste" on
real Wayland) is where the real-compositor run belongs; do not
mark this row XFAIL in the meantime — it is GREEN against the
mock and unproven only against a live compositor.

---

## Summary for the contract suite

| # | Bug | At 6fec2e8 | Suite marking |
|---|---------|------------|---------------|
| 1 | 17-leaf piece root collapse | Not confirmed; no v1 artifact; suspect invariants read sound | Deciding probe first (unit, piece.h); mark from its result |
| 2 | Post-rename save conflict | Present (`savectl_tick` installs foreign BASE + needs_finish) | XFAIL on v1, must-pass on v2 (unit, savectl.h + step hook) |
| 3 | LINEIDX delete+undo >32 MiB | Present (index vetoes undo; threshold = 512×64 KiB (P)) | XFAIL on v1, must-pass on v2 (unit boundary + black-box Ctrl+Z) |
| 4 | Wayland clipboard copy | Fixed by 73beeb4 (mock-verified; real compositor open) | GREEN on v1 (mock); real-compositor run = Wave 2 gate |

VERDICT: Bugs 2 and 3 are confirmed live at 6fec2e8 with symbol-level traces and deterministic test designs; bug 4 was fixed by 73beeb4 and must be recorded GREEN (mock), not XFAIL; bug 1 could not be confirmed from v1 source or history — its deciding probe, not an assumed XFAIL, is the deliverable. File: docs/decisions/wave0-known-bugs.md (this file only; nothing else edited, v1 not built or run).
