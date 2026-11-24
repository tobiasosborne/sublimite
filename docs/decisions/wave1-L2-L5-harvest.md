# Wave 1 — L2 (`libpersist.a`) and minimal L5 (`src/app/`) harvest dossier

> Research dossier, written 2026-11-23 against v2 `c16bfc2` (branch `v2`) and the v1 oracle pinned at `6fec2e8`
> (`/home/tobias/Projects/sublimite-v1-oracle`, read-only). It is the input for the coordinator's L2 and L5 worker
> briefs. Format follows [wave1-L1-harvest.md](wave1-L1-harvest.md) and [wave1-L0-harvest.md](wave1-L0-harvest.md).
>
> **Conventions.** Citations are `FILE:LINE` plus symbol. Lines are at `6fec2e8` for v1 paths (prefix `v1:`) and at
> `c16bfc2` for v2 paths (prefix `v2:`). The undo candidate is `review/undo` at `9edcb41` (prefix `undo@9edcb41:`).
> Line counts are `wc -l` at those commits (M, command `wc -l <files>`, no load or power stamp because nothing was
> timed). Every other number carries (P), (M), (E) or (G) and its derivation. Nothing was built, run or timed. v1 was
> only read, never built. `bd` was not run, so the text of bead `v2-0fv.15` was **not** read. Its acceptance lines
> are quoted from the amendments and `HANDOFF.md`, where they are restated.
>
> **Sequencing (A10).** L2 and minimal-L5 beads wait for the Wave 0 gate
> ([wave0-plan-amendments.md](wave0-plan-amendments.md) A10). Two harder dependencies also block them:
> - piece 7b (`v2-0fv.17`, the real tree behind the frozen `v2:src/piece/piece.h`);
> - the undo landing (`v2-0fv.8`, currently HOLD with three MUST-FIX findings,
>   [wave1-undo-astra.md](../reviews/wave1-undo-astra.md)). Its MUST-FIX 2 is the edit/undo transaction boundary that
>   the edit bead must own (§4.5).
>
> Before writing any brief, read §0 (cross-cutting) and §6 (open derivations). Six of the questions in §6 change the
> journal wire format or the savectl FSM. A worker must not decide them.

## 0. Cross-cutting facts

### 0.1 Layer and module order for L2/L5, and the edges v1 has that v2 forbids

The v2 rules are:
- `v2:tools/check_dag.py:19-36` `INTRA_RULES` contains `"file < journal < savectl"`, `"journal < edit"` and `"app < main"`.
- L2 may include only L0, L1 and L2.
- L5 never reaches `x11`, `wayland`, `gl` or `raster` (`L5_BACKENDS`, `v2:tools/check_dag.py:40`).
- `v2:tools/system-symbols.txt` grants kernel-entering imports (`openat`, `renameat2`, `fsync`, `ioctl`, …) to `os` only
  ([tools/README.md](../../tools/README.md), `check_symbols.py` row).

That last rule is new relative to v1, and it is the headline structural fact for `file`: v1 `file` makes direct syscalls
that v2 forbids outside `os`.

| v1 edge or call | v1 site | v2 verdict |
|---|---|---|
| `file → piece, work, os` | `v1:src/file/file.h:70-73` | Allowed (L2 → L1/L0). The calls change (§1.2). |
| `file` → raw syscalls `inotify_*`, `fchown`, `fgetxattr`/`flistxattr`/`fsetxattr` | `v1:src/file/file_posix.c` (inotify :29-33, 77, 153; `fchown` :290-291; xattrs :212-261) | **Forbidden** (only `os` enters the kernel). Needs new `os` seams: L2-0 (§7). |
| `savectl` → `<sys/stat.h>` | `v1:src/savectl/savectl.c:6` | Forbidden for the same reason. Use `os_stat`. |
| `file` reads work-pool internals: `pool->slots[h.slot].busy/epoch`, `.job.generation`, `pool->n_workers`, `n_bulk`, `m->slot_`/`epoch_` | `v1:src/file/file.c:206-211, 488, 504-516` | Not a layer edge, but it breaks `work`'s encapsulation. v2 has `work_handle_finished` (`v2:src/work/work.h:191`) and `work_mailbox_receive` (:240-242). Use them. |
| `file` downcasts `piece_map_hooks.ctx` by comparing function pointers | `v1:src/file/file.c:33-37` `file_snapshot_backing` | Deleted. v2 `piece_view_backing` (`v2:src/piece/piece.h:221`) returns the `piece_backing` with its `os_fault_id` (:159-166). |
| `journal → file` (`file_id` only) | `v1:src/journal/journal.c:2`, used only in `journal_base_from_prefix` (:374-387) | Allowed by `file < journal`. Simpler is to build `journal_base` from `os_stat` and drop the edge (§2.3). |
| edit kernel lives in `editor/input.c` and calls layout, scroll, palette and frame | `v1:src/editor/input.c:42-83` `changed` (`editor_scroll_edit` :67, `editor_palette_close` :70, `editor_begin_frame` :71, `layout_edit` :73-81) | **Upward from L2**. The edit kernel reports a change record. L4/L5 consume it (§4.3). |
| `view/edits.c` → `layout/layout.h` | `v1:src/view/edits.c:2`, used only by `soft_row` :216-235 (`layout_visual_row` :221, :225) | Upward from L2. The two soft-row commands take the row bounds as arguments (§4.3). |
| `savectl` drives the journal's private pool from a worker thread | `v1:src/savectl/savectl.c:345-362` (OP_SAVE on the bulk worker calls `journal_save_prepare`), and `v1:src/journal/journal.c:1115-1141` `retain_base`, which submits on `j->pool` and spins `work_mailbox_drain` + `delay` (1 ms, :62) until `r.received` | **Violates v2 `work`'s contract**: "Pool lifecycle, wake access, submit, cancel and drain: UI thread only" (`v2:src/work/work.h:2-4`). The same shape occurs in `publish_worker` → `journal_flush` (`v1:src/editor/session.c:396-410`, `v1:src/journal/journal.c:849-862`). §6 Q7. |

### 0.2 PLAN line citations re-resolved (L2/L5-relevant)

PLAN cites v1 lines written against `c95b6eb`. At `6fec2e8` they resolve as follows.

| PLAN citation | Symbol at `6fec2e8` | Verdict |
|---|---|---|
| §4.2 `input.c:108` (staging emission) | `editor_journal_staged`, `v1:src/editor/input.c:108-170` | Holds. |
| §4.2 `input.c:404` (preflight before mutating) | `mutate_text`, `v1:src/editor/input.c:404`: `if (e->journal && (e->op_count + 3 > EDITOR_STAGE_OPS \|\| n > EDITOR_STAGE_BYTES - e->stage_used))` | Holds, but the preflight covers **only** staging capacity. There is no piece or undo preflight and no checkpoint. A refusal after the first part is kept as a partial edit (`prefix_error`, :368-375). §4.4. |
| §4.2 `journal.h:274` ("retain group progress across turns") | `journal_insert_step` comment, `v1:src/journal/journal.h:274-292` (progress across calls, :285-289) | Holds in substance. The phrase is PLAN's own. |
| §4.2 `journal.h:255` ("never roll back after RAM acceptance") | `journal_append` comment, `v1:src/journal/journal.h:250-262`: "Do not retry that edit; it was accepted into RAM" | Holds in substance. |
| §4.2 `editor.c:430` (frame fence) | **Moved.** :430 is `platform_complete`. The fence is the first condition of `submit`, `v1:src/editor/editor.c:625`, which returns 0 while `e->op_count` is nonzero, with `JOURNAL_BUSY` refusals at :626-628 and :634 | Re-cite as `editor.c:625` `submit`. |
| §4.2 `dataloss_test.c:436` (kill oracle) | There is no oracle at :436. The area is the end of `hot_exit_mixed` (:413). Oracle structs are at :36, :378 and :447. `kill_at_step`/`killed_mid_save` are at :204-239 | Re-cite as `v1:tests/dataloss_test.c:204-239` (save steps) and :443-498 (`torn_undo_group`). |
| §4.4 `savectl.h:99` (lease returned on PREPARING) | `savectl_save` comment, `v1:src/savectl/savectl.h:99-100`: "returns journal ownership BEFORE file-only writing starts" | Holds. |
| §4.1 `session.c:142` | `publish`, `v1:src/editor/session.c:123-151` (leaf allocation :142-148). A4 already notes that two retainers hide behind this line | Holds as A4 states. |
| A8 `checkpoint.c:124` (DELETE in checkpoint) | Trailing `JOURNAL_DELETE` for the unkept BASE tail in `editor_text_ops`, `v1:src/editor/checkpoint.c:124` | Holds. |
| A8/A9 `editor.c:1064–1086`, `:1376`; `input.c:718`, `:1281`; `main.c:342–344` | `queue_pick` :1064-1086; `if (rc) return fail(e, rc);` :1376; `replay_history` :712-724; `main` exit :341-344 | Hold. |

### 0.3 Globals, Windows and `#include "*.c"` — L2/L5 sweep

**Globals.** v1 L2 has one mutable global: `file_watches` (the inotify service), `v1:src/file/file_posix.c:27`. It is
whitelisted by v1's own `v1:src/file/check_signal_globals.py:18-21`. v2 CLAUDE.md admits globals only where PLAN names
them (trace ring, allocator hook, `os_fault_service`). **Proposal (push-back):** the watch service becomes an owned
object, created by `app_buffers` and passed to `file`. Its single-UI-owner, lock-free, allocation-free design
(`file_posix.c:13-17`) needs no process-global reach. The simplest alternative, a global with a decision note, loses
nothing functionally. It is rejected because it adds a fourth global with no counter-example requiring one. `journal`,
`savectl` and `view/edits.c` have none (journal: only `static const` tables, `journal.c:927`, `retire.c:83,100,260,361`,
`session.c:13`).

**Windows.** None of this lands (L0 dossier shared block). Strip:
- `v1:src/file/file_win.c` (622 lines), plus `_WIN32` blocks at `file.c:855,861,936,968`, `file_internal.h:128`,
  `file.h:289`.
- `v1:src/journal/journal_win.c`, plus blocks at `journal.c:530-540,965-969`, `private.h:30-34,40-42`, `journal.h:434-442`.
  `journal_posix.c` keeps only what survives without int-fd wrappers.
- `v1:src/savectl/savectl.c:117-121,128-143,208-212,277-281`.
- `v1:src/editor/wait_win.c`. `editor.c:1405-1412` is Windows-only close.

**`#include "*.c"` in the v1 test estate for these modules.** Each needs a rewrite (public headers + layer archive +
`-Wl,--wrap` / `<MODULE>_TEST_HOOKS`, CLAUDE.md):

| Test | Include | What it reaches |
|---|---|---|
| `v1:tests/file_acquire_test.c:11` | `../src/file/file.c` | Static `prefix_job`, `open_job`, `struct file` fields, `workers.slots[0].epoch` |
| `v1:tests/file_fault_test.c:13` | `../src/os/os_posix.c` | OS fault internals only. Superseded by v2 `os_fault_test` (`v2:Makefile:191`) |
| `v1:tests/file_kill_test.c:18` | `../bench/file_bench.c` | Bench statics |
| `v1:tests/savectl_test.c:185` | `../src/savectl/savectl.c` | Only a `#define work_publish` rewrite (:184) to hold publication. It becomes `-Wl,--wrap=work_publish`. It also defines libc `stat`/`openat`/`fsync`/… directly (:23-74). These become `--wrap` on the `os_*` seam |
| `v1:tests/journal_safety_test.c:28` | `../src/journal/journal.c` | Macros rename `os_file_pwrite`, `os_file_sync_data`, `os_file_sync` and `os_dir_sync` (:24-27). These become `--wrap` |
| `v1:tests/journal_cancel_test.c:16` | `../src/journal/journal.c` | `retain_generation`, `journal_retention`, `work_slot` internals |
| `v1:tests/journal_ack_test.c:20` | `../tools/journal_replay.c` | A CLI `main`. Becomes a public-API test |
| `v1:tests/editor_recovery_test.c:39` | `../src/editor/recovery.c` | `materialize_base` via an `os_file_read` rename (:38-40) |

`v1:tests/file_test.c` defines `__wrap_*` under `FILE_TEST_WRAP`/`FILE_REVIEW_WRAP`/`FILE_SEMANTICS_WRAP` (:659, :1218,
:1643, :1681). Nothing defines those macros and nothing links `--wrap` for `file_test`, so **those cases are compiled out
of v1's default build**. Never treat them as evidence that v1 ran them. The v2 port must make them fail closed: a
registered wrap that a test asserts actually fired.

### 0.4 What the contract suite needs from L2/L5

Sources are `v2:tests/contract/expectations.json` and the ledger in
[wave0-contract-design.md](wave0-contract-design.md) §"Coverage ledger". `first_required_wave` values (M, read from the
manifest) are:
- 2: R1, R5, R6, R31, X3;
- 3: R3, R4.

The `v2` target binding is `null` until the Wave 2 native inspector ("a v2 run is an ERROR until then", manifest
`targets.v2.note`). **No contract case can score v2 in Wave 1.** The Wave 1 obligations are therefore the headless
public-header tests that the ledger names for W1. These must exist and pass in Wave 1 so the W2/W3 GUI reruns can
pass on v2:

| Ledger row | W1 owner here | What L2/L5 must make true |
|---|---|---|
| R31 (W1 fault/recovery) | journal, savectl, app_session | Pre-ACK kill recovers pre-edit **or** whole-edit, never torn. Post-ACK requires whole-edit, untitled included. fsync, dirsync and retention retirement are wrapped separately (ledger text; PLAN §4.2 oracle). |
| X2 (W1 savectl/file) | file, savectl | Post-rename foreign replacement never yields a finishable state (§3.5, known-bugs Bug 2). |
| X3 (W1 edit/lineidx; lineidx now piece per A19) | edit, app_buffers | Delete a 33,619,968 B mapped file, Undo, Save As, then the bytes compare equal. 33,619,968 = (512 + 1) × 65,536 (P, A8). |
| R10 (W1 headless) | file, savectl, app_save | Edit and save the same large acquired file twice. Both successive exact disk generations are correct. |
| R4 (recovery kernel W1, GUI W3) | app_session | Partial restore under capacity puts unsaved tabs first (v1 `plan_partial`, §5.3). |
| R17, R18, R29 (W3) | app_loop, app_save | Undo after Save As; save of a small tab while a large one opens; input during a pending open. **A9's in-order rule** governs all three (§5.1). Building app_loop against it in W1 costs nothing extra. |

## 1. `file` — `src/file/`

PLAN §3: "harvest + targeted fix (`NOREPLACE` absent target, unified fault-pinned reload)". PLAN §2 adds
"existing file_id with ctime_ns + ino_hi". Amendment A5 replaced "pinned" with the generation-validated fault id and no
pins. The [fault-lease spec](wave1-os-fault-lease.md) §"L2 and L1 integration" is the signed acceptance for the `file`
half of the fault service.

### 1.1 Files and counts (`wc -l`, M) and disposition

| File | Lines | Disposition |
|---|---|---|
| `v1:src/file/file.c` | 1235 | Harvest + fix (§1.5) |
| `v1:src/file/file.h` | 353 | Rewrite the surface: drop backing/snapshot helpers and watch globals; take `piece_snapshot` (§1.2) |
| `v1:src/file/file_internal.h` | 135 | Harvest; collapse the job-flag triples (§1.5(e)) |
| `v1:src/file/file_posix.c` | 294 | **Move** the syscalls into `os` (L2-0). The remainder (temp naming, watch bookkeeping) stays in `file.c` |
| `v1:src/file/file_test.h` | 14 | Becomes `FILE_TEST_HOOKS` (CLAUDE.md, `v2-0fv.16` pattern) |
| `v1:src/file/file_test_helpers.c` | 46 | Becomes a `file_test_hooks.o` variant |
| `v1:src/file/file_win.c` | 622 | Delete |
| `v1:src/file/check_signal_globals.py` | 22 | Delete. v2's `check_symbols.py` covers it |
| `v1:src/file/STATUS.md` | 203 | Read only |

v1 functions by role, from `file.h` (§1.2) and `file.c`:
- acquisition: `prefix_job` :294-383, `open_job` :385-449, `file_sync` :742-820, `acquisition_validate` :275-291;
- mapping lifetime: `map_acquire` :20, `map_release` :21-31, `guard_faults`/`guard_faulted` :16-17, `map_change`
  :621-636;
- save: `file_save_begin` :1193-1235, `save_job` :1071-1111, `save_commit_pump` :1117-1126, `save_commit_job`
  :1128-1141, `transaction_prepare` :923-982, `transaction_commit` :984-1033, `metadata_capture` :880-903,
  `metadata_apply` :904-921, `transaction_discard` :870-878;
- close: `file_close` :537-564, `wait_job` :504-516;
- identity: `file_id_from_stat` :230-244, `file_id_diff` :246-258, `file_id_stat_path` :260-269.

### 1.2 Public API to carry, and what changes

| v1 (`file.h` line) | v2 |
|---|---|
| `file_open_begin` :207, `file_open_ready` :235, `file_prefix*` :224-231, `file_size`, `file_open_mode`, `file_path`, `file_errno`, `file_msg_decode` :204, `file_prefix_scan` :131 | Keep. The open is two worker jobs, prefix then open (`prefix_job`, `open_job`). The prefix job decides COPY or MMAP by `size < threshold \|\| size == 0` (`file.c:320-321`). The threshold is `min(FILE_COPY_CAP = 256 MiB, phys/32)` (`file.c:10, 137-142`, (G) v1 policy). |
| `file_attach(file*, piece_tree*)` :237, which calls v1 `piece_init_mapped` with `{f->map, map_acquire, map_release}` (`file.c:591-592`) | Becomes **`file_open_tree(file*, piece_reserve*, piece_tree**)`**. It calls `piece_open_copy` (`v2:piece.h:183`) or `piece_open_mapped` (:193) with a `piece_backing {bytes, len, fault, ctx, acquire, release}` (:159-166). v1's COPY branch wraps the prefix in a fake `file_map` with slot −1 (`file.c:584-590`). That disappears, because `piece_open_copy` takes the bytes and the caller frees them on return. |
| `file_snapshot_backing` :98, `file_backing_acquire/release/faulted` :99-101, `file_snapshot_faulted` :102, `file_snapshot_bytes` :109 | **Delete.** Callers use `piece_view_backing` (:221) plus `os_fault_poll` (`v2:os.h:364`) with the single closing poll (A19 last bullet; `piece.h` FAULTS :461-476). |
| `file_check` :264, `file_changed` :265, `file_resolve_keep` :273 | Keep. `guard_faulted` becomes "`os_fault_poll` != OK or epoch ≠ 0", per the fault spec's L2 row. |
| `file_source_*` :244-259 (retained identity/guard lease for savectl) | Keep. It is the savectl validator source. The fault-spec row "`file_source_acquire` … held through `file_source_validate`" stays. |
| `file_save_begin(file*, piece_tree*, …)` :312 takes the **live tree** and calls `piece_snapshot_take` inside (`file.c:1201`) | Takes a **`piece_snapshot *`** owned by the caller (app_save/savectl). The capture belongs to the caller's cutoff (piece-pool ownership table, row "Saved text, fd-captured BASE and in-flight save"). |
| `file_save_write` :348 (synchronous prepare+commit) | Keep. It is the worker core. |
| `file_set_step_hook` :351, `FILE_STEP_*` :319-326 | Keep the six steps. Compile the hook only under `FILE_TEST_HOOKS`. Firing points are `TEMP_CREATED` :951, `MID_WRITE` :962/:967, `TEMP_WRITTEN` :975, `FSYNCED` :978, `RENAMED` :1021 and `DIR_SYNCED` :1026 (`file.c`). |
| `file_watch_*` :287-308 (global service) | An owned `file_watch` object (§0.3), on the L2-0 `os` watch seam. |
| `file_id` :142-151 `{dev, ino, size, mtime_ns, mode, exists, ctime_ns, uid, gid, metadata_valid, type, ino_hi}` | PLAN wants ctime_ns + ino_hi kept. `ino_hi` maps to `os_identity.index[1]` (`v2:os.h:164-167`), which is 0 on Linux. Open question §6 Q14: keep `file_id` or collapse it onto `os_stat`. Note that savectl's private `identity()` omits `ino_hi` (`v1:savectl.c:107-113`). A second identity comparison is a v1 duplicate; v2 has one. |

### 1.3 Dependencies, upward edges, globals, Windows, test `.c` includes

The fixes are in §0.1. These new `os` seams are needed (L2-0); none exist in `v2:src/os/os.h`, checked by grep for
`inotify|xattr|chown|link`:
1. A directory-change watch: init, add, remove, read events. The fd is borrowed into `os_waitset` (`v2:os.h:464`).
2. Metadata copy from fd to fd: owner (fchown) and the xattr set, including ACLs and security labels. v1's logic,
   including "remove inherited xattrs absent on the source" and "tolerate a matching immutable label", is
   `v1:file_posix.c:184-261, 285-293`.
3. Possibly `os_link_at` (A14(b) option; §6 Q2).

v2 already has, and `file` should use:
- `os_rename_at(…, OS_RENAME_NOREPLACE)` (`v2:os.h:269-271`), which returns `OS_E_EXIST`/`OS_E_NOTSUP`;
- `os_file_clone` (:219), `os_file_open_anon` (:256), `os_map_file` (:317), `os_fault_register/poll/retire` (:356-368);
- `os_waitset` (:460-477).

### 1.4 Acceptance lines that already exist

- **PLAN §6 Wave 1 exit.** "post-rename foreign-file conflict test → `SIGKILL` at every save step + failed fsync/dirsync →
  restart and verify exact pre-ack/post-ack recovery oracle". The file half is the step hooks and the wrapped
  fsync/dirsync.
- **Fault spec, "L2 and L1 integration" row `file` and reload.**
  - `open_job` registers before publication.
  - The ID is stored in the backing.
  - Change, save and source checks use a validated poll while the backing is held.
  - The savectl reload map goes through the same construction and finalization.
  - On failed registration the unpublished map and fd are disposed; on a later failed handoff the acquired reference is dropped.
  - Final release retires successfully before unmapping.
  - A reload builds a new registration.
  - Private heap copies use the absent identity.
- **Fault spec, "Implementation acceptance tests".** Test the integration through actual entry points:
  - block a running job, cancel it, release it, and assert the backing survives to physical acknowledgement and is
    retired exactly once;
  - for the finalizer invariant, the release child sees zero unmaps/frees of that backing;
  - the Law 2 release guard is active with zero allocations over snapshot take/retain, token retain/poll/drop and
    foreground queries.
- **Piece-pool spec (i), "What a mapped ORIGINAL guarantees"**
  ([wave1-piece-pool.md](wave1-piece-pool.md):99-105).
  - `file` keeps v1's detection: `map_change`, `check_now`, `file_check`, `file_changed`, `file_resolve_keep`, plus
    identity and timestamps for a same-size in-place overwrite, which raises no fault.
  - "Never adopt a handler's substitute zeroes as deleted text."
  - Row "Source conflicts, rotation and history lifetime" (:206): overwrite the mapped inode in place without a SIGBUS
    and truncate with a real fault. A captured short literal still holds its old byte, and a stale long span and live
    ORIGINAL take the same conflict path.
- **Undo review MUST-FIX 3** ([wave1-undo-astra.md](../reviews/wave1-undo-astra.md)). The "live backing identity check" is
  `file`'s. Undo now requires it for a mutable mapped document. `file` must therefore export a validator that "must
  neither edit nor block" (`undo@9edcb41:src/undo/undo.h:45-49`) and runs inside the Law 2 window.
- **Known bugs, Bug 2, variant (a)** ([wave0-known-bugs.md](wave0-known-bugs.md):246-251). Verify pathname against the
  fd-captured identity unconditionally, with or without a journal.

### 1.5 v1 structure `file` must NOT carry

- **(a) The absent-target race.** `transaction_commit` always renames with `OS_RENAME_REPLACE`
  (`v1:file.c:1006`); there is no `NOREPLACE` anywhere in `src/file` or `src/savectl` (grep). An absent target is
  checked only by comparing absent with absent in `file_id_diff` (`file.c:249-250`) at :994-999 and :890-891. A file
  created between that check and the rename is silently overwritten. v2 uses `OS_RENAME_NOREPLACE` when the expected
  identity is absent. `OS_E_EXIST` maps to `FILE_ERR_CHANGED`; `OS_E_NOTSUP` (filesystem without atomic exclusion)
  needs a stated fallback in the bead decision.
- **(b) UI spin on close.** `file_close` cancels and then `wait_job` sleep-spins (`os_sleep_ns(50000)`,
  `v1:file.c:514`) on work-pool internals for six job kinds (:544-549). v2 replaces this with a non-blocking two-phase
  close: begin, then `work_handle_finished` per handle (PLAN §4.7 "zero `sched_yield` spins"; work.h:191).
- **(c) Worker publish retry.** `post` retries `work_publish` with 1 ms sleeps while `!work_should_stop`
  (`v1:file.c:176-193`). It is legitimate backpressure on a full mailbox (`WORK_MAILBOX_CAP 256`, `v2:work.h:26`,
  (G)), but the sleep is a fixed (G) with no derivation. The bead states its bound or uses `work_continue`.
- **(d) Downcast.** `file_snapshot_backing` compares function pointers (`file.c:33-37`). Deleted (§1.2).
- **(e) Coordinating flag triples.** Every job kind carries `_fin` (atomic, worker-written), `_ready` (atomic) and
  `_installed` (UI) (`v1:file_internal.h:46-118`, initial values `file.c:469-484`). "Save busy" is
  `!save_installed || !save_fin || !prepare_fin || !abort_fin` (`file.c:1182-1184`). Together with `commit_pending`,
  `save_aborted` and `abort_pending` (:105-108), the save path alone has seven booleans encoding one lifecycle. v2 uses
  one `-Wswitch-enum` enum per job slot, for example
  `{FILE_JOB_IDLE, FILE_JOB_QUEUED, FILE_JOB_RESULT_READY, FILE_JOB_INSTALLED}`. The worker publishes the result through
  the mailbox; the atomic-ready + installed pair duplicates that message.
- **(f) Save input is the live tree.** `file_save_begin` takes `piece_tree*` (§1.2). The caller's snapshot is the save
  cutoff.
- **(g) Post-rename identity loss.** If `fstat(observer)` fails after the rename, the error path clears
  `metadata_valid` (`file.c:1014-1017`). savectl then keeps a stale baseline against the renamed-in file (§3.5). v2
  treats an unverifiable post-rename identity as `CONFLICT_CANCEL`, never as plain FAILED with a stale baseline.
- **(h) Unregistered reload map.** The reload mapping in `savectl.c:268` is not registered (`grep os_fault
  savectl.c` → 0). Since the fault spec requires it, reload acquisition **moves into `file`** (it owns mappings) and
  savectl orchestrates it. This also removes savectl's second open/copy/map code path (`reload_worker`, savectl.c:195-294).

### 1.6 Tests to port (counts M)

| v1 test | Lines | Notes |
|---|---|---|
| `tests/file_test.c` | 2311 | Public API apart from the compiled-out wrap blocks (§0.3). Port the cases listed in the agent census: `t_save_*`, `t_durability`, `t_sigbus`, `t_review_*`, `t_semantic_*`, the latency group as Law-2/G1 cases |
| `tests/file_os_cases.inc` | 260 | Port |
| `tests/file_acquire_test.c` | 87 | Rewrite off `.c` (`--wrap=pread,--wrap=os_file_open_at` already at `v1:Makefile:156`, the only `--wrap` in v1's Makefile) |
| `tests/file_kill_test.c` | 413 | Rewrite off `bench/file_bench.c` |
| `tests/file_fault_test.c` | 81 | Drop. The v2 os fault suites cover it |
| `fuzz/file_fuzz.c` | 291 | Port. Ops `fuzz_open/insert/delete/save/external/lifetime/cancel_save` |
| `bench/file_bench.c` | 650 | Port without its layout/raster/x11 includes (`file_bench.c:5-7`) |

## 2. `journal` — `src/journal/`

PLAN §2: "checked arena allocations, versioned wire header, CRC + committed-group replay". A14(a): "a versioned record
for a BASE-range restore plus the durable generation declaration; the v2 journal is never offered to a v1 parser". The
piece-pool spec, "Journal encoding and durable source lifetime" (:137-139) and "Wire and integration" (:145), is the
signed specification of the wire change.

### 2.1 Files and counts (M) and disposition

| File | Lines | Disposition |
|---|---|---|
| `v1:src/journal/journal.c` | 1331 | Harvest + fix (§2.5). The wire format is versioned anew (§2.4) |
| `v1:src/journal/journal.h` | 449 | Harvest the surface. Drop int-fd wrappers and hook docs for Windows |
| `v1:src/journal/retire.c` | 496 | Harvest (startup retirement/inventory). Fix unchecked allocation `retire.c:404` |
| `v1:src/journal/session.c` | 96 | Harvest (session lock files) |
| `v1:src/journal/manual.c` | 144 | **Defer.** Manual replay/acknowledge CLI (`tools/journal_replay.c`, 35 lines). It is not needed for Wave 1 recovery. `manual.c:82-87` uses `os_file_to_stdio`, which may allocate |
| `v1:src/journal/private.h` | 51 | Harvest minus `_WIN32` |
| `v1:src/journal/path.c` | 35 | Harvest |
| `v1:src/journal/journal_posix.c` | 24 | Delete the int-fd shims (`journal_hook_file/dir`, `journal_session_create/release/absent`). v2 callers use `os_file`/`os_dir` |
| `v1:src/journal/journal_win.c` | 5 | Delete |
| `v1:src/journal/STATUS.md` | 298 | Read only |

v1 functions by role:
- append path (UI): `journal_append` :264 (h), `cache_write` :604-625, `reserve_record` :599, `journal_insert` :655-658,
  `journal_insert_step` :692-694, `journal_append_protected`/`journal_delete_protected`;
- worker: `worker` :450-474;
- receive: `journal_receive` :811-814;
- flush: `journal_flush` :849-862 (blocking loop);
- rotation: `journal_rotate` :943-1027;
- save: `journal_save_prepare` :1219-1266, `journal_save_finish` :1267-1303, `journal_save_cancel` :1305-1331;
- retention: `retain_base` :1115-1141 (sync spin), `journal_retain_start` :1208-1216 and `retain_async_worker`
  :1157-1207, `retain_generation` :1082-1105, `retain_copy` :1052-1065;
- BASE: `capture_open` :334-363, `journal_capture_base_with_io` :364-373, `journal_check_base` :390-397,
  `journal_encode_base` :712-723;
- replay: `replay_source_records` :269-300, `replay_file_mode` :312-316, `journal_apply_piece` :409-417.

### 2.2 Public API to carry, and what changes

- **Open/close.** `journal_open`, `journal_open_checkpoint` (recovery-pending open), `journal_close`,
  `journal_close_discard_empty`, `journal_set_io` (the BASE-read hook used by tests). Keep. The v2 `journal_open`
  checks the file header version first (§2.4).
- **Typing path.**
  - `journal_append`, `journal_insert_step`, `journal_append_protected`, `journal_delete_protected`. Keep the
    contract at `journal.h:250-262` (no malloc, lock, sync or submission; one bounded `pwrite` per record; sticky IO
    retains the whole record).
  - `journal_insert` (the one-shot 1 MiB paste form, :264-272) is not needed once edit uses `insert_step`. Cut it unless
    a caller remains.
  - **New: `journal_restore_base_range(j, buffer_id, dest, generation_key, src_off, len)`**, emitting the 64 B record
    (§2.4).
- **Metadata records.** `journal_set_base`, `_view`, `_tabs`, `_window`, plus encode/decode helpers. Keep BASE,
  VIEW and TABS. WINDOW and PAINT are GUI records: PAINT (`journal.h:33-37`) names "a tab that died in its first paint",
  which has no meaning in the headless W1 editor. Define them in the W2 bead that needs them, not by default (push-back,
  "no fossils").
- **Worker I/O.** `journal_pump`, `journal_receive`, `journal_in_flight`, `journal_get_stats`. Keep. Drop
  `journal_set_message_handler`: it exists only because the journal owns a private pool whose mailbox carries others'
  messages (`journal.h:328-330`); see §6 Q7.
- **Rotation and save.** `journal_rotate`, `journal_save_prepare/finish/cancel`, `journal_retry`. Keep the semantics in
  `journal.h:169-202` (retained previous generation, SAVE PREPARED/FINISHED markers, retire after durable rotation).
  The retention step becomes the generation-retention mechanism of §6 Q2.
- **BASE.** `journal_capture_base[_with_io]`, `journal_check_base`, `journal_encode/decode_base`. Keep.
  `journal_base_from_prefix` takes `os_stat` + prefix instead of `file_id` (§0.1). **New:** a generation declaration
  record and its decode (§2.4).
- **Replay.** `journal_replay_bytes/file[_with_io]`, `journal_scan_file`.
  - `journal_apply_piece` changes:
    - INSERT becomes `piece_replace(t, off, 0, bytes, n)` (`v2:piece.h:294`);
    - DELETE becomes `piece_replace(t, off, len, NULL, 0)`;
    - the **new BASE-range record** becomes `piece_replace_spans(t, dest, 0, &span, NULL, 1)`, with `nl == NULL`
      ("journal replay passes NULL", `piece.h:317`; `v2-0fv.7.md:96`), but **only** after the generation key resolves
      to a verified source whose ORIGINAL is the tree's ORIGINAL. Otherwise the restore materializes bytes from the
      retained artifact (§6 Q3).
- **Retain/retire.** `journal_retire_directory`, `journal_acknowledge` (operator removal; defer with manual.c),
  `journal_retain_start` (async retention). Keep `retire_directory` for startup.
- **Session files.** `journal_session_create_file`, `_release_file`, `_absent_at`. Keep.

### 2.3 Dependencies, globals, Windows, test `.c` includes

- `journal.h:4-8` includes base, work, piece and os. `journal.c:2` includes `file/file.h` for `file_id` only (§0.1).
- No mutable globals. The CRC table is per instance (`journal.c:41,76`).
- The hardware CRC32C path (`crc_hardware`, :81-91, SSE4.2, `<nmmintrin.h>`) is selected at :75-79 and has an env
  override `EDIT_JOURNAL_PORTABLE_CRC`. CLAUDE.md forbids env-var feature matrices; the override is a fossil. Select
  through `base_cpu_has_*` (`v2:base.h:57-58`; add `sse42` if needed) and test both paths through a `--wrap` of the CPU
  predicate, as `v2:Makefile:225` does for find.
- Windows (§0.3). Test `.c` includes (§0.3).

### 2.4 The wire format: what v1 has, and what v2 changes (most of it is an open derivation)

These v1 facts come from the journal census; cites are in `v1:src/journal/journal.c` and `journal.h`.

- **No file header and no version.** A journal is a bare sequence of records, each starting with magic `0x314c4e4a`
  ("JNL1", :12). Schemas evolve by payload discrimination:
  - bit 31 of BASE `prefix_len` (`JOURNAL_BASE_V2`, h:43, c:127);
  - VIEW length 32/64 (c:141);
  - WINDOW length 8/12 (c:144);
  - SAVE phase (c:136).
- **Record header**, 32 B (P, (G) layout, `JOURNAL_HEADER`, h:11): magic u32, total length u32, type u32, CRC u32,
  sequence u64, buffer_id u64.
  - CRC32C (polynomial 0x82f63b78 reflected) runs over the header with the CRC field zeroed, plus the payload (c:101-109).
  - Non-PAD records have `seq == prev + 1` (c:159).
  - PAD seals each batch to the 4,096 B page (c:110-122, 153-157).
- **Types** (h:28-32): BASE=1, INSERT, DELETE, VIEW, TABS, WINDOW, SAVE, TARGET, GROUP_BEGIN, GROUP_COMMIT, PAINT=11.
  PAD=0 is private (c:13).
- **Unknown or invalid records**
  - An unknown type is invalid (`payload_valid` `default:`, c:146). The parser marks the source corrupt and stops (c:278).
  - **Non-readonly file replay truncates at the last valid byte and fdatasyncs** (c:315). This is why A14(a) and the
    piece-pool spec say "the v2 journal is never offered to a v1 parser" and "check its version before any
    repairing/truncating scan" (piece-pool spec :139).
- **Group atomicity.** A committed group is delivered only after its GROUP_COMMIT validates (c:285-295). An open group at
  EOF is dropped (c:278, 315).
- **Limits.** All are (G) v1 policy:
  - `JOURNAL_MAX_RECORD` 1 MiB (h:12);
  - `JOURNAL_DEFAULT_FILE_BYTES` 64 MiB (h:15), a log budget (rotation files may be larger, c:976);
  - `JOURNAL_DEFAULT_SYNC_BYTES` 65,536 and `SYNC_NS` 1 s (h:16-17);
  - `JOURNAL_INSERT_SLICE_BYTES` 16,384 (h:18), marked "(G) payload ceiling" in v1 `STATUS.md:30`.
- **BASE payload** (c:712-723): size, mtime_ns, inode, device, prefix_crc u32, prefix_len|V2 u32, NUL-terminated path,
  then ctime_ns. `prefix_crc` is CRC32C of the first ≤ 4,096 B (c:359). The maximum is 4,145 B (h:44). Derivation
  (P): 40 B fixed + 4,097 B path with NUL + 8 B ctime = 4,145.

v2 changes:

1. **File header.** Magic distinct from v1's "JNL1", a format version, and a feature word, checked by open, replay
   and scan **before** any truncating repair. An unknown version is refused and the file set aside, never truncated.
   This is (G) policy and needs Astra's ruling on layout (§6 Q5). The v2 namespace (`sublimite-v2` data dir, CLAUDE.md
   isolation rule) already keeps v1 from finding v2 journals. The header protects the reverse direction (W4 v1 → v2
   import) and future v2 versions.
2. **BASE-range restore record.** Layout as signed in the piece-pool spec (:137): 32 B header (type + buffer id) plus
   four LE u64 fields (destination, durable source-generation key, source offset, length) = **64 B** (G layout, P
   arithmetic: 32 + 4 × 8). It needs no group markers on its own; the group rule counts emitted records (spec :137).
3. **Generation declaration record.** Durable identity + location of a source generation, resolvable after restart
   ("a process fd, fault id, pointer or ADD offset cannot be replayed after restart", spec :137). Its schema is open
   (§6 Q1).
4. **Undo journaling changes from by-value to by-reference.** v1 stages undo as DELETE + a borrowed-tree INSERT
   re-read by value at drain (`v1:input.c:698-708`, `stage_tree` :98-106, drained at :134-148). The spec's cost for a
   one-span undo of L bytes by value is `L + 40 × ceil(L / 16,384) + 64` B (P, spec :119). For the 33,619,968 B
   fixture that is 33,702,112 B (spec table :113). By reference it is 64 B (spec table :129). Forward DELETE stays
   `(offset, length)` = 48 B on the wire. Derivation (P): 32 B header + 16 B payload, `journal_delete_protected`; spec
   :119.

### 2.5 Acceptance lines that already exist

- **PLAN §4.2 items 1-4.**
  - Preflight journal staging capacity before mutating.
  - Stage the group and keep group progress across turns.
  - Fence frame submission and acknowledgement while staged operations remain.
  - Never roll back after RAM acceptance.
  - On unrecoverable I/O, latch visibly suspended recovery without rewinding or duplicating.
- **PLAN §4.2 SIGKILL oracle.** Before ack: pre-edit or whole-edit. After ack: whole-edit. Failed fsync/dirsync and
  suspended recovery have distinct expected outcomes.
- **PLAN §6.** `make check-tsan` green on `journal`.
- **A14(a).** A versioned BASE-range record plus the durable generation declaration, never offered to a v1 parser.
- **Piece-pool spec, "Wire and integration"** (:145).
  - Version the new restore record, generation declaration, reader and checkpoint writer together.
  - Preserve CRC, group atomicity and the pre-/post-ack oracle.
  - Adopt a whole group or none.
  - Keep parser fixtures for prior supported records and rejection of unknown formats.
- **Piece-pool acceptance row "Source conflicts, rotation and history lifetime"** (:206).
  - Select All → Delete → Save empty → Undo → kill/restart with different old and new BASE generations.
  - Include missing/mismatched generation and artifact retirement races.
  - Test out-of-order/repeated checkpoint source runs, mixed BASE-reference/by-value replay, unknown journal version and
    torn groups.
- **Piece-pool (iii), "Contracts that change," checkpoint diff** (:143). Checkpoint writers can no longer assume ORIGINAL
  spans appear once in increasing order. That assumption is exactly `v1:checkpoint.c:109-114`; it lives in L5
  app_session (§5.3).
- **Contract ledger R31** (§0.4). Wrap BUSY, fsync/dirsync and retention retirement separately.

### 2.6 v1 structure `journal` must NOT carry

- **Unchecked arena allocations** (PLAN §2's "checked arena allocations").
  - `edit_arena_alloc` results are used unchecked at `journal.c:313, 322, 522, 525, 527, 1251, 1291`, `retire.c:404` and
    `manual.c:106`.
  - The arena inits are all checked (`journal.c:312, 321, 521, 1250, 1290`; `retire.c:403`).
  - Most allocations are sized to fit, so they "cannot fail in practice". That is precisely the class CLAUDE.md
    (A15) makes either an ASSERT (if no input can produce it) or a return code. Each site states which, with the
    sizing derivation.
- **Synchronous UI-spin retention.** `retain_base` submits and drains with 1 ms sleeps until received
  (`journal.c:1115-1141`). It runs inside `journal_save_prepare` on whichever thread calls prepare: in v1 that is the
  savectl worker (§0.1). It goes away with §6 Q7.
- **Documented-but-absent reflink.** `journal.h:173, 213-229` and v1 `STATUS.md:182` say "private reflink or bounded
  worker copy". The code never calls `os_file_clone`; `retain_copy` copies in 64 KiB chunks (`journal.c:1052-1065`), and
  `v1:tests/journal_cancel_test.c:7-16` asserts `clone_retries == 0`. The v2 retention bead states which it implements
  and tests it. A14(b) asks for "hard link, then reflink, then copy, and the bead states which and why".
- **Private pool and message handler** (§0.1, §6 Q7).
- **`EDIT_JOURNAL_PORTABLE_CRC` env switch** (§2.3).
- **Coordinating booleans in `struct journal`.** `active`, `failed`, `directory_pending`, `recovery_pending`,
  `append_failed`, `append_pending` (`journal.c:47-48`), plus `journal_disk.writeback_failed` (:32). That is seven
  booleans for two orthogonal axes:
  - **worker transport**: {IDLE, WRITING, FAILED};
  - **recovery suspension**: {HEALTHY, APPEND_GAP, WRITEBACK_LOST, DIRECTORY_PENDING, NEEDS_ROTATION}.

  Model them as two enums. Whether these are exactly the states is for the bead decision note, checked against
  `journal_retry` (:575-591), which is the only function that clears them.

### 2.7 Tests to port (counts M)

| v1 test | Lines | Notes |
|---|---|---|
| `tests/journal_test.c` | 2232 | Public API plus `file/file.h` (:6). Port. Includes the Law 2 guard case (:2062-2064) |
| `tests/journal_group_test.c` | 182 | Includes `editor/private.h` (:5). Re-target at edit + journal |
| `tests/journal_hot_exit_test.c` | 241 | Port (phase kill points) |
| `tests/journal_kill_test.c` | 255 | Port |
| `tests/journal_os_test.c` | 359 | Uses `journal/private.h` internals (`journal_retire_with_io`). Expose through `JOURNAL_TEST_HOOKS` |
| `tests/journal_retire_test.c` | 620 | Includes `editor/private.h` (:6). Re-target |
| `tests/journal_safety_test.c` | 395 | Rewrite off `.c` (§0.3) |
| `tests/journal_cancel_test.c` | 47 | Rewrite off `.c` |
| `tests/journal_ack_test.c` | 103 | Defer with manual.c |
| `fuzz/journal_fuzz.c` | 389 | Port. Add BASE-range, generation and version-header ops |
| `bench/journal_bench.c` | 374 | Port |

## 3. `savectl` — `src/savectl/`

PLAN §3: **rewrite cleanly** (orthogonal 4-axis FSM + fd-captured BASE). PLAN §4.4 gives the axes. The rewrite keeps
v1's protocol knowledge and cuts its state encoding.

### 3.1 Files and counts (M) and disposition

| File | Lines | Disposition |
|---|---|---|
| `v1:src/savectl/savectl.c` | 893 | Rewrite. Harvest the protocol (§3.2): two-phase journal lease, finish/recover/cancel tokens, staleness epochs |
| `v1:src/savectl/savectl.h` | 215 | Rewrite the surface. Keep the host-contract comments as the spec of what changes |
| `v1:src/savectl/STATUS.md` | 92 | Read only |

v1 functions:
- `worker` :314-444, with the OP_SAVE branch at :343-384 and the post-rename capture at :375-381;
- `savectl_tick` :786 onward, with finish installation at :798-817;
- `submit` (lease at :467);
- `changed` :755-759, `saved` :766;
- `reload_worker` :195-294, `savectl_take_reload` :853-893;
- `savectl_get_model` :568-592;
- `savectl_destroy` :531-543.

### 3.2 Public API to carry, and what changes

Keep the operations, change the types:
- `create`, `destroy`, `close_begin`;
- `modified` / `content_identity` (the dirty tracking by content identity, `savectl.h:84-92`);
- `save_request`, `finish_request`, `recover_request` (the bounded worker-prepare form, `savectl.h:107-145`);
- `recover_finish`, `recover_cancel`;
- `saved_base`, `save_token`;
- `target_identity`, `rebase_target`, `rebase_kept_target`;
- `file_event`, `reload`, `keep`;
- `receive`, `tick`, `take_reload`.

Changes:
- `savectl_save(s, piece_tree *tree, …)` (:105) takes a live tree. It is replaced by the request form taking a
  **`piece_snapshot *`** (already the shape of `savectl_request.snapshot`, :134-140). Delete the tree form.
- `savectl_options.reload_allocator` (`piece_allocator`, :52) and `reload_mark/reset` are v1 piece allocator hooks.
  They do not exist in v2 piece, where storage comes from the process `piece_reserve` (`piece.h` STORAGE :59-74). Delete
  them. Reload opens through `file` (§1.5(h)) into a tree on the same reserve.
- `savectl_options.journal_pool` "must differ" (:41-43, :55). Deleted under §6 Q7.
- `savectl_status_frame_submitted` and `saving_frame_pending`/`request_ns`/`saving_submitted_ns` (G8s frame accounting,
  :146-153) are a W2 GUI concern. In W1 there is no frame, so keep the request timestamp and defer the frame hook to
  app_paint.
- `savectl_model` (:28-35) exposes `bool modified, can_reload, can_keep, busy, needs_finish, journal_leased,
  can_recover_cancel, saving_frame_pending`. In v2 it exposes the four axis enums plus dirty state; derived predicates
  are functions of them.

### 3.3 Dependencies, globals, Windows

- `savectl.h:3-4` includes `file/file.h` and `journal/journal.h`. That conforms (`file < journal < savectl`).
- `savectl.c:6` includes `<sys/stat.h>`, which is forbidden (§0.1).
- No globals. Windows blocks are listed in §0.3.
- `savectl_cache` (a refcounted reload-cache directory descriptor, :8-17) disappears if reload goes through `file`
  and `os_file_open_anon` in the target's directory (`v1:savectl.c:228-234` already prefers that).

### 3.4 Acceptance lines that already exist

- **PLAN §4.4.**
  - The four axes under `-Wswitch-enum`.
  - FD-captured BASE.
  - Typing resumes as soon as PREPARING returns the journal lease.
  - Post-rename `FILE_ERR_CHANGED` goes strictly to `CONFLICT_CANCEL` (never `FINISHING`), keeping the retained original
    and checkpointing a self-contained snapshot.
  - `FINISHING` updates every buffer sharing the path and preserves post-cutoff edits.
- **PLAN §6.** Post-rename foreign-file conflict test, SIGKILL at every save step + failed fsync/dirsync, and
  `make check-tsan` green on `savectl`.
- **Known bugs, Bug 2** ([wave0-known-bugs.md](wave0-known-bugs.md):135-251). This is the full test design: step hook at
  `FILE_STEP_RENAMED`, foreign rename-over, then:
  - `needs_finish == false`;
  - `savectl_saved_base() == NULL`;
  - `can_recover_cancel == true`;
  - the buffer stays modified;
  - disk keeps `"foreign"`, the buffer keeps `"old!"`.

  Variants: (a) no journal; (b) the control (foreign replace after needs_finish, before finish, gives BASE_CHANGED);
  (c) post-cutoff typing survives. This is contract ledger row **X2**.
- **A14(b)** (savectl/edit). Generation retention across saves: an undo naming a pre-save generation needs that
  generation durable before acknowledgement. The bead evaluates hard link → reflink → copy and states which and why.
- **Piece-pool ownership table, row "Saved text, fd-captured BASE and in-flight save"** (:16).
  - The `file_id` from the still-open output fd adds zero tree roots.
  - The writer keeps its input through its last read.
  - The controller keeps the cutoff until finish, reconciliation or teardown.
  - Conflict cancellation keeps the original and obtains the self-contained checkpoint.

### 3.5 v1 structure `savectl` must NOT carry

- **The post-rename conflict bug.** The worker correctly sets `file_error = FILE_ERR_CHANGED` when the pathname-captured
  BASE differs from the fd-captured identity (`v1:savectl.c:375-381`). `savectl_tick` then installs `saved_base` and
  `needs_finish = true` **without gating on `r->file_error`** (:801-804), and also calls `changed(s)` (:806-807). The
  result is `needs_finish && external`, a pair that no single v1 state names. A later finish marks the buffer clean with
  foreign bytes on disk. v2 avoids this by construction: `save_phase` moves VERIFYING_FD → CONFLICT_CANCEL as an enum
  transition, and FINISHING is reachable only from VERIFYING_FD with an equal identity.
- **Pathname-captured BASE after rename.** `journal_capture_base(s->path, …)` (:376) re-opens the **path**. PLAN §4.4's
  fix: the BASE for the new generation comes from the fd `file` dup'ed before rename (`v1:file.c:1005, 1011`), and the
  path is only verified against it. A mismatch moves to CONFLICT_CANCEL.
- **The no-journal variant** (known bugs, Bug 2, "Secondary gap"). With `options.journal == NULL` the verification at
  :375 is skipped. On a post-rename `FILE_ERR_DIRSYNC`/identity error, `saved(s)` is not reached and `baseline` stays
  stale (§1.5(g)). v2 verifies unconditionally.
- **Coordinating booleans.** `struct savectl` (`savectl.c:52-93`) has `modified, active, event, external, ready_reload,
  needs_finish, closing` (one line, :59), `content_known, saved_content_known`, `reload_reserved`, atomic
  `prepare_returned`, `journal_leased, saving_frame_pending`, `received`, `task.request_mode`, `task.guarded`, plus the
  public `savectl_state` enum (`savectl.h:18-21`). That enum collapses phase and external state: `SAVING` and
  `RELOADING` are phases, `EXTERNAL_*` is external state, and `SAVED`/`FAILED` are last outcomes. Proposed mapping (the
  census's, to be confirmed in the bead decision note):

  | v2 axis | v1 encoding today |
  |---|---|
  | `save_phase` IDLE / PREPARING / WRITING / VERIFYING_FD / FINISHING / DIRSYNC_RETRY / CONFLICT_CANCEL | `active`, `task.op`, `completion.prepared/handoff_pending/pending`, `prepare_returned`, `needs_finish`, retained `token`, `file_error`/`journal_error`. **No v1 field marks VERIFYING_FD**: the fd identity capture is buried in `file_save_write` (`file.c:1011`) |
  | `external_state` CLEAN / CHANGED_ON_DISK / RELOADING | `external`, `event`, `ready_reload`, `state ∈ {EXTERNAL_*, RELOADING}`, `reload_tree`/`result.reload` |
  | `journal_lease` UI_OWNED / SAVE_OWNED | `journal_leased`, `prepare_returned`, `handoff_pending` |
  | `close_state` OPEN / CLOSE_PENDING / RETIRED | `closing`, the `epoch` bump at close (:547), `destroy` BUSY (:534-536) |
  | none (dirty tracking, a separate axis owned by edit/app) | `modified`, `content_known`, `saved_content_known`, `revision`/`saved_revision`/`content_id`/`clean_id`/`saved_content_id` |

  Open point for the reviewer: PLAN's `save_phase` has no state for "prepared token retained after a failed attempt"
  (`token` + `!active`, `can_recover_cancel`, :589-590). v1 forbids a fresh save while it exists (`savectl.h:104`). §6 Q9.
- **Worker spin on the lease handoff.** The worker spins via `work_continue` until the UI sets `prepare_returned`
  (`savectl.c:320-322`). The handoff should be two jobs, prepare then write, not a parked worker holding a bulk slot.
  (v2 `WORK_MAX_WORKERS` is 9 (P: 1 + `WORK_MAX_RASTER` 8, `v2:work.h:23-24`). A parked save holds one of the bulk
  workers configured at `work_pool_init`.)
- **`busy_wait` retry** around `journal_save_prepare` (:354, :392; defined :101). This is a worker-side retry of the
  journal's BUSY. It goes with Q7.

### 3.6 Tests to port (counts M)

| v1 test | Lines | Notes |
|---|---|---|
| `tests/savectl_test.c` | 1309 | Rewrite off `.c` and off libc interposition. 30+ cases (census): `journal_handoff_crash`, `finish_writeback_recovery`, `journal_base_conflict`, `journal_transaction`, `ck4_directory_recovery` (the Bug 2 template, :1127), `race`, `failed_io`, `logical_close`, `during_reload_edit`, … |
| `tests/savectl_reload_test.c` | 122 | Port |
| `fuzz/savectl_fuzz.c` | 592 | Port. `journal_session`, `real_mapped_fault`, `multichunk_reload`, `reused_slots` |
| `bench/savectl_bench.c` | 330 | Port without the frame hook |

## 4. `edit` — `src/edit/` (the L2 edit kernel)

PLAN §2: "layout-free edit kernel harvested from `view/edits.c`: selection, `piece_checkpoint` + undo reserve, journal
staging preflight & emission". PLAN §4.2 gives ownership. The v2 placeholder is `v2:src/edit/edit.c` (8 lines) and
`edit.h` (10 lines), the smoke stub calling `piece_app_id_len` that 7b removes (`piece.h:561-563`).

### 4.1 Sources and counts (M) and disposition

The kernel has **two** v1 sources. PLAN §3 names only `view/edits.c`, but the transaction half is in `editor/input.c`.

| v1 file | Lines | What goes to `edit` | Disposition |
|---|---|---|---|
| `v1:src/view/edits.c` | 931 | Command plans: `view_duplicate_line` :83, `view_delete_line` :102, `move_line` :108-139, `view_join_lines` :141, `insert_line` :175-195, `view_delete_to_bol/eol` :197-215, `view_delete_to_soft_bol/eol` :244-275 (layout, §4.3), `indentation`/`view_indent`/`view_unindent` :278-315, `view_transpose` :317, case mapping `change_case` :440-471, `view_sort_lines` :526, subword motion :563-625, brackets :630-664, mark ops :665-695, `view_yank` :697, `view_overwrite_range` :707, `view_toggle_comment` :834 | Harvest + fix. Plans are pure ("no mutation, allocation, or retained pointers", `v1:src/view/view.h:189-194`) |
| `v1:src/view/case_tables.h` | 4400 | Case mapping tables used by `change_case` (`edits.c:339`) | Harvest with its generator, if one exists (check v1 `tools/`). Otherwise record it as data (§6 Q13) |
| `v1:src/view/view.h` (subset) | 246 | `view_selection` :26, `view_edit_plan` :195-199, `view_state` selection fields :27-37 | Carry the selection and plan types only. `view_state`'s scroll fields belong to L4 viewport |
| `v1:src/editor/input.c` (subset) | 1707 | `mutate_text` :395-448, `delete_part` :383-394, `stage_delete/insert/tree` :85-106, `editor_journal_staged` :108-170, `remember` :211-244 (history ring), `end_edit_group` :245-251, `finish_edit` :261-300 (minus layout), `prefix_error` :368-375, `editor_replace_begin/delete/insert/end` :451-484, `replay_slice` :689-710, `replay_history` :712-724, `soft_history` :725-745 | Rewrite (§4.4). Keep the protocol knowledge, cut the structure |
| `v1:src/undo/undo.c` | 492 | Not edit's. Undo is L1 (`v2-0fv.8`, rewritten as `undo@9edcb41:src/undo/undo.c`, 930 lines) | Dependency |

### 4.2 Public API to define (v2)

The API is proposed from PLAN §4.2 and the piece/undo headers. Names are the bead's to settle:
- **`edit_buffer`**: one per open buffer, UI-only. It holds `piece_tree*`, `undo_log*`, the selection (cursor, anchor,
  preferred column, mark), the transaction state enum (§4.4), and the journal staging queue for this buffer.
- **Commands.** `edit_apply_plan(eb, plan)` for every plan producer. `edit_type`, `edit_delete`, `edit_replace`
  (compound under one checkpoint). `edit_undo`/`edit_redo` and their sliced forms, driving `undo_undo_slice`
  (`undo@9edcb41:undo.h:96-101`).
- **Change record (output, not a callback).** `{off, old_len, new_len, revision}` per applied command. L4 layout/viewport
  and L5 consume it after submit (§4.3). v1's `changed` (`input.c:42-83`) calls up into scroll/palette/frame/layout from
  inside the mutation.
- **Journal emission.**
  - `edit_stage(eb, journal*)` and progress continuation (v1 `editor_journal_staged` semantics: group markers only for
    multi-record actions, `journal.h:38-42`).
  - Undo of ORIGINAL runs emits the BASE-range record.
  - ADD runs and literals are emitted by value (piece-pool spec :137).
- **Maintenance.** `edit_maintain(eb)` is the **one** function that calls `piece_lines_resolve`, then `piece_replenish`,
  then `piece_reclaim`, in that order, outside the input→submit window. This is the A19 acceptance line for v2-0fv.15:
  "Post-submit maintenance (resolve, replenish, reclaim, in that order, from one function that is their only caller)".
  `piece.h:390-400` says to run resolve before replenish, "whose top-up it would otherwise spend". Undo's
  `undo_maintain` (`undo@9edcb41:undo.h:66`) belongs in the same function.

### 4.3 Dependencies and upward edges to kill

- edit includes piece, undo and journal (`journal < edit`), plus utf8 (cluster boundaries, `edits.c` uses `utf8_step`
  :381) and indent (`v1:input.c:416-421` `indent_on_enter_after_delete`, `indent_on_close_brace_after_delete`).
  `find/find.h` (`edits.c:3`) provides `find_literal_next` for `view_skip_next_occurrence` (:419). All of these are L1, so they are allowed.
- **Layout.** `soft_row` (`edits.c:216-235`) calls `layout_visual_row` when wrapped. In v2,
  `edit_delete_to_soft_bol/eol` take the soft row `[start, end)` from the caller, which is L4 viewport/layout. The
  plan is then layout-free; the row query is L4's. The simplest alternative, a callback from L2 into L4, is an upward
  edge by function pointer, the same smell as the v1 downcast. Rejected.
- **`changed()`'s upward calls** (§0.1). The kernel only records the change; consumers poll the record.
- **`comments_for(path)`** (`edits.c:731-748`) picks comment syntax from the file name. That is language knowledge,
  which v2 puts in hl's `enum lang IDs` (PLAN §2 L4). In Wave 1 the comment command takes the comment style as an
  argument; L4 supplies it.

### 4.4 Acceptance lines that already exist

- **PLAN §4.2.**
  - L2 edit owns selection, atomic forward replacement/insertion, `piece_checkpoint` + undo reservation, and journal
    staging preflight and emission.
  - Wave 1's headless editor uses this exact production path.
  - Steps 1-4 of §2.5, and the SIGKILL oracle.
- **PLAN §6 Wave 1 exit.**
  - The edit runs under the live release malloc guard (`active && count == 0`).
  - The A13 bounded-copy gate: delete and undo of a mapped range of any length copy at most min(length, 16 × spans)
    ORIGINAL bytes, with O(spans) inverse storage. Fixtures are a one-byte literal and a one-span large range.
  - A one-span delete + undo reads at most 2 × 65,536 = 131,072 B of mapped ORIGINAL (P), tested by counting bytes read.
  - The A19 replacement: after ORIGINAL is counted, line queries are exact after any edit or undo of any size, in O(h)
    node visits, counted. The fixture is the 513-chunk delete and its undo, with line queries checked against the model
    before, between and after.
  - A19 bullet 5 sets the read count at 131,072 B (P, 2 × 2 × 32,768). The test counts bytes read against 131,072.
- **A19.** The edit bead owns the maintenance-order line (§4.2).
- **A14(b), (c).** Generation retention; checkpoint-diff consumers cannot assume ordered ORIGINAL spans.
- **Piece-pool, "Synchronous admission"** (:181-190).
  - A valid command executes once, or returns a visible refusal after actual base failure:
    **"Edit not applied — buffer memory is full."**
  - A failed preflight leaves bytes, selection, history cursor, save cutoff and journal sequence unchanged.
  - Test a failed post-submit warm-up followed by a fitting key, exact synchronous top-up inside the active guard,
    injected necessary base refusal, and a later successful command.
- **Piece-pool, "Acceptance at the edit boundary," rows "Admission at the limit" and "Revision accounting"**
  (:201-202). Preserve bytes, selection, undo, saved cutoff and journal sequence on refusal. Back-to-back keys, compound
  replace, tab switches.
- **Undo review MUST-FIX 2** ([wave1-undo-astra.md](../reviews/wave1-undo-astra.md)).
  - The forward transaction has one checkpoint owner and a matching history commit/abort boundary.
  - Redo detachment, eviction, burst mutation and publication are deferred until irrevocable commit, or restored on
    abort.
  - Exercise the trace (private-copy tree, outer checkpoint, insert + delete groups, abort, capture, undo yields the
    wrong 16 bytes) and a literal capture under the same outer command.

  This is an **undo** fix whose shape edit dictates. §6 Q8.
- **Undo review SHOULD-FIX 1.** Verify the complete input-to-submit path at integration with the guarded fixture
  extended to replay. Instrument the real tree for the 131,072 B ceiling.
- **Undo candidate open question** (`undo@9edcb41:docs/decisions/v2-0fv.8.md`, "Integration and open questions"):
  - Which byte cap will L2 use with the new slot and literal charges?
  - How will a compound forward edit share its journal admission boundary with history?
  - L2 and L5 own the editor-level branch contract that v1 covered in `editor_undo_branch_test.c`.

### 4.5 v1 structure `edit` must NOT carry

- **(a) Non-atomic compound edits.** `mutate_text` runs `undo_group_begin` → `delete_part` → `undo_insert` →
  `stage_insert` → `changed` → `repair` with **no** `piece_checkpoint_begin`. That symbol occurs nowhere in
  `src/editor`; its only v1 caller is `undo.c:209`, in replay. A refusal after the delete is kept as a partial edit:
  `prefix_error` records the partial delta into history and marks the buffer modified (`input.c:368-375`, callers
  :431-482, :674). v2: a compound command runs under one checkpoint, whose abort cannot fail (`piece.h:376-386`). On
  refusal, nothing changed.
- **(b) Preflight covers only staging.** The check at `input.c:404` is staging capacity only. The index preflight
  `check_index_edit` (:376-382) is the v1 32 MiB veto (known bugs, Bug 3) and **disappears** with lineidx (A19).
- **(c) Content scan before history capture.** `delete_part` calls `newlines(e, lo, old)` before `undo_delete`
  (`input.c:386`). A13: "v1's delete_part scans newlines before history capture". Line counts now live in the tree
  (A19), so edit never scans for newlines.
- **(d) By-value undo journaling** with a borrowed tree pointer in the staging op (`stage_tree`, `editor_jop.tree`,
  `v1:private.h:42`). This keeps input parked "until this borrowed tree range is journaled" (`input.c:100-101`). The
  BASE-range record replaces it for ORIGINAL runs. ADD runs are re-read from immutable ADD coordinates, which no edit
  can invalidate.
- **(e) History ring duplicated in the editor.** `editor_buffer.history`, `history_head/count/cursor/cap`,
  `saved_cursor`, `save_cursor`, `save_tracking`, `saved_lost`, `soft_history`/`soft_*` (`v1:private.h` `editor_buffer`;
  logic in `remember`, `input.c:211-244`). This shadows undo's own group list in order to track the "saved" history
  position and selection deltas. The `saved_lost`/`save_tracking` booleans patch eviction and redo-discard cases by
  hand (:215-243). v2 has one source of truth: undo's `group_serial` (`undo@9edcb41:undo.h:37-40`). "Clean" is defined
  by content identity: savectl's `content_identity(current, saved, saved_valid)` (`v1:savectl.h:84-92`) already exists
  for this. The simplest alternative, keeping the ring, is rejected: it duplicates undo's state, and its consistency is
  the four hand-maintained flags above.
- **(f) The `editor_action` enum used as a scheduler.** `EDITOR_ACTION_REPLAY_WORK`, `_CLIP`, `_REPAIR` (`private.h:256`)
  mark multi-turn operations. The edit transaction state is its own enum, for example
  `{EDIT_IDLE, EDIT_OPEN, EDIT_STAGING, EDIT_REPLAY_YIELDED, EDIT_SUSPENDED}`. "Suspended" is the latched journal I/O
  failure of §4.2 step 4. Whether STAGING and OPEN are separate is the bead's derivation.
- **(g) Silent refusals.** On a full staging buffer, `mutate_text` increments `rejected_commands` and returns OK
  (`input.c:404-406`). The piece-pool spec requires a visible refusal message (§4.4).

### 4.6 Tests to port (counts M)

| v1 test | Lines | Notes |
|---|---|---|
| `tests/view_test.c` | 1016 | Plan producers. Port the edits.c cases |
| `fuzz/view_fuzz.c` | 336 | Port the plan ops against a byte model |
| `tests/editor_undo_branch_test.c` | 106 | `branch(discard_saved)`. Re-target at edit + savectl (the undo candidate names this gap) |
| `tests/journal_group_test.c` | 182 | Crash at AFTER_DELETE / BEFORE_COMMIT / COMPLETE × UNDO/REDO/REPLACE/PASTE. Re-target at edit |
| `tests/dataloss_test.c` `torn_undo_group` | :443-498 | A 256 KiB replacement undo killed mid-staging restores to length 1 or 262,144, never a prefix (`GROUP_BYTES` :446: 16 × 16,384 = 262,144 (P)). In v1 it runs only outside `--probe` (:2095) |
| `bench/view_bench.c` | 297 | Port |

## 5. Minimal L5 — `src/app/` (`app_loop`, `app_buffers`, `app_session`, `app_save`)

PLAN §2 L5 and §4.7. A10: minimal L5 waits for the Wave 0 gate. v2 today has `v2:src/app/app.c` (8 lines),
`app.h` (11 lines) and `v2:src/main.c` (10 lines), all smoke stubs. **Everything here is a rewrite.** v1 `src/editor/`
is evidence only. PLAN §3 lists `src/app/` + `main.c` under "Rewrite cleanly".

### 5.1 `app_loop` — v1 evidence `v1:src/editor/editor.c` (1544 lines, M)

**v1 shape.** `editor_step` (:1291-1414) is the per-iteration function. The census lists its steps in order. Its blocking
point is `pump` (:1302), which runs `editor_poll_sources` (`v1:buffers.c:961-1078`). That function holds the mailbox
drains, savectl ticks, retain tick, source checks, IPC drain and one line index per turn.

**v1 structure not to carry, with a cite for each:**
- **Reentrancy.** Several steps re-enter the loop:
  - `add_buffer` calls `editor_step(e, 1)` in a loop during a mapped open (`buffers.c:505-512`);
  - `platform_work` calls `editor_poll_sources` from inside `plat_run_for` (`editor.c:390-399`);
  - `editor_flush` runs ticks itself (:1502-1509);
  - the mailbox callback `editor_route_work` (:327) is invoked from many sync-wait sites (`buffers.c:66, 169, 733, 741`;
    `session.c:484, 775, 787`).

  PLAN §4.7 requires `assert(!app->in_step)`. In v2 terms that is an `ASSERT` (A15, the always-on macro).
- **Sync waits on the UI thread.**
  - `wait_open_ready` (`buffers.c:61-69`) and the prefix wait in `editor_buffer_prepare` (`buffers.c:164-173`) loop with
    100 ms `os_wake_wait` + drain.
  - The `os_yield` spins (`editor.c:1510`; `session.c:489, 775, 788`; `buffers.c:88`; `retain.c:126`) are in close or
    flush paths, except `buffers.c:88`, which is `editor_buffer_destroy`.

  PLAN §4.7: "zero `sched_yield` spins; large mmap opens … run as `BUF_OPENING` async states". Close and exit are
  states too.
- **Two "pending" predicates.** `runnable` (`editor.c:1098-1120`) and `editor_get_stats().pending` (:1459-1466) OR
  different sets. PLAN §4.7 asks for one `app_work_pending()` / `app_next_deadline_ns()` derived from the same cluster
  states. v1's `wait_timeout` (:1121-1180) is the deadline evidence: blink, journal sync `last_sync_ns + 1 s` (:1167-1176),
  checkpoint retry, busy-due.
- **Input order (A9).**
  - `queue_pick` (`editor.c:1064-1086`) parks keys of an opening buffer (:1082, :1084-1085).
  - It lets "control" events overtake them (:1080), where control means SAVE, CLOSE, NEW_FILE, OPEN_FILE and tab
    switches, as classified by `editor_event_control` (`input.c:967-985`).

  This is the A9 failure: Ctrl+S overtakes held keys and saves nothing. The v2 rule: events apply in the order sent,
  across an in-progress open. A full queue drops input with a notice (`editor.c:1248-1253`, `EDITOR_INPUT_CAP` 1024,
  `v1:editor.h:22`, (G)). Keep the visible refusal. §6 Q15 asks what an event aimed at a still-opening buffer does.
- **Fatal key errors.** Any key error is fatal (`editor.c:1376` `fail`; A8 traces the −105 exit through it). v2: a
  command returns a refusal (piece-pool "Failure and retry") and only an invariant violation aborts (A15).
- **Frame fence** (§0.2, `editor.c:625`). Headless W1 has no frame, so the fence applies to the **command
  acknowledgement** (PLAN §4.2 step 3: "frame submission and command acknowledgment"). The headless editor needs an
  explicit ack event for the SIGKILL oracle to observe (§6 Q16).

**Acceptance lines that already exist:**
- PLAN §4.7: straight-line ordered `app_step()` with named cluster steps in fixed order, one `app_work_pending()` /
  `app_next_deadline_ns()`, the non-reentrancy guard, zero spins, physical work-lease retirement (`work.h`).
- A9 (input order).
- Law 2: the live malloc guard over input → submit, and G1 is the one enforced perf guard (CLAUDE.md Law 1/2).

**The A12 analogue** (push-back, not an existing line). A12 forbids UI-thread libxcb reads between ingress and submit.
The same window in v2 contains `journal_append`'s `pwrite` (`v1:journal.h:250-262`, a syscall on the typing path, by
design). It is a write, not a read, and it is the RAM/page-cache acceptance the SIGKILL oracle depends on. State it
explicitly as the one sanctioned I/O in the window, rather than leave it implicit (§6 Q6).

### 5.2 `app_buffers` — v1 evidence `v1:src/editor/buffers.c` (1264 lines, M)

**v1 shape.**
- `editor_buffer_prepare` (:151-312) creates the per-buffer arena, `piece_create` (:228), the undo log (:258), the
  history rings (:260-263), minimap, `lineidx_create` (:280, again at :332) and scroll.
- `editor_open_request` (:827-930): identity lookup `path_tab` (:860), prepare, journal registration
  (`editor_register_journal` :411-435), `install`, `editor_save_setup` (`session.c:493-532`).
- Lifecycle is booleans on `editor_buffer`: `pending_open`, `placeholder`, `retired`, `source_stale`, `watch_pending`,
  `target_rebound`, `read_only`, `derived`, `undo_ready`, `index_dirty`, `painted`. There is no enum.

**v2 shape.**
- **Buffer lifecycle enum** `{BUF_OPENING, BUF_LIVE, BUF_RELOADING, BUF_CLOSING, BUF_RETIRED}` under `-Wswitch-enum`
  (PLAN §4.7 names `BUF_OPENING`).
- `read_only`/`derived` are a separate **kind** axis (`BUF_FILE`, `BUF_UNTITLED`, `BUF_DERIVED`), not lifecycle.
- Per buffer it owns `file*`, `edit_buffer` (tree + undo), `savectl*` and the line-counting job.
- **No per-buffer arena for piece.** Piece draws from the one process `piece_reserve` (`piece.h` STORAGE). The
  reserve is created and destroyed by app (`piece_reserve_create/destroy`, :174-177).

**Acceptance lines that already exist:**
- **A19.** "scheduling the piece_lines_count job is an acceptance line of the L5 buffers bead".
  - The job counts chunks off path (`piece_lines_count`, `piece.h:516`) on `WORK_BULK`.
  - It is adopted with `piece_lines_adopt` (:530); a batch for a closed or reloaded buffer is refused by the source-id
    check.
  - The job's lifetime is work's lease (`v2-0fv.7.md:296-302`).
  - v2-0fv.9 is held until this lands (A19).
  - Derivation of the table size quoted in `piece.h:524-526`: 4 B per chunk (P); 1 GiB / 65,536 = 16,384 chunks × 4 B =
    65,536 B (P).
- **Fault spec, "Physical work retirement" row.**
  - Acquire an owning source reference before submit, and roll it back on failed submission.
  - Logical cancel retains a running job's source.
  - Release job resources exactly once after `work_handle_finished` or joined shutdown.
- **A17** (W3 line, but do not build against it).
  - Opening or restoring N tabs is O(N) total, tested by counting the per-tab operation.
  - v1 candidates by symbol (census): `path_tab` (`buffers.c:476-480`) does `realpath` + stat per existing tab per
    requested path, via `editor_same_path` :460-474. `editor_session_metadata` (`session.c:184-208`) and
    `editor_journal_metadata` (`checkpoint.c:359-410`) walk all tabs.
  - The W1 app_buffers uses an identity-keyed lookup (by `os_identity`, `v2:os.h:164-167`) so the path check is O(1)
    per open. A17 asks the W3 bead to **identify** the operation first. This dossier only lists candidates; nothing was
    timed.
- **Contract R16** (W3): per-file fd ownership is a v1 mechanism (ledger). Mapped buffers hold a dup fd
  (`v1:file.c:400`). See §6 Q12 on `OS_FAULT_SLOTS`.

### 5.3 `app_session` — v1 evidence `v1:src/editor/session.c` (794), `checkpoint.c` (476), `recovery.c` (1074), `retain.c` (142) (M)

**v1 shape.**
- **Leaves.** A persistent path-copied binary tree of `session_node` leaves (`session.c:12-27, 109-120`), one mutex for
  refs and the freelist (:116-117).
  - `publish` (:123-151) takes `piece_snapshot_take`, copies BASE and view, and retains `saved_snapshot` as `reference`.
  - `capture` (:549-570) pins root + selected snapshot for the save worker.
- **Checkpoint.**
  - `editor_publish_start` (:412-456) → `publish_worker` (:396-410): validate, `encode_all`, `journal_flush`,
    `journal_rotate`, release.
  - `encode_leaf` (:220-274) uses `editor_text_ops` (`checkpoint.c:98-125`) to diff against the saved reference, with
    the BASE-gap optimization (:109-114).
- **Recovery.** `editor_session_restore` (`recovery.c:988-1057`) → `restore_source` (:751-887) → `restore_record`
  (:207-296) into gap buffers, `materialize_base` (:175-205), `plan_partial` (:540-596), `install_session` (:622-693).
- **Retain.** `editor_retain_tick` (`retain.c:35-106`), a per-buffer `EDITOR_RETAIN_{IDLE,RUNNING,READY,FAILED}`
  enum (`private.h:674`, stored in a `uint8_t`).

**v2 shape** (PLAN §2: "copies {path, slot, origin, view_state} into immutable leaves on UI; workers never dereference
live app_buffer*"):
- Leaves carry copied metadata + an owned `piece_snapshot` + the saved-text reference (piece-pool ownership row
  "Published and captured `app_session` leaves", :15).
- The v1 leaf also holds `buffer` (a live pointer, `session.c:12-27`). The piece-pool spec lists "the raw buffer pointer
  in the old session graph" among the **v1 holders v2 does not have** (:20).

**Acceptance lines that already exist:**
- **PLAN §6.** `make check-tsan` green on `app_session`.
- **Piece-pool row "Published and captured app_session leaves".**
  - Replacing the graph leaves the captured graph alive until physical read completion and release, or until UI
    rollback on failed submission.
  - No worker reads a live `app_buffer *`.
  - The acceptance row "Revision accounting" (:202): hold saved text, start save, edit and publish, capture finish, edit
    again. Verify both captured leaf references, the written cutoff and current text for selected and non-selected
    buffers.
- **A14(c) / piece-pool (iii).** In `editor_text_ops`, keep the forward BASE-gap optimization only for proven ordered
  matching runs. For a backward or repeated range, emit a BASE-range insert from the verified source at the output
  cursor without advancing the old BASE cursor. Test overlap, duplicate runs, and a saved BASE that differs from the
  tree origin. The v1 assumption is stated in its own comment at `checkpoint.c:109-110` ("Kept spans arrive in
  increasing BASE order … edits never reorder saved bytes"), which the span-sequence inverse breaks.
- **R4 recovery kernel** (W1; GUI in W3). Partial restore picks unsaved first (`plan_partial`, `recovery.c:561-593`)
  and sets the source aside rather than deleting it when anything was dropped (:818-826).
- **R31** (§0.4).

**v1 structure not to carry:**
- **The checkpoint worker drives the journal's private pool** from a worker (`publish_worker` → `journal_flush`; §0.1).
- **Silent snapshot failure in `publish`** (`session.c:140-141`): "a later turn retries". Under v2 synchronous admission
  a refused take is `PIECE_ERR_NOMEM` (piece.h:348-357). State what the session does: keep the old leaf and mark it
  stale, so a save refuses.
- **Startup recovery's gap buffers** (`recovery.c:119-163`) replay into a private byte array, then reopen. The v2
  replay applies into piece trees directly (`journal_apply_piece`), so BASE-range records resolve against a verified
  source (§6 Q3).
- **The sync spin in `editor_publish_wait`** (`session.c:489`) and in close (:775, :788).

### 5.4 `app_save` — v1 evidence `v1:src/editor/session.c` `request_buffer` :585-623, `editor_save_tick` :637-737; `commands.c` (Save As)

**v1 shape.**
- `request_buffer` captures the cutoff, sets `saving`/`save_revision`/`save_cursor`/`save_tracking`, breaks the undo
  burst, and starts only when no ops are staged and no group is open (:621-624). Otherwise it sets `pending`.
- `editor_save_tick` covers:
  - deferred start (:642-651);
  - reconcile/retry (:653-673);
  - failure (:674-684);
  - the finish checkpoint request (:686-694);
  - SAVED handling: rebase all buffers sharing `save_path`, install `saved_text`/`editor_saved_reference`, drop the save
    snapshot (:696-736).

**Acceptance lines that already exist:**
- **PLAN §6 Wave 1 exit**, the whole first bullet:
  - open file;
  - edit via production L2 edit under the live release malloc guard;
  - save while typing continues;
  - post-rename foreign-file conflict test;
  - SIGKILL at every save step + failed fsync/dirsync;
  - restart and verify the exact pre-ack/post-ack recovery oracle.
- **PLAN §4.4.** FINISHING updates every buffer sharing the path and preserves post-cutoff edits.
- **v1 evidence for "save while typing".**
  - `post_cutoff` (`v1:tests/editor_session_test.c:315-331`): hold the save at `FILE_STEP_TEMP_WRITTEN`, type `y`, release.
    The disk holds `xbase` and the tab stays modified; a second save gives `xybase`.
  - `killed_mid_save(phase)` for each `FILE_STEP_TEMP_CREATED..DIR_SYNCED` (`v1:tests/dataloss_test.c:204-239, 2074`):
    the target is exactly old or new bytes.

**v1 structure not to carry:**
- `save_tracking`/`saved_lost`/`save_cursor` (§4.5(e)).
- The save-As path's `os_yield` close spin (`commands.c:288`).

### 5.5 v1 tests for L5 behaviour (counts M)

| v1 test | Lines | Use |
|---|---|---|
| `tests/editor_session_test.c` | 3331 | Evidence and case list for save while typing (`post_cutoff`, `duuw_*`, `saving_frame_busy`), interrupted saves (`interrupted_save` :636), checkpoint eviction (`crash_after_evicted_checkpoint` :680). It includes `editor/private.h` throughout, so it is rewritten against `app.h` |
| `tests/editor_checkpoint_test.c` | 936 | `fi0p-barrier`, `busy`, `pending`, `gib`, `f3i-a/b` (`type_held` :800-812) |
| `tests/editor_recovery_test.c` | 118 | Rewrite off `.c` (§0.3) |
| `tests/editor_hot_exit_test.c` | 191 | `kill_trials`, a forked child killed at `kill_point` steps |
| `tests/editor_close_test.c` | 79 | A held-frame close. W2 |
| `tests/dataloss_test.c` | 2107 | The SIGKILL campaign. W1 ports `kill_after_append`, `killed_mid_save`, `changed_base`, `reload_during_save`, `hot_exit_mixed`, `two_process_save`, `shared_save_as`, `external_rewrite(RENAME/IN_PLACE)`, `torn_undo_group`, `retain_*`. Each case is a separate port, not one bead (§7) |

## 6. Open derivations — for a reviewer (Astra), not for a worker

Each is a question plus what it depends on. Q1-Q5 are A14's and the piece-pool spec's own open edges. The rest were
found in this harvest.

- **Q1 — Generation key and declaration record.** What is the "durable source-generation key" in the 64 B BASE-range
  record, and what does the declaration record carry?
  - Candidates: a journal-local monotonic counter, declared by a GENERATION record that carries `{dev, ino, size,
    mtime_ns, ctime_ns, prefix_crc, prefix_len}` + artifact path; or the sequence number of the BASE/SAVE record that
    introduced it.
  - Must every rotation (checkpoint) re-declare all live generations so a checkpoint is self-contained? `journal.h`'s
    rotation contract says complete checkpoints retain every in-flight token's BASE and SAVE metadata (:196-199).
  - Depends on: Q2 (where the artifact lives), Q3 (how replay resolves it), and retire.c's reference scan
    (`retire.c:316`, which marks artifacts referenced while scanning BASE/SAVE records and must learn the new record).
- **Q2 — Retention mechanism across saves (A14(b)).** Hard link, reflink or copy, and when?
  - **Hard link** is O(1), but there are counter-examples to evaluate:
    1. `link(2)` changes the inode's **ctime**, and ctime is part of the BASE identity (`journal.c:394`). The retained
       generation's identity must be re-captured after linking, as v1 already does after rename (`journal.c:1190-1193`).
    2. With `fs.protected_hardlinks = 1`, linking a file the user does not own is refused.
    3. Cross-filesystem link fails.
    4. A hard link shares the inode, so a foreign **in-place** writer rewrites the "retained" generation too. Detection
       then holds (identity/mtime) but preservation does not. A copy or reflink survives it.
  - **Reflink** (`os_file_clone`, `v2:os.h:219`) is independent storage, but v1 never used it despite its docs (§2.6).
  - **Copy** is a file-sized write off the typing path (A14(b)).
  - **When.** Retain only if some live inverse or un-rotated journal record names the old generation (a reference count
    per generation, owned by undo's inverse owners + the journal), or always on save, like v1's `journal_save_prepare`
    step 2 (`journal.h:172-178`)?
  - Depends on: Q4, the undo inverse ownership API (`undo@9edcb41` "Keep generation/view ownership once per inverse",
    piece-pool spec :144), and L2-0 (an `os_link_at` exists only if chosen).
- **Q3 — Replay of a BASE-range record after restart.** At restart the tree is reopened over the **current** BASE
  (`materialize_base`, `recovery.c:175-205`), not over the old generation.
  - A BASE-range restore naming an older generation cannot be a `piece_replace_spans` reference into the new tree's
    ORIGINAL. It must copy bytes from the retained artifact (by value, into ADD) or open a second mapped source.
  - Piece has one ORIGINAL per tree (`piece.h:6-15`), so it must be by value at recovery. Is that acceptable for a 10 GiB
    restore (recovery is off the typing path; `piece_replace` copies and counts O(n))? Or does recovery re-open the
    tree over the old generation and replay forward?
  - Depends on: Q1, the piece open API, and the recovery memory budget (no derivation exists yet).
- **Q4 — Ack latency when an undo names an unretained generation.** Piece-pool spec :139: "If the required generation
  cannot be verified or durably secured, … withhold acknowledgment".
  - If retention is lazy (Q2), the undo's ack waits for a possibly file-sized copy, and PLAN §4.2 step 3 fences input
    and frames behind it.
  - If retention is eager at save time, every save of a buffer whose history holds ORIGINAL spans pays it.
  - Which, and what is the user-visible state meanwhile?
  - Depends on: Q2, G1 (CLAUDE.md Law 1), and the §4.2 fence.
- **Q5 — Journal file header.** Magic, version and feature-word layout; behaviour on an unknown version (set aside, never
  truncate); and whether the header goes on every rotation file and on session lock files.
  - Depends on: W4 v1 → v2 import (PLAN §6 Wave 4), which reads "JNL1" files with a separate importer, and retire.c's
    magic probe (`retire.c:426-430`).
- **Q6 — UI-thread `pwrite` on the typing path.**
  - v1 acceptance is "complete record in kernel page cache", so a process crash loses no successful append
    (`journal.h:250-253`). That is what makes post-ack whole-edit hold under SIGKILL.
  - Alternative: a shared-memory (memfd) ring that a crash also preserves would move the syscall off the UI thread.
    That is a new mechanism with its own fd/mapping lifetime.
  - Keep v1's choice, and state it as the sanctioned I/O in the Law 2/G1 window? "Linux regular-file pwrite can itself
    block" (:261).
  - Depends on: G1 evidence (none measured for v2), and the A12 principle (§5.1).
- **Q7 — One work pool or a private journal pool.** v1 requires a private journal pool (`savectl.h:41-43`) and drains it
  from worker threads (§0.1), which v2 work forbids (`work.h:2-4`).
  - Proposal to test: one pool. Journal batch writes are WORK_BULK jobs submitted by the UI.
  - The save job performs retention **inline** (it is already on a worker; nested submission buys nothing), and
    `journal_flush`/`rotate` in a checkpoint job become steps of that job, not a drain loop.
  - The simplest alternative, keeping two pools and letting one worker drain the other, needs a second mailbox consumer.
    That is the shape `work.h`'s single-consumer contract excludes.
  - Depends on: `WORK_MAX_JOBS` 64 / mailbox capacity (G, `work.h:25-26`) under a save + checkpoint + counting jobs, and
    TSan.
- **Q8 — The edit ↔ undo transaction boundary** (undo MUST-FIX 2).
  - Proposed order: preflight (journal staging + undo inverse capacity; piece self-admits) → `piece_checkpoint_begin` →
    mutations recorded by undo as **provisional** → journal acceptance → `piece_checkpoint_commit` + undo publish.
  - On any refusal before journal acceptance: abort piece + rewind undo provisional state (redo detachment, burst,
    eviction).
  - After acceptance, nothing aborts (§4.2 step 4). Commit cannot fail (`piece.h:377-379`), so acceptance-then-commit
    is safe.
  - Is the undo API a caller-owned transaction (`undo_txn_begin/commit/abort`) or a flag on each call? And what about a
    sliced replay that owns its own checkpoint across turns (`undo.h:84-101`)? With one checkpoint per tree
    (`piece.h:369-376`), a forward edit while replay is yielded is BUSY. Does input queue (A9 order) or is replay
    cancelled?
  - Depends on: the undo round-2 fix (v2-0fv.8), A9, and Q7 (journal acceptance is a UI call).
- **Q9 — The savectl state that PLAN's four axes omit.** "Prepared token retained after a failed attempt; fresh save
  forbidden until recover_finish/recover_cancel" (`savectl.h:104`, `can_recover_cancel` `savectl.c:589-590`).
  - Is it a fifth `save_phase` value (RECOVERY_PENDING), or is it CONFLICT_CANCEL generalized?
  - DIRSYNC_RETRY in PLAN covers only dirsync. v1 also has writeback loss (`recover_finish`, `savectl.h:162-166`).
  - Depends on: Bug 2's test design, and the PLAN §4.4 text (which would be amended).
- **Q10 — Dirty state.** With the editor history ring cut (§4.5(e)), "modified" is
  `content_id(current) ≠ content_id(saved)`. Who mints content ids: edit per committed command, with undo restoring old
  ids? v1 has `savectl_content_identity` (`savectl.h:84-92`): "Identity values name content states, never reused across
  undo branches".
  - Depends on: Q8 and undo's `group_serial`.
- **Q11 — Buffers sharing a path.** Both a forward save and generation retention affect every buffer naming the same
  file (`journal.h:183-185`; `session.c:696-736`; PLAN §4.4 FINISHING).
  - Does retention reference-count per path or per generation? What happens to two buffers whose undo histories name
    the same old generation, after one closes?
  - Depends on: Q1, Q2.
- **Q12 — `OS_FAULT_SLOTS` = 256 (M, v1 table size) vs tab capacity.** v1's live tab capacity is 1000 (`v1:src/main.c:248`,
  `.tab_capacity = 1000u`, (G)). Files at or above the copy threshold map, and each mapping registers one slot
  (`file.c:407`).
  - The 257th simultaneously open mapped file gets `OS_E_NOSPC` (`v2:os.h:356`). What does open do: copy instead
    (impossible above physical memory for 10 GiB), or refuse visibly? Reloads also register (fault spec), so a reload of
    one of 256 open mapped files needs a free slot before the old one retires.
  - Depends on: the copy threshold policy (G) and R16 (W3, fd limits).
- **Q13 — `case_tables.h` provenance** (4,400 lines). Generated? If so, from which UCD data and tool? A generated table
  needs a registered generator with `--check` (PLAN §5 lints) or it is a fossil.
  - Depends on: v1 `tools/` (not read here) and the utf8 UCD version (15.1.0, L1 dossier §2.1).
- **Q14 — `file_id` vs `os_stat`.** PLAN keeps "existing file_id with ctime_ns + ino_hi". `os_stat`
  (`v2:os.h:172-180`) already carries identity, size, mtime, ctime, perm, uid and gid. `file_id` adds `exists`,
  `metadata_valid` and `type`.
  - Collapse to `os_stat` + an absent flag, or keep `file_id` as the comparison type? One comparison function either way
    (§1.2 notes savectl's duplicate).
  - Depends on: journal BASE encoding (Q1).
- **Q15 — Input aimed at a still-opening buffer (A9).** Strict FIFO with head-of-line blocking? Or per-buffer queues
  that preserve order only within a buffer, while a Save of buffer B overtakes keys aimed at opening buffer A?
  - A9 says "events apply in the order sent".
  - What do R18 (save a small tab while a large one opens) and R29 (busy Ctrl+W during an open) require?
  - Depends on: the contract cases R18/R29 (W3) and §5.1.
- **Q16 — The headless editor's acknowledgement and input source.** The W1 exit needs "command acknowledgement" for the
  pre-/post-ack oracle, with no frames.
  - What carries input and acks in the headless binary? Candidates: the existing v2 `ipc` stream (`v2:os.h:389-436`,
    `ipc` L0), or a test-only `app_inject`/`app_ack` API.
  - The oracle says "a marker-only repaint, server key delivery and an EDIP open ACK are insufficient" (contract design,
    recovery paragraph). The W1 ack must be "command applied and journal-accepted", observable from outside.
  - Depends on: L0 ipc, the contract design's recovery rules, and W2's inspector binding.

## 7. Bead proposals (dependency order)

The shared acceptance block of the L0 dossier §7 applies to every bead below and is not repeated. It covers:
- Law 9 red → green pasted;
- `make check` incl. lints and the isolated `--whole-archive` link (here `libpersist.a` against L1+L0, and `libapp.a`
  against L4..L0);
- no `#include "*.c"`;
- no Windows code;
- per-test temp dirs, never v1's profile;
- decision notes;
- power-stamped benches as records.

Labels `L2-n`/`L5-n` are dossier-local. "Q*n*" means the bead may not start until the reviewer's answer to §6 Q*n* is
recorded in a decision note.

### L2-0 — `os` seams for persistence (an L0 bead)
- **Scope:** `src/os/os.h` + `os_posix.c`: a directory-change watch object (init/add/remove/read; fd into
  `os_waitset`), fd → fd metadata copy (owner + xattr set with v1's rules, `v1:file_posix.c:184-293`), and `os_link_at`
  only if Q2 chooses it. Grants in `tools/system-symbols.txt` (`inotify_*`, `fchown`, `*xattr`, `linkat`) for `os`
  only. Tests: `tests/os_test.c` cases with `--wrap` on each new syscall, each wrap asserted to fire.
- **Deps:** none inside L2. Q2 only for the link primitive (it can land without it).
- **Acceptance:** watch events for create/rename/delete/modify of a watched file and its parent; metadata copy
  round-trips xattrs and preserves a matching immutable label; zero heap allocation in each (`base_malloc_guard`); no
  global (§0.3).

### L2-1 — `file`: acquisition, fault registration, change detection, save transaction
- **Scope:** `src/file/{file.h,file.c}`; a `FILE_TEST_HOOKS` variant (step hook); tests ported per §1.6; fuzz; bench;
  decision note covering the job-state enum (§1.5(e)), the NOREPLACE fallback on `OS_E_NOTSUP`, close as two phases, and
  the watch object.
- **Deps:** L2-0; piece 7b (`piece_open_mapped`/`piece_open_copy` real); L0 work + os fault (landed).
- **Acceptance:**
  - Red first: absent target + a file created after the absence check is never overwritten (EXIST → CHANGED).
  - Fault-spec L2 row and integration tests (§1.4), including "block a running job, cancel, release; backing survives
    to physical acknowledgement and retires exactly once".
  - Same-size in-place overwrite with no SIGBUS detected by identity.
  - `file_save_begin` takes a snapshot; typing on the live tree during the save is unaffected.
  - Each of the six step hooks fires and is killable.
  - Fsync and dirsync failures are each injectable via `--wrap=os_file_sync,--wrap=os_dir_sync`, asserted to fire.
  - Zero UI-thread sleeps or spins (grep proof + close-while-saving test).
  - Reload acquisition through the same registered construction.

### L2-2 — `journal` core: v2 wire, typing-path append, worker transport, replay into piece
- **Scope:** `src/journal/{journal.h,journal.c,private.h,path.c,session.c}`. File header (Q5). Record set: BASE,
  INSERT, DELETE, VIEW, TABS, SAVE, TARGET, GROUP_BEGIN/COMMIT, plus **BASE-range restore** and **generation
  declaration** (Q1). CRC32C with CPU selection through base (§2.3). Two state enums (§2.6). Checked allocations with
  per-site derivations. `journal_apply_piece` on the v2 piece API. Tests ported per §2.7 (core subset).
- **Deps:** Q1, Q5, Q6, Q7 answered; piece 7b; L2-1 only if `file_id` stays (Q14).
- **Acceptance:**
  - The PLAN §4.2 items and oracle at journal level (kill at every group boundary: AFTER_BEGIN/AFTER_DELETE/
    BEFORE_COMMIT/AFTER_COMMIT, v1 `journal_kill_test.c:145-182`).
  - Unknown version refused **without truncation** (red on a v1-style truncating scan).
  - Unknown record inside a known version follows the stated rule.
  - Group atomicity under torn tails.
  - The BASE-range record round-trips and is rejected on kind/bounds/overflow/source-identity mismatch (spec :137).
  - Typing-path append and `insert_step` under the release malloc guard, `active && count == 0`.
  - `make check-tsan` on the journal suites.
  - Fuzz with version/BASE-range/generation ops.

### L2-3 — `journal` save transaction, generation retention and startup retirement
- **Scope:** `journal_save_prepare/finish/cancel`, `journal_retry`, retention per Q2 (and the async retain),
  `retire.c` (startup inventory, references incl. generation records), session lock files. Tests:
  `journal_retire_test`, `journal_os_test` (via `JOURNAL_TEST_HOOKS`), `journal_safety_test`, `journal_cancel_test`
  (rewritten).
- **Deps:** L2-2; L2-0 (link, if chosen); Q2, Q3, Q4, Q11.
- **Acceptance:**
  - The piece-pool row "Source conflicts, rotation and history lifetime" (:206) at journal level: Select All → Delete →
    Save empty → Undo → kill/restart with different old and new BASE generations; missing/mismatched generation;
    artifact retirement races.
  - Retention mechanism red→green for the chosen fallback order; the bead states which it implements and why (A14(b)).
  - Retained copy identity re-captured after any operation that changes ctime.
  - No UI-thread drain loop (Q7).

### L2-4 — `savectl` rewrite: four orthogonal axes, fd-captured BASE, conflict-cancel
- **Scope:** `src/savectl/{savectl.h,savectl.c}`, rewritten. Snapshot-only requests. Reload through `file`. Tests
  ported per §3.6. Decision note with the axis table (§3.5) and the Q9 answer.
- **Deps:** L2-1, L2-3; Q9.
- **Acceptance:**
  - **Bug 2 / X2 red first** with the `FILE_STEP_RENAMED` hook (known-bugs test design, variants a-c).
  - Every axis an enum handled by `switch` under `-Wswitch-enum`, no `default:` (CLAUDE.md FSM rule; reviewer reads for
    coordinating booleans).
  - The new BASE comes only from the fd-captured identity.
  - FINISHING rebases every buffer sharing the path and preserves post-cutoff edits.
  - SIGKILL at each of the six steps leaves old-or-new bytes; failed fsync and failed dirsync reach their distinct states.
  - No worker parked on a UI handoff (§3.5).
  - `make check-tsan` on savectl.

### L2-5 — `edit` kernel: selection, transaction, journal emission, maintenance
- **Scope:** `src/edit/{edit.h,edit.c}` replacing the stub: the selection; the transaction enum (§4.5(f)); preflight
  (journal staging + undo inverse; piece self-admits) → checkpoint → undo provisional → journal acceptance → commit;
  emission (by-value ADD/literal, BASE-range for ORIGINAL undo runs); the change record; `edit_maintain` (resolve →
  replenish → reclaim + `undo_maintain`, the only caller). Tests: transaction unit suite; `journal_group_test` and
  `editor_undo_branch_test` re-targeted; Law 2 guard over type/delete/replace/undo/redo.
- **Deps:** undo v2-0fv.8 landed with MUST-FIX 2 resolved per Q8; L2-2; piece 7b; Q10.
- **Acceptance:**
  - PLAN §4.2 at the kernel; Astra's MUST-FIX 2 trace as a red test.
  - A13 bounded-copy gate: min(L, 16 × spans) ORIGINAL bytes copied over delete + undo; one-byte literal and one-span
    large fixtures; 131,072 B read ceiling counted on the real tree (A19).
  - The A19 exactness fixture (513-chunk delete + undo, line queries before/between/after, O(h) visits counted).
  - Refusal is visible and changes nothing (piece-pool "Failure and retry").
  - Release malloc guard `active && count == 0` over input → mutation → staging.
  - Maintenance order asserted by a single call site (grep) and a wrapped-call order test.

### L2-6 — `edit` command plans harvested from `view/edits.c`
- **Scope:** plan producers (§4.1 list) as layout-free functions (soft-row bounds and comment style as arguments);
  `case_tables.h` with its generator or a recorded provenance (Q13); `view_test`/`view_fuzz` ports.
- **Deps:** L2-5 (the plan type and apply path); Q13. Parallel with L5-1.
- **Acceptance:** plan purity (no mutation/allocation; guard-proven); byte-model differential fuzz; `edit` links with no
  `layout` symbol (`check_symbols`).

### L5-1 — `app_loop` + `app_buffers`: straight-line step, buffer lifecycle, counting job
- **Scope:** `src/app/{app.h,app_loop.c,app_buffers.c}`, `src/main.c` (headless). `app_step` with named cluster steps;
  one `app_work_pending`/`app_next_deadline_ns`; the non-reentrancy ASSERT; the in-order input queue (A9, Q15); the
  buffer lifecycle and kind enums; the process `piece_reserve`; the identity-keyed open lookup; the `piece_lines_count`
  job with adoption; the watch object; the headless input/ack channel (Q16).
- **Deps:** L2-1, L2-5; Q15, Q16.
- **Acceptance:**
  - PLAN §4.7 items.
  - A19's counting-job line, including refusal of a batch for a reloaded or closed buffer.
  - The fault-spec "Physical work retirement" row.
  - A9 as a test: keys then Save, sent while the buffer opens, apply in order.
  - No spin and no sync wait on the UI thread (each close/exit path is a state).
  - Release malloc guard over input → submit (headless "submit" = ack).

### L5-2 — `app_session`: immutable leaves, checkpoint job, startup recovery
- **Scope:** `src/app/app_session.c`: leaves copying `{path, slot, origin, view_state}` + owned snapshot + saved
  reference; the checkpoint job (one pool, Q7); `text_ops` with the A14(c) rule; recovery (replay into trees, Q3);
  partial restore (unsaved first, set-aside on drop).
- **Deps:** L2-2, L2-3, L5-1; Q3.
- **Acceptance:**
  - The piece-pool rows "Published and captured app_session leaves" and "Revision accounting".
  - Checkpoint-diff tests for overlap, duplicate runs and a saved BASE differing from the tree origin (spec :143).
  - R31 pre-/post-ack controls headless, file and untitled.
  - R4 kernel: partial restore puts unsaved first.
  - `make check-tsan` on app_session.

### L5-3 — `app_save` and the Wave 1 exit suite
- **Scope:** `src/app/app_save.c`: save/save-as orchestration over savectl, shared-path rebase, retention ticks. The
  Wave 1 exit integration suite: open → edit → save while typing → post-rename conflict → SIGKILL at every save step +
  failed fsync/dirsync → restart oracle; X3 headless (delete 33,619,968 B mapped, undo, Save As, compare); R10 headless.
- **Deps:** L2-4, L5-2.
- **Acceptance:**
  - PLAN §6 Wave 1 exit, every bullet that names these modules.
  - Each `dataloss_test` case ported (§5.5) as its own named test.
  - Save while typing: the `post_cutoff` shape.
  - `make check-tsan` green on `work`, `piece`, `journal`, `savectl`, `app_session` (PLAN §6 list).

### Handed off / tracked elsewhere
- Frame acknowledgement, `saving_frame_pending`, PAINT/WINDOW records and the G8s hook: W2 app_paint.
- A17's per-tab operation identification: W3 session-restore bead (candidates in §5.2).
- A11/A12: W2 layout and X11 present.
- Manual replay CLI and `journal_acknowledge`: deferred (§2.1).

## 8. Later-wave leaves that could start now

**Criterion.** Every include has landed on v2:
- L0: os, base, trace, work, ipc;
- L1: scan, utf8, indent, piece (header frozen; 7b pending), find (landed 1.19); undo is pending.

Further conditions: no display code (X11/Wayland/GL/raster/render), and no design that the Wave 0 gate or the piece
implementation could still change. Includes are from the v1 census at `6fec2e8`. Everything in PLAN §3's L3/L4 rows was
checked:

| Candidate (layer) | v1 files, lines (M) | Includes | Verdict |
|---|---|---|---|
| **`dnd`** (L4, PLAN "harvest verbatim") | `src/dnd/dnd.c` 162, `dnd.h` 42; `tests/dnd_test.c` 241, `fuzz/dnd_fuzz.c` 137 | `dnd.h:4-5` base, ipc; `dnd.c:1-2` dnd, utf8 | **Qualifies.** All includes landed, no display, no globals, no `.c` includes in tests. Strip `_WIN32` blocks at `dnd.c:29,44,57,75,99` (UNC/drive URIs). Not `tests/x11_dnd_test.c` (X11, 242 lines). |
| **`config` TOML parser** (L4) | `src/config/parser.c` 330 | `parser.c:1-2` `config/private.h`, utf8. `private.h:3-4` → `config.h` → **`keys/keys.h` → `x11/plat.h`**, `view/view.h` | **Qualifies only after a split.** `CONFIG_ENTRY_CAP = KEYS_ACTION_COUNT + 10` (`config.h:23-25`) and `CONFIG_NAME_CAP` (`config.h:20`) size the parser's fixed arrays (`parser.c:7-18`); `VIEW_SCAN_BOUND` 4096 (`view.h:9`) also enters (`parser.c:29`). Harvesting now with literal caps would invent numbers whose derivation (the cmd action count) does not exist yet (Law 4). The harvest is legitimate only if the parser takes its capacities from the caller (caller-provided storage). PLAN's "private pool" does not exist in v1's parser: all state is a stack `struct parser` with fixed arrays. The only pool in config is `runtime.c`'s reload arena, `runtime.c:12-14`, which also includes `file/file.h`, so it is out. The app-identity path is in `config_posix.c:5-15` (hard-coded `sublimite`), which the isolation rule rewrites in the settings bead, not here. v1 tests all include `config.h` (keys) or `editor/private.h`, so new public tests are needed. |
| **`font` hostile-input validator** (L3) | `src/font/font.c` (validators `in_buf` :89, `find_table` :106, `glyf_range` :184, `shape_ok` :203, `glyph_checked` :277, `cff_*` :359-488), `cff_guard.h` 327 | `font.h:12` → **`render/render.h`**; `font.c` also includes `dwrite.h`, `ui_raster.h`, atlas headers | **Does not qualify.** The validator is not a separable unit: it is static functions in `font.c`, sharing the thread-local `font_stb_cur` (`font.c:34`, v1's "one deliberate exception to the no-globals rule"). Splitting it requires deciding `font.h`'s v2 shape, which depends on `render_types` (PLAN §2 L3 order `render_types < font`), not yet designed. |
| **`font` GSUB core** (L3) | `src/font/gsub.h` 82, `gsub.c` 569; `fuzz/font_gsub_fuzz.c` 99, `tests/font_gsub_test.c` 380 | `gsub.h:11-12` stdint/stddef only; `gsub.c:6-7` `font/font.h` (render), utf8 | **Conditional.** `font_gsub_parse`/`font_gsub_apply` (`gsub.h:56,65`) are byte-level ("no allocation, no globals"). Only `font_gsub_cluster` (:79) takes `struct font *`. A bead could land `gsub.{h,c}` minus `cluster`. Recommendation: **hold**. The cluster API is half its consumer surface, its tests include `font/font.h`, and the font bead's shape is undecided. |
| **`fif`** (L4) | `src/fif/` 842 total (`fif.c` 176, `worker.c` 280, `filter.c` 150, `fif.h` 130, `private.h` 60, `fif_posix.c` 36, `fif_win.c` 10). PLAN §3 says "748-LOC"; the basis of PLAN's figure is unknown (count (M) here) | `fif.h:6-9` base, find, work, os; `private.h:4` **`find/options.h`** | **Does not qualify yet.** v2 `find` folded `options.h` into `find.h` (`v2:src/find/` has no `options.h`; `find_regex_compile_options` is in `find.h:136`). L1 find's visit API was unified (1.19), so fif's worker must be re-cut onto it. PLAN's "shared find meter" ties fif to `find_session` (W3 rewrite, undesigned). A walk/dedup-only bead is possible but would split one module across two beads for no input-visible gain. |
| `hl` (L4) | `src/hl/` (`parse.c` 249 includes `file/file.h`) | `hl.h:4-6` work, piece, tree-sitter | **No.** PLAN §3 marks hl "rewrite cleanly" (opaque `hl_session`, budgeted `hl_query_run`, enum lang IDs). It uses the v1 piece snapshot API, which changed (frozen v2 `piece.h`), and `parse.c` needs `file`. |
| `config/actions.inc` (L4) | 165 | none | **No.** It is cmd's action table. PLAN §3 rewrites cmd (unify keys/menu/palette/actions.inc). |
| `raster_kernel.c` (L3) | 366 | `raster/raster.h` → `render/render.h` | **No** (render; also display-path code). |
| `keys`, `menu`, `palette` (cmd) | 485 + 93 + 101 + 112 (+ headers) | `keys.h:6` `x11/plat.h` | **No** (plat; cmd is a rewrite). |
| `tabs`, `minimap` (L4) | 302 + 121; 713 + 289 | render, undo, view; lineidx | **No.** |
| plat, x11, wayland, render, raster, gl, font_async, viewport, layout, find_session/panel, replace_txn | — | display or undesigned | **No** (excluded by the criterion). |

**Proposed leaf bead (one).**

**LF-1 — Harvest `dnd`** (L4, early).
- **Scope:** `src/dnd/{dnd.h,dnd.c}` minus `_WIN32`; `tests/dnd_test.c`, `fuzz/dnd_fuzz.c`; `tests/lanes.mk` rows.
- **Deps:** none beyond landed L0/L1.
- **Acceptance:** the shared block; the URI-list parsing differential against v1's test expectations; fuzz-smoke seed.
- `mk/layers.mk` already lists `dnd` in `L4_MODS`.

**Conditional leaf** (needs an advisor ruling first): **LF-2 — TOML parser as `config/toml.{h,c}` with caller-sized
storage.**
- Ruling needed: does caller-sized storage satisfy PLAN's "transactional TOML parser + private pool"? "Transactional"
  is v1's all-or-nothing visit (`config_toml_parse_worker`, `parser.c:271`), and a private pool does not exist in v1.
- **Scope:** split `parser.c` off `config.h`; new public tests and fuzz, since the v1 ones include keys/editor headers.

VERDICT: L2 and minimal L5 are harvest-plus-rewrite. `file` and `journal` carry most of v1's protocol knowledge with
targeted fixes: NOREPLACE absent target, syscalls moved into `os`, no work-pool internals, no UI spins, a versioned
journal header, checked allocations, two state enums instead of seven booleans. `savectl`, `edit` and `src/app/` are
rewrites whose v1 evidence is cited by symbol. `edit` is assembled from `view/edits.c` plans plus the transaction half of
`editor/input.c`, which today has no checkpoint and keeps partial compound edits. Sixteen derivations (§6) must go to
the reviewer before briefs are written: six change the journal wire, retention or savectl FSM (Q1-Q5, Q9), and Q7 (one
pool) and Q8 (edit/undo transaction) change module boundaries. Ten beads are proposed (L2-0 … L2-6, L5-1 … L5-3), all
behind the Wave 0 gate, piece 7b and the undo round-2 fix. Only `dnd` qualifies as a later-wave leaf now, with the TOML
parser conditional on an advisor ruling.
