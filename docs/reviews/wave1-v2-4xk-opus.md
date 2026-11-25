# Re-check: v2-4xk, the L2-1 file follow-up (Opus, adversarial)

Change re-checked: `git diff dc2d874 review/4xk` (the bead, dd28e63, on v2 dc2d874), applied with one `git apply` to
a v2 worktree at 4820416. The apply was clean. It printed one whitespace warning: a blank line at the end of the
L2-1-file.md hunk. `src/file/file.c` builds (`base-build.log`). The bead's record is
`docs/decisions/L2-1-file.md` §14 (revised) and §16 (new). Earlier reviews of this module:
`docs/reviews/wave1-L2-1-opus.md` and `-recheck.md` to `-recheck4.md`.

Scope (advisor), limited:
- O1, O2 and N-C;
- the recover rule restated in os terms;
- a rerun of the earlier mutant sets, regressions only, plus at least six new mutants;
- the §14 rows the bead changed.

Method:
- Reading of the file.c/file.h diff against `wave1-savectl-spec.md`.
- `build/review-probe/probe7.c` is new. It is built from a clean copy of the bead's file.c (`p7/file_orig.c`)
  against copies of the san archives, with `--wrap=os_file_pread,os_map_file`. Its three probes:
  - `splice`, an O1 attack;
  - `stale`, the pre-reload snapshot;
  - `fds`, descriptor counts across open, reload, failed acquisition and close for both kinds.
- `build/review-probe/run_all.py` applies one mutant, rebuilds the san `file_test` and the rel `law2_file_test`
  (plus san `os_test` for os mutants), runs each suite whole, records the first failure, and restores. It takes
  five sets:
  - `r`: the reviewer's round-5 set, `m_r.py`, a copy of re-check 4's `mutate5r.py`;
  - `w`: the round-5 worker set, `m_w.py`, a copy of re-check 4's `mutate5w.py`;
  - `x`: this bead's worker set, `m_x.py`, a copy of `build/v4xk/mutate_x.py`;
  - `e`: earlier mutants whose code moved, re-expressed, `m_e.py`;
  - `n`: new mutants, `m_new.py`.

  Every set runs the whole suites here, not only the named test. Logs: `build/review-probe/run-{r,w,x,e,n}.log`.

Stamp: BAT0 = Charging. load1 was 51.6 at the start (M), 2026-11-25 22:07 CET, from other work on the box. No
number below is a timing.

Codes used below:
- status: 0 OK, 5 CHANGED, 7 STATE, 12 CONFLICT, 13 SOURCE;
- pub: 1 PRE_RENAME, 4 VERIFIED, 5 CONFLICT.

## Verdicts

### 1. O1, the source taken from the snapshot: FIXED for every backing file built; one forged shape gets through (F1)

Every save path goes through `temp_prepare_core`:
- the one-shot `file_save_write`;
- `file_temp_prepare`;
- the async `save_job_fn`.

That function takes the backing from `piece_view_backing(piece_snapshot_view(snap))`. It refuses a backing whose
callbacks are not file's (file.c:529–530), retains `sb->ctx` (file.c:539), and validates it after the write
(file.c:624–626). Every early return after the retain goes through `temp_release`, which releases the source. No
caller can lend "no source" any more, and the async path no longer lends `f->backing`. Before the bead it lent the
current backing even for a snapshot taken before a reload, so it validated the wrong inode. The fuzz harness and
the bench were updated for the new signature.

No path runs a mapped save without validation, for a mapped snapshot whose backing file built.

A stale backing is accepted, and it is right to accept it (probe `stale`). A snapshot of the tree from before a
reload is saved, after the reload, with the new baseline as its expectation:
- after a foreign **atomic** replace: status 13, pub 1, and the foreign file is kept;
- after a same-size **in-place** rewrite with the mtime restored: status 13, pub 1, and the foreign file is kept.

The atomic case is refused because the old inode lost its link, which moves its ctime. That is the MF6 ruling: a
ctime move is indistinguishable from a rewrite, so the snapshot's bytes cannot be shown intact. SOURCE is the right
code for both.

A backing from another tree of another `file` is validated against its own source. That is right too: those are the
bytes the snapshot reads.

**The gap (F1).** `backing_is_ours` checks only the callbacks and a non-NULL ctx. It does not check that the
backing's bytes, length and fault id are its ctx's own. `file_backing_validate(NULL, …)` does check them (A3c, A3d).
So a backing copied from file's own with another mapping spliced in passes prepare, and prepare then validates the
wrong mapping. See F1 for the input and the result.

### 2. O2, a copy closes its original's descriptor: FIXED

`acquire_work` keeps the fd local through the read or map and through the after-read check (file.c:1333–1342). It
then moves the fd into the backing for MMAP only (file.c:1344–1355); `done:` closes the local fd. `backing_new`
starts every backing with `fd = OS_FILE_INIT`, and `os_file_close` is a no-op on −1 and sets −1. So the paths that
release an unpublished backing close nothing twice:
- the copy read failing;
- the after-check failing;
- a cancel.

Remaining readers of a backing's fd, by `grep '->fd'` over file.c:
- `file_source_validate`, which returns OK at once when the fd is closed;
- `file_check`'s source leg, which runs only while the fd is open;
- `source_rebaseline` and the before-stat in publish. These are reached only through `source_displaced`, which needs
  `t->source`. A copy tree has no piece backing (`tree_from_backing` uses `piece_open_copy`), so custody never holds
  a copy's source.

`file_resolve_keep` reaches a copy only through `file_source_validate`, which gives OK. Nothing reads a copy's
original after acquisition.

Descriptor counts from probe `fds`, each relative to the count before the open:

| Step | Copy | Mapped |
|---|---|---|
| open | +0 | +1 |
| after 3 reloads, each after a foreign atomic replace, with the old tree closed | +0 | +1 |
| a reload that fails with the backing already built (a swap inside `pread` or `os_map_file`; status 5) | +0 | +1 |
| close | +0 | +0 |
| an open that fails the same way (status 5) | +0 | +0 |

ASan was clean throughout. A double close of a reused descriptor number would have shown as a count below the
starting count; none did.

### 3. N-C, FILE_ERR_SOURCE: CORRECT on every path that reports one side; untested where both sides change (F2)

The source side:
- prepare's post-write validation is SOURCE, PRE_RENAME, with the temp discarded (file.c:625). Under savectl that is
  "a source drift before the rename ends the attempt".
- a FOREIGN pair after the rename is SOURCE with pub CONFLICT (`temp_verify_installed`, file.c:697–700), so savectl
  goes to CONFLICT_CANCEL. `out->installed` is filled, as file.h says.
- `file_temp_verify` returns SOURCE only after `witness_check` has passed, with its outputs filled (file.c:1003–1010).

The destination side:
- the metadata open finding another inode (file.c:490) is CHANGED;
- the pre-rename recheck (file.c:817–819) is CHANGED;
- NOREPLACE EXIST is CHANGED;
- `temp_proven` failing after the rename is CONFLICT with pub CONFLICT (file.c:689–693);
- the HELD retry without proof is CONFLICT;
- a witness mismatch in verify is CHANGED.

A FOREIGN pair from an unobservable before-stat or after-stat (`before_err`) is SOURCE. It concerns the source fd
only, so that is the right side. A source drift after prepare and before the rename is not rechecked in publish.
The bytes are already in the synced temp under their CRC, so the drift cannot reach the output. Where source and
destination are one inode, the drift shows as a destination change, CHANGED, PRE_RENAME. That is correct for the
destination, and savectl's IDLE_CHANGED then validates the source itself.

When both sides change after the rename, a foreign destination and a FOREIGN pair, the code reports the destination
first (CONFLICT), which is the more severe class, and leaves `installed` unfilled. Nothing pins that order (F2,
mutant V9).

### 4. The recover rule in os terms: MATCHES

file.h:413–423 against file.c:1033–1043:
- NOENT, NOTDIR and ACCESS give CHANGED;
- LOOP and ISDIR give NOTREG;
- every other os_err gives IO and keeps the obligation.

`map_errno` (os_posix.c:42–64) supports the "OS_E_IO also carries EMFILE and ENFILE" clause: neither is mapped, so
both fall to the default. The same classification applies to `split_path` and `os_dir_open` failures, because `e`
is shared. That fits "the os_err the open reports": a missing directory is CHANGED, a non-directory component is
CHANGED, and the root is NOTREG. Neither os_posix.c nor os.h is in the bead's diff.

## 5. §14 rows the bead changed

Checked against the applied file.c:

| Row | Claim | Verdict |
|---|---|---|
| A2 | `file_source_validate` at 326: mapped only; a copy validates by construction | Correct. The gate is at 326, the function at 321. |
| A5 | prepare at 530, 539 and 625; the source is the snapshot's own; a foreign mapping refused; A5b inexpressible; A5c deleted as "unreachable-distinct" because the source's poll "polls the same fault id" | Lines and the A5b claim are correct. The A5c justification holds only for a backing file built. For a backing that passes `backing_is_ours` with another fault id, the deleted poll on `sb->fault` was the only check that saw the fault (F1). The claim needs the qualifier, or the check that makes it true. |
| A6 | `source_displaced` at 732; mapped by construction; A6a gone | Correct. A copy tree has no backing, so custody has no source. |
| A8 | `file_check` at 1919; mapped by construction | Correct. |
| A9 | Keep at 1960 | Correct. |
| A11 | 1333–1355: after-read check, then only mapped keeps the fd | Correct. |
| deleted `file_save_begin` row | the loan is gone | Correct (file.c:2067–2073 lends nothing). |

Missing from (a): the two consumers of the pair verdict are now source sites with their own return code, and neither
has a row. They are `temp_verify_installed` (file.c:697) and `file_temp_verify` (file.c:1010), killed by NCa and NCb
(§16). That belongs in the backlog, not a fix.

## 6. Mutants

Every mutant ran against the whole san `file_test` and rel `law2_file_test` (plus `os_test` for os mutants). The
sources were restored and `cmp`-checked against the saved originals after every set.

| Set | Rows | Killed | Survived | No longer expressible | Build failed |
|---|---|---|---|---|---|
| r (reviewer, round 5) | 93 | 79 | 3 | 11 | 0 |
| w (round-5 worker) | 57 | 46 | 1 | 9 | 1 |
| x (this bead's worker) | 29 | 28 | 0 | 0 | 1 |
| e (re-expressed, moved code) | 12 | 12 | 0 | — | 0 |
| n (new) | 15 | 12 | 3 | — | 0 |

**No regression.** Every earlier mutant whose code still exists is killed, or survives exactly as it did in re-check 4.

The earlier survivors:
- **N1 (mine)**: the destination's identity is dropped from NOT_DONE. It still survives. The bead's "N1" is a
  different and stronger mutation (`tmp_ours || …`), which is why §16 lists N1 as killed. Mine is equivalent except
  in one case: a foreign hard link of our temp onto the destination, and then a rename that reports an error. With a
  real error, rename(2) between two links of one inode does nothing and succeeds. Backlog; no test asked.
- **R4j** (NOTDIR stays IO in recover) survives, as in re-check 4. It is backlog by ruling.
- **R5n and A5b** survive by construction. Both inject behaviour for a copy source, and a copy has no source now.
  Both are equivalent.

The re-check-4 survivors R4d, R5f, R5h, R5i, R5j, U2, U3, U5 and U6 are now killed by the bead's `t_x_*` tests, so
the §16 claims hold under whole-suite runs.

Build failures:
- **w A11b** no longer compiles: the weakened line leaves `now` unused under `-Werror`. Its re-expression A11be is
  killed by `t_r5_open_identity`.
- **x N11** no longer compiles. The same mutation in set r (N11) is killed by `t_x_pair_reset`.

No longer expressible:
- **Re-expressed in set e, all killed:** M17, M19, M21/A5a, X18/A3b, R5a/A8a, R5b, R5m, A8b, A8c, A8d, A9 and A11b.
  The table below gives the killing test for each. Their code moved when `switch (mode)` was removed from
  `file_check`, Keep and `source_displaced`.
- **Gone by construction:**
  - R4e and R4f (a copy's source validated): a copy has no source.
  - R5l and A6a (the pair for a copy): no copy source and no mode gate.
  - R4g and A5c (the separate closing poll): deleted. See F1 for why it was not fully redundant.

| Re-expressed (e) | Killed by |
|---|---|
| M17e (Keep by poll only) | `t_change_in_place` |
| M19e (post-rename mismatch as PRE_RENAME/IO) | `t_save_foreign_after_rename` |
| M21e (prepare skips the source validation) | `t_r2_source_validation` |
| X18e (null-ctx validator accepts any callbacks) | `t_r3_null_ctx_validator` |
| R5ae (source leg for a copy) | `t_r2_abandon_while_publishing` |
| R5be (source leg dropped) | `t_sigbus` |
| R5me (the pair never runs) | `t_r2_validate_after_own_save` |
| A8be (ctime ignored) | `t_r2_keep_hardlink` |
| A8ce (fault poll dropped) | `t_r2_check_fault_poll` |
| A8de (TRUNCATED dropped) | `t_sigbus` |
| A9e (Keep skips validation) | `t_change_in_place` |
| A11be (fd stat not re-checked after the read) | `t_r5_open_identity` |

**New mutants (n)**, 15 in all: 5 of O1, 3 of O2 and 7 of N-C. 12 were caught and 3 were not:

| # | Mutation | Result |
|---|---|---|
| V1 | O1: prepare's ownership test drops the callbacks (any non-NULL ctx) | caught: `t_x_foreign_mapped_backing` (ASan, the foreign ctx read as a file_source) |
| V2 | O1: a foreign mapped backing proceeds with no source (the old NULL loan) | caught: `t_x_foreign_mapped_backing` |
| V3 | O1: prepare validates the source by its fault poll only | caught: `t_r2_source_validation` |
| V4 | O1: the source is validated but not retained in custody | caught: `t_r2_validate_after_own_save` |
| V5 | O1: the foreign refusal comes after the directory open and leaks the dir fd | **not caught** (F6) |
| V6 | O2: a mapped backing keeps no descriptor either (the fd gate fails open) | caught: `t_change_in_place` |
| V7 | O2: `file_check`'s source leg runs for a copy | caught: `t_r2_abandon_while_publishing` |
| V8 | O2: a copy skips the after-read check | caught: `t_r5_open_identity` |
| V9 | N-C: `temp_verify_installed` names the source before the destination proof | **not caught** (F2) |
| V10 | N-C: `file_temp_verify` names the source before the witness check | caught: `t_x_sides` |
| V11 | N-C: prepare's destination refusal (metadata open) named SOURCE | caught: `t_r2_fifo_destination` |
| V12 | N-C: publish's pre-rename destination recheck named SOURCE | caught: `t_save_absent_race` |
| V13 | N-C: a foreign destination after the rename named SOURCE | caught: `t_save_foreign_after_rename` |
| V14 | N-C: a FOREIGN pair leaves `out->installed` unfilled | **not caught** (F2) |
| V15 | N-C: NOREPLACE EXIST named SOURCE | caught: `t_r2_exist_race` |

## New findings

### F1 (MUST-FIX, contract): prepare accepts a spliced backing, and the deleted closing poll was its only guard

**Input** (probe `splice`):
1. Open A mapped (16 × 'A'). Take `*piece_view_backing(...)` of its snapshot.
2. Replace `bytes`, `len` and `fault` with a caller-mapped, registered view of file B (16 × 'B'). `ctx`,
   `acquire` and `release` stay file's.
3. Open a tree from it with `piece_open_mapped`.
4. Truncate B to 0.
5. `file_save_write` to an absent C.

**Wrong result:** status 0, pub 4 VERIFIED, and C holds 16 NUL bytes. B's fault id has epoch 1, unseen.
`file_backing_validate(NULL, &spliced)` returns false for the same backing. Before the bead the closing poll on
`sb->fault` refused this with CHANGED (re-check 4, A5c).

**Why:** `backing_is_ours` (file.c:337–341) checks the callbacks and ctx but not the identity fields. Prepare then
validates `ctx`'s own mapping and polls its fault id, not the one the snapshot read.

**Fix:** one helper shared with `file_backing_validate`'s identity block (file.c:356–361). It requires
`pb->bytes == b->backing.bytes`, `pb->len == b->backing.len`, and the fault slot and generation equal. Prepare
refuses with STATE when they differ. One test: the probe's splice, expecting STATE and nothing written.

**Triage:** no current caller builds such a backing, and Law 8 trial use is unaffected. But the bead's acceptance is
"refuses a mapped backing that file did not create", and §14 A5 deletes the poll on the strength of "one fault id".
The fix is three comparisons.

### F2 (NOTE): the SOURCE verdict's shape is unpinned

What the code does is correct. `temp_verify_installed` checks the destination proof before the pair, so a foreign
destination is CONFLICT with `installed` left unfilled. A verified destination with a FOREIGN pair is SOURCE with
`installed` filled (file.h: "our text is installed").

Two mutants of that order survive:
- V9 puts the pair first, so a foreign destination with a FOREIGN pair is reported as SOURCE, as if our text were
  installed;
- V14 leaves `installed` unfilled on SOURCE.

`t_x_sides` checks only the return codes, and each side only alone. Two pins are needed:
- a combined case: the R3_GROW hook plus a foreign rename over the destination at RENAMED, expecting CONFLICT;
- `out.installed.presence == FILE_PRESENT` on the publish SOURCE case.

Backlog: savectl's CONFLICT_CANCEL is reached either way, but it will read `installed` on SOURCE.

### F3 (NOTE): the fd-openness gate fails open

`file_source_validate` and `file_check` now decide "copy or mapped" by `os_file_is_open(&fd)`, not by the
`file_mode` enum. Today they agree, because only a published mapped backing holds an fd. But a mapped backing that
ever lacks its fd would validate OK silently. The old `switch (mode)` failed closed under `-Wswitch-enum`. A
`switch (source->mode)` with `case FILE_MODE_MMAP: ASSERT(os_file_is_open(...))` keeps the O2 behaviour and the
explicit-FSM law. Today the suite would notice: mutant V6, a mapped backing without its fd, is killed by `t_change_in_place`. So this is a design note (the explicit-FSM law), not a test gap. Backlog.

### F4 (NOTE): prepare drops the source's os_err

When the source's fstat fails, prepare returns SOURCE but does not copy `ve` into `*oserr`. This is pre-existing:
before the bead it was CHANGED with the same drop. The caller cannot tell an unobservable source from a drifted
one. Backlog.

### F5 (NOTE, for savectl's brief): which source WRITING rechecks

savectl's WRITING row says to recheck the source. After O1, the source the save read is custody's
`file_temp.source`. That is not `file_source_acquire(f)`, which is the current backing and differs when the snapshot
predates a reload. No file change.

### F6 (NOTE): the O1 refusal is not pinned for descriptors

V5 survives: `t_x_foreign_mapped_backing` checks the status, PRE_RENAME and that no destination exists, but not the
descriptor count. The code is correct today, because the refusal precedes `os_dir_open`. Add `count_fds() == fds0`
after the refused save. Backlog.

## Lanes

The lanes ran after every mutation was restored: `cmp` of file.c and os_posix.c against the saved originals. HOME was
under build/. Stamp: BAT0 = Charging, load1 26.7 at the start (M), 2026-11-25 22:39–22:56 CET. Logs are in
`build/review-probe/lane-*.log`.
- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:190-:195`: exit 0, `check: ok`.
  - check-layers: 6 isolated layer links;
  - `file_test: ok (85 tests)`, `os_test: ok`, `law2_file_test: ok`;
  - contract: PASS 64.
- `nice make -j2 check-tsan`: exit 0, 13 threaded suites passed, `file_test: ok (85 tests)`.
- `nice make -j2 fuzz-smoke`: exit 0, 16 fuzzers replayed their seeds.
