# L2-1 · file: acquisition, fault registration, change detection, the save transaction's file half, reload

Brief: `docs/briefs/L2-1-file.md` (round 1) and `docs/briefs/L2-1-file-round2.md` (round 2, advisor 2026-11-25).
Review: `docs/reviews/wave1-L2-1-opus.md` (ten MUST-FIX, N1–N9, 23 mutants). Header target: the signed errata §3, §4
and §5 of `docs/decisions/wave1-app-spec.md`, file.h and os.h parts only; the savectl and journal parts belong to their
own beads. Built against the real piece tree (1.23). The journal is not called anywhere: the seam is ordering, and the
caller writes SAVE(PREPARED) from `file_save_witness` between `FILE_MSG_SAVE_PREPARED` and `file_save_publish`.

Each mechanism below states its invariant, the simplest alternative, and the counter-example that defeats it.
Section 9 is the round-2 report: root cause, red and green per finding, the mutant table, sizes and lanes.

## 1. Acquisition and fault registration

**Mechanism.** One bulk job per open or reload (`acquire_work`): canonicalize, open by pinned directory + leaf
(non-blocking), fstat, verify that the canonical entry names the opened inode, read the prefix (≤ `FILE_PREFIX_MAX`,
2^20 B, (G) v1's bound), then copy below the threshold or map at or above it. A mapped view is registered with
`os_fault_register` before anything is published. Registration refusal disposes of the unpublished map and fd and
fails the open (`FILE_ERR_NOSPC`, Q12).

**Prefix custody (round 2, MF1).** The job record has a prefix part (arena, bytes, info, baseline, path, mode, size)
written before `FILE_MSG_PREFIX_READY` and never written again by the job. It also has a final part (backing,
status, oserr). Only the UI frees the record: decode on OPEN_FAILED, or close. The UI borrows the prefix at
PREFIX_READY (`FILE_OPEN_PREFIX`) and withdraws it at OPEN_FAILED (`file_prefix` returns NULL, 0). A failed job releases
only its unpublished backing.

**Invariant.** No readable byte exists outside a registered backing. No prefix reader outlives its bytes. Every
backing's final release retires its registration exactly once, before unmapping, on whichever thread drops the last
owner.

**Alternative defeated.** v1's two jobs (prefix, then open) defeat nothing but size. One job with an advisory prefix
message gives the same first-viewport latency. Round 1's "free the prefix on the worker when the job fails" is the
counter-example: the UI was reading those bytes (§9, MF1).

## 2. Identity: one expectation, one classifier

`file_expectation` is ABSENT or PRESENT(`os_stat`) (Q14). Observation returns ABSENT only for ENOENT/ENOTDIR; any other
failure is `FILE_ERR_IO` with the output untouched. `file_expectation_diff` is the only classifier. `os_stat` gained
`nlink` in round 2 (MF5 ruling). The classifier observes it but never compares it, because every link change already
moves ctime. `file_target` (erratum 3) is the borrowed path/expectation value; file defines it, and savectl copies it.

Path diagnosis (MF8, A15): a name never reaches an ASSERT. The results are:
- empty: `OS_E_INVAL`;
- trailing `/`, the root included: `OS_E_ISDIR` (`FILE_ERR_NOTREG` on open);
- `OS_PATH_MAX` bytes or more, or a leaf over NAME_MAX: `OS_E_NAMETOOLONG`.
A relative path resolves against the working directory. Round 1 accepted those, and the bench and tests use them.
Round 3 (review N-D): the async prepare rewrites the destination with its canonical directory on the worker, so a
relative or symlinked spelling of `file_path(f)` adopts its VERIFIED baseline exactly like the canonical one. The leaf
is kept: a symlink leaf is replaced, never followed, so it is another path.

## 3. Change detection, the validator, Keep

**file_check** combines three things:
- the path observation against the path baseline;
- the held fd against the backing's baseline, through the same classifier, ctime included (MF6), plus TRUNCATED for a
  mapping;
- one closing fault poll.
A failed fd stat is a change (N3).

**The one re-baselining point (MF5, advisor ruling).** The backing stores its acquisition stat immutably, plus one
`_Atomic int64_t ctime_ns`. The publish job does the following when its custody's source is the displaced inode (its
identity is the PRESENT expectation's):
- It fstats the source's held fd immediately before and immediately after its own rename.
- If the pair differs in ctime and a falling link count only, the after ctime becomes the baseline. That requires the
  before value to have been the baseline; otherwise the backing already fails validation, and stays failing.
- Any other difference, or an unobservable pair, is `FILE_PUB_CONFLICT`.
No other code path re-baselines. Round 1's Keep adoption of ctime is cut (MF6). ctime stays in every comparison,
because finding 6 needs it.

**Validator (erratum 5).** `file_backing_validate(ctx, backing)`. A non-null ctx is the struct file, whose installed
backing must be the supplied one. A null ctx selects the backing's own `file_source` through `piece_backing.ctx`, and
only for a backing file created (its acquire/release callbacks are file's). It does one poll and one fstat against the
baseline, takes no allocation and never waits.

**Keep.** A copy backing keeps any disk state. A mapped backing can keep a changed path only while the backing still
validates in full. Any ctime move of the mapped inode refuses Keep with the baseline preserved, and the caller reloads
(A23 rule 7).

**Counter-example that fixes the rule (the reviewer's `keep` probe, now `t_r2_keep_hardlink`).** Inode X has two names,
a and b. Rename c over a: X's link count falls 2 → 1 and its ctime moves. Write b in place at the same size and
restore its mtime: ctime moves again. The fd stat now shows a lower link count, the same size and mtime, and a moved
ctime. That is exactly what the dropped link alone shows.

**Simplest alternatives, defeated.** "Adopt any ctime move once the path names another inode" is round 1's rule, and
the trace defeats it. "Adopt a ctime move explained by a falling link count" is the review's suggestion, and the same
trace defeats it: the count did fall. A held descriptor defeats even "link count 0".

**Product consequence, named.** Keep after a foreign replace-by-rename of a mapped (large) file now always refuses.
So do Save and Save As of that buffer, because prepare's source validation refuses the same moved ctime. This is
MF-A (§10).

## 4. The save file half and temp custody (erratum 4)

**Custody states** (`file_temp_state`, each owning exactly what it says):

| State | What it holds and how it is reached |
|---|---|
| EMPTY | Nothing. |
| SYNCED | Prepare's handoff: a synced, unpublished temp. Publish renames only from here, at most once. |
| INSTALLED_UNSYNCED | The written fd and the original directory, after the directory barrier failed. The retry is barrier-only. |
| HELD | Both descriptors, after VERIFIED, RENAME_UNCERTAIN or CONFLICT, kept for verification, reconciliation or discard. |
| DONE | Nothing: discard or PRE_RENAME cleanup. |

Every owning state also holds the custody's retained `file_source`. Moving custody is a struct copy plus
`file_temp_forget`.

**Unlink authority.** Only `temp_unlink_own` removes a name. It runs only from SYNCED or a PRE_RENAME refusal, and only
while the temp name still names the held fd (identity check). An uncertain rename never unlinks the destination or a
reused temp name (MF2, M18).

**Prepare** does the following, in order:
1. It refuses a PRESENT expectation that is not a regular file, before any open (MF9).
2. It creates the temp `RDWR | CREATE | EXCL`. RDWR is new: verification reads the file back.
3. It writes S and computes the witness.
4. It does one closing poll and validates the retained source in full.
5. It applies metadata. The destination opens `READ | NONBLOCK | NOFOLLOW`. When that open is still the expected
   regular file, its metadata is copied (`os_file_copy_metadata`; the existing mode is kept). When it is another kind or
   another inode, the result is `FILE_ERR_CHANGED` without FORCE. When no copy happens, the mode comes from
   `os_file_set_perm`. A destination that exists keeps its observed mode: the expectation's for one the owner cannot
   open (MF-B, round 3: 0060 stays 0060), what is there now under FORCE. Only a name with no destination gets the
   new-file mode `0666 & ~os_creation_mask()` (MF7). Special bits travel only with the owner, that is through the copy.
6. It syncs and captures `fd_stat`.

**Mode (MF7, advisor ruling, a deliberate deviation from v1).** v1 gave a new file 0644 whatever the umask (v1
file.c:908/919 at 6fec2e8). v2 gives `0666 & ~mask`. The mask is captured once, by a constructor in os that runs before
main and so before any thread exists. Round 3 (advisor) replaced that: the mask is read once, at the first call, from
the `Umask:` line of `/proc/self/status`. A read changes nothing, so there is no window and no assumption about
threads or constructor order. If the line cannot be read, the mask is 077, the private side. No `umask(2)` call
remains in `src/`. A later change by the process is not seen. There is no os start entry point to hang the read on, and
a constructor would bring the ordering assumption back, so "at os start" is realized as "at os's first use of it". The two
modes agree under umask 022, and v2 never widens past the user's umask. The temp is created `0600 & ~m`, so it is never
wider than a new file's result (P: (0600 & ~m) ⊆ (0666 & ~m)). For an existing destination, the temp grants group and
other nothing the result lacks. It is owner read/write until the copy applies the predecessor's mode. Narrowing it
further would break the copy itself: setting a user xattr needs owner write on the inode, even through the fd.

**Publish** does the following, in order:
1. It rechecks the destination (unless FORCE).
2. It takes the before-stat of a displaced source.
3. It renames once, NOREPLACE for an absent expectation. EXIST and NOTSUP are PRE_RENAME: nothing was renamed, and the
   temp is cleaned up. Any other error is classified by observation, not by errno (round 3, review N-B):
   - the temp name still names the held fd: nothing was renamed, because a rename removes its source name. That is
     PRE_RENAME, and the temp is removed (still ours, definitely unpublished);
   - the destination names the held fd and the temp name is gone: the rename was performed despite the report, so
     publish continues;
   - anything else is RENAME_UNCERTAIN, and custody goes to HELD with nothing unlinked.
4. It runs the rebaseline pair (§3). The verdict, `file_source_pair` OURS or FOREIGN, stays in custody.
5. It syncs the directory, a FOREIGN pair included (round 3: the rename happened, so the barrier is owed). On
   failure, custody goes to INSTALLED_UNSYNCED.
6. It verifies that the path names the written fd, whose size and mtime still equal `fd_stat`. The rename moves only
   ctime, so a foreign in-place write into our installed inode is a CONFLICT (N2, answered: `fd_stat` is now used). A
   FOREIGN pair is CONFLICT at this and every later verification, so no retry turns it into VERIFIED. Custody goes to
   HELD.

**Retry** (`file_temp_retry_barrier`) only syncs the original directory and re-verifies. It never renames. From HELD,
after an uncertain rename, it first proves that the path names the held fd, and without that proof it syncs nothing.

**file_temp_verify** compares the named destination to the held fd. It then reads the fd back, checking identity, size
and the full content CRC32C against the prepared witness. Finally it re-takes the fd stat and the path identity, which
must be unchanged (a stable observation). It returns the post-rename expectation and the CRC32C over the first
min(size, 4096) bytes. 4096 is `FILE_WIRE_PREFIX_BYTES`, restating journal.h `JOURNAL_PREFIX_BYTES`, because
file < journal. The read-back buffer is 64 KiB: (E) the syscall count is size/65536, and the CRC dominates; it is not a
gate.

**file_temp_recover** opens a PREPARED's named candidate and its parent, non-blocking with no leaf follow. On a stable
match to the whole witness, it transfers them into INSTALLED_UNSYNCED. Otherwise it releases everything and leaves the
temp EMPTY. A symlink or directory at the name is FILE_ERR_NOTREG, a mismatch class (round 3, review N-F), never the
I/O error that savectl would retry forever. `file_save_write`, the one-shot form, discards retained custody before it returns.

**NOTSUP fallback (unchanged).** None. An absent target on a filesystem without atomic exclusion is refused, because
observe-then-REPLACE is the v1 race (Q14). N1 (the recheck-then-REPLACE window for a PRESENT target) stays open by
plain rename. Publish-by-exchange (`docs/decisions/wave1-publish-by-exchange.md`) is a later bead, by ruling.

## 5. Object lifecycle, messages, close

**FSMs (N4).** Every lifecycle is one enum handled by `switch` under `-Wswitch-enum`, with no `default:` on any of
them:
- `file_open_state`: NONE, PREFIX, READY, ATTACHED, FAILED. It replaces `prefix_ready`, `open_ready` and `attached`.
- `file_reload_state`: NONE, ACQUIRING, READY. It replaces `reload_pending`.
- `file_save_phase`: IDLE, PREPARING, PREPARED, PUBLISHING, UNSYNCED, HELD, CANCELLING. It replaces `save_abandoned`.
- `file_save_op`, which replaces round 1's `int` with `default: ASSERT`.
- `file_watch_state`, which replaces `watching`.
- `file_job_progress`, which is worker-private across continuations.
`tree_from_backing` lost its `default:`. The one remaining `default:` cases sit on `work_msg.kind` (a `uint32_t` wire
value, not an enum) in decode.

**Messages (N8).** Decode accepts a message only if its `work_msg` slot, epoch and generation equal the handle and
generation of the file slot it names. A superseded job, or a message to reused storage, is refused. `file_close_begin`
cancels every handle the file ever submitted, finished or not, so every undrained message of this file is stale in the
pool. Storage may then be freed after `file_close_finish`. The single-consumer rule for `file_msg_decode` stays as
file.h states it.

**Close (MF3).** Readiness is physical completion of every handle (`work_handle_finished`). It no longer requires a
decoded result. Finish disposes of undelivered records: the backing and the prefix of a dropped OPEN or RELOAD result.

**Async save phases (MF2, MF4).** SAVE_DONE settles by custody:
- PRE_RENAME: IDLE.
- INSTALLED_UNSYNCED: UNSYNCED.
- VERIFIED, RENAME_UNCERTAIN, CONFLICT: HELD. A VERIFIED save to `file_path(f)` adopts the installed baseline.
`file_save_begin` is BUSY while any custody is held. `file_save_abandon` discards from PREPARED, UNSYNCED or HELD.

With a job in flight, abandon cancels it, and the phase becomes CANCELLING. Once the handle is physically finished, the
next non-const call settles it from the job's record, exactly as its message would have:
- a synced, unpublished temp is discarded;
- every exposed effect is kept (the verified baseline is adopted; UNSYNCED or HELD custody stays for the caller).
The classified outcome stays readable.

**No I/O in const queries (N5).** `file_save_busy` and `file_save_phase_of` are pure. busy is
`!work_handle_finished` in CANCELLING, and phase_of reports the phase the settle will produce. The reap (unlink, close)
runs only from non-const entry points: abandon, begin, publish, retry, check, close.

**Alternative defeated.** Round 1 reaped from the const queries through a cast. That made a query an unlink. It also
discarded RENAME_UNCERTAIN evidence and skipped the VERIFIED adoption (MF4).

## 6. Reload and the watch

Unchanged from round 1, apart from the enums. Reload acquisition lives in file (§1.5(h)). `file_reload_tree` adopts the
new backing, and until then the old tree, registration and baseline are untouched. The watch is an owned `os_watch`,
one per file, with no process global.

## 7. What v1 kept, what was cut

Kept:
- the temp-name shape (the NUL is now counted for two 20-digit counters, N9);
- the six step hooks (`FILE_TEST_HOOKS` only);
- the prefix and EOL scan;
- the copy-versus-map threshold;
- metadata copy;
- the order write → sync → rename → dirsync → verify.
The mode rule changed deliberately (§4).

Cut:
- `file_id` and the second comparator;
- `file_snapshot_backing`;
- `file_attach`;
- the global watch registry;
- `_WIN32` blocks;
- the UI close spin and the worker publish sleep;
- round 1's Keep ctime adoption.
Round 1's "permission bits are not preserved / no fchmod seam" text is withdrawn. 1.28 copies the mode, and round 2
adds `os_file_set_perm` and `os_creation_mask`.

## 8. os changes carried by this bead

- `os_stat.nlink`.
- `os_file_set_perm` (fchmod through the descriptor; `fchmod` was already granted to os).
- `os_creation_mask`: read once from `/proc/self/status`, 077 if unreadable (round 3). The round 2 `umask` grant is
  withdrawn from `tools/system-symbols.txt`.
- Two process globals in `os_posix.c`: `creation_mask` and its `os_once` guard. They are written only inside the once
  and read only afterwards. This entry is the decision record CLAUDE.md requires for a global not named in PLAN.

Tests:
- `tests/os_test.c` `case_modes` covers the mask against the live umask (the test itself reads it with umask(2),
  outside src/), creation `0600 & ~m`, set_perm 0640 and 0600, and nlink 1 → 2 → 0.
- `tests/file_test.c` `t_r3_creation_mask` re-executes the suite twice. Under umask 027 the value is 027. With the
  `/proc/self/status` open refused (`--wrap=openat`), it is 077.

## 9. Round 2 report

Measurement stamp for the bench: (M)[AC] BAT0 = Not charging, load1 = 0.27, 2026-11-25 04:01 CET.
- open_copy_1MiB: p50 = 2,242,972 ns, p99 = 2,661,726 ns;
- open_mmap_1MiB: p50 = 992,194 ns, p99 = 1,619,466 ns;
- save_1MiB: p50 = 4,283,778 ns, p99 = 6,225,450 ns.
n = 100 each. These are tracked numbers, not gates.

### Red and green per finding

The red runs were on round 1 with the new tests (`build/r2/red-round1.log`), and the green runs on round 2
(`build/r2/green-round2.log`). Each test runs alone with `build/san/tests/file_test <name>`.

| # | Test | Red on round 1 | Root cause | Green |
|---|---|---|---|---|
| MF1 | `t_r2_prefix_after_failed_open` | `FAIL !file_prefix_ready(&f1)`: the borrowed prefix outlived its unmapped arena | The prefix had two owners. The UI borrowed it while the worker freed it on failure and rewrote the whole record (`*r = keep`). A coordinating boolean (`prefix_ready`) stayed true across OPEN_FAILED. | ok |
| MF2 | `t_r2_uncertain_rename` | `FAIL count_temps(dir) == 1`: discard unlinked the evidence, and async custody was unreachable | Custody had no state for "held after publish". RENAME_UNCERTAIN stayed SYNCED, which looked unpublished, so discard unlinked. The async phase dropped to IDLE with live descriptors. | ok (HELD; discard closes and never unlinks) |
| MF3 | `t_r2_close_after_dropped_result` | `FAIL os_now_ns() < deadline`: close never ready | Close readiness required the slot to be returned to IDLE, which only decode did. A result whose message cancellation or a full mailbox dropped could never be reached. Readiness was logical, not physical. | ok (readiness = `work_handle_finished`) |
| MF4 | `t_r2_abandon_while_publishing` | `FAIL !file_check(&f1, &reasons)`: our verified save read as foreign | The reaper treated every cancelled job as "nothing happened". It set IDLE and discarded whatever custody held, ignoring the classified outcome the job had already produced. | ok (settled from the record; HELD, baseline adopted, evidence kept) |
| MF5 | `t_r2_validate_after_own_save` | `FAIL file_backing_validate(&f1, b)` after our own VERIFIED save | Our rename drops the old inode's link, which moves its ctime. The validator compares ctime, and nothing re-baselined the backing for our own rename. | ok (the rename pair re-baselines) |
| MF6 | `t_r2_keep_hardlink` | `FAIL file_resolve_keep(&f1) == FILE_ERR_CHANGED`: foreign bytes kept | Keep inferred "the ctime move is the dropped link" from the path naming another inode. That inference is unsound (counter-example in §3). The fd leg of `file_check` omitted ctime. | ok (mapped Keep requires full validation; the fd leg uses the classifier) |
| MF7 | `t_r2_new_mode` | `FAIL ... == 0644` (it was 0600) | os had no seam for the creation default, so the temp's private 0600 became the file's mode. | ok (umask 022 → 0644; 077 → 0600; an existing 0640 is kept; the temp is never wider) |
| MF8 | `t_r2_bad_names` | the child died by SIGABRT, `file.c:35: ASSERT failed` | `split_path` treated input-shaped conditions (trailing slash, overlong leaf, the root on open) as invariants. | ok (OS_E_ISDIR, NAMETOOLONG, INVAL; NOTREG for "/") |
| MF9 | `t_r2_fifo_destination` | the child died by SIGALRM (blocked) | The metadata open was blocking, and the expectation's kind was never checked before it. | ok (NOTREG before any open; a swapped-in FIFO gives CHANGED without blocking) |
| MF10 | `t_r2_exist_race`, `t_r2_cleanup_paths`, `t_r2_barrier_retry_async`, `t_r2_check_fault_poll`, `t_r2_source_validation`, `t_r2_mapped_cancel_lease`, plus a rename counter in `t_save_dirsync_retry` | green on round 1 (these lines were implemented but untested); the red runs are the mutants below | No test reached the branches: NOREPLACE EXIST, leak counts, async retry, poll-only fault, source validation, MAPPED cancel with retire and unmap counts. | ok; every mutant is killed |

The MF10 fault-spec row runs on a MAPPED open (threshold 1, one registration counted). The running save blocks at
MID_WRITE. The tree and snapshot are dropped, and close begins, which cancels the job. While the job runs, there are 0
retires, 0 unmaps and a pristine poll. After physical completion, `file_close_finish` performs exactly 1 retire and
1 unmap of that backing, and the poll then gives NOENT. The spec's "assertion-enabled child" half does not apply:
`os_fault_retire` returns void, so there is no failed retire to report.

N2 was answered by using `fd_stat` (§4). N3: a failed fd stat is a change. N4, N5 and N8 are in §5. N9:
- The empty `if`, the unused parameters and the temp-name NUL are fixed.
- The garbled header comment was rewritten.
- `file_crc32c` still builds its table per call. It is a nit with no trace.

### Mutant table

`build/r2/mutate.py`, adapted from the reviewer's script, re-expresses each surviving mutant against the round-2 code.
Each one is applied, the san suite is rebuilt, the named test runs, and the mutant is reverted. `cmp` against the saved
base passed afterwards.

| # | Mutation | Killed by |
|---|---|---|
| M3 | leak the temp on a temp-fsync failure | `t_r2_cleanup_paths` (count_temps/fds) |
| M4 | leak the temp on NOREPLACE EXIST | `t_r2_exist_race` |
| M5 | leak the temp on a pre-rename CHANGED | `t_r2_cleanup_paths` |
| M7 | the reaper forgets an abandoned synced temp | `t_r2_cleanup_paths` |
| M8 | close_finish leaks custody | `t_r2_cleanup_paths` |
| M9 | the prepare failure path (cancel, write, source) leaks the temp | `t_r2_cleanup_paths` |
| M11a | the sync barrier retry renames again (error ignored) | `t_save_dirsync_retry` (rename counter) |
| M11b | the async barrier retry re-runs the whole publish (FORCE) | `t_r2_barrier_retry_async` |
| M12 | an absent target renamed with plain REPLACE | `t_r2_exist_race` |
| M15 | file_check drops the closing fault poll | `t_r2_check_fault_poll` |
| M18 | every rename error classed PRE_RENAME and the temp unlinked | `t_r2_uncertain_rename` |
| M20 | a verified async save does not adopt the baseline | `t_r2_barrier_retry_async` |
| M21 | prepare skips the retained-source validation | `t_r2_source_validation` |

13 of 13 were killed (P, a count of the rows). The brief says "twelve" but lists thirteen; all thirteen are here.

### Sizes

Changed lines against round 1 (`diff`, + added / − removed):

| File | Added | Removed |
|---|---|---|
| `src/file/file.c` | 901 | 544 |
| `src/file/file.h` | 304 | 171 |
| `src/os/os.h` | 13 | 0 |
| `src/os/os_posix.c` | 19 | 0 |
| `tests/file_test.c` | 846 | 29 |
| `tests/os_test.c` | 37 | 0 |
| `fuzz/file_fuzz.c` | 12 | 3 |
| `bench/file_bench.c` | 1 | 1 |
| `Makefile` | 1 | 1 |
| `tools/system-symbols.txt` | 1 | 0 |

Comment-free, non-blank lines (`build/r2/ncloc.py`):

| File | Before | After |
|---|---|---|
| `file.c` | 1,558 | 1,873 |
| `file.h` | 259 | 286 |
| `os.h` | 241 | 244 |
| `os_posix.c` | 1,286 | 1,301 |

Together, file.c and file.h now come to 2,159 lines, against v1's src/file core of 1,664 (given in the brief). Most of
the growth comes from three sources:
- the errata's new surface (`file_temp_verify`, `file_temp_recover`, the stable read-back witness check, the rename
  pair);
- the explicit-FSM rule (file.c now has 184 case labels);
- the metadata and mode split.
The tests grew more than the code.

## 10. Open items (advisor)

**MF-A: open, v2-hy8, design under signature (docs/decisions/wave1-file-source-witness.md).** The defect: edits to a
mapped file after a foreign replace-by-rename cannot be saved anywhere. A foreign rename
over the path drops the old inode's link and moves its ctime. The validator then refuses those intact bytes forever,
by the round 2 rule. So Keep, Save, Save As to a fresh name, and a conflict checkpoint that reads the mapped spans are
all refused; only reload remains, and it discards the edits. The reviewer's candidate is a content hash taken off path
after acquisition, re-checked on a ctime-only drift. Round 3 does not change this behaviour.

**MF-C: stated limit until v2-hy8, which closes it.** The limit: the rename pair can absorb a foreign rewrite inside
its window. The pair
re-baselines the displaced backing to a foreign rewrite exactly when all of these coincide:
1. The displaced inode is writable by another process through a second name (a hard link) or through a descriptor it
   already holds. The destination path itself names our new inode from the rename on.
2. That process writes it between our `rename(2)` returning and our after-fstat of the held descriptor. Those are two
   consecutive syscalls on the publish worker; only the test-only RENAMED step hook runs between them.
3. The write leaves the size unchanged.
4. The writer restores the mtime exactly (`touch -r`, `cp -p`, `rsync -t` and similar), so only ctime moves.
5. The write changes no kind, mode, owner or group, and adds no link: a link added in the window keeps the link count
   from falling, and that is refused (`t_r3_source_conflict`, relink case).

The pair then shows only "ctime moved, link count fell", which is exactly what our own rename shows. The after ctime
becomes the baseline, and the validator accepts the foreign bytes as original; undo by reference would restore them.
Without condition 2, prepare's source validation or the next validation refuses. Without condition 4, the pair is
FOREIGN. The window is the same class as N1 (recheck-then-REPLACE): inherent to plain rename plus a stat pair, not
to the code.

## 11. Round 3 report

Input: `docs/reviews/wave1-L2-1-opus-recheck.md`; the advisor's scope items 1–5. Red runs on the round 2 code are in
`build/r3/red-round2.log`, green runs in `build/r3/green-round3.log`. Each test runs alone with
`build/san/tests/file_test <name>`.

### Red and green per item

| Item | Test | Red on round 2 | Root cause | Green |
|---|---|---|---|---|
| 1 MF-B | `t_r3_unreadable_dest` | `FAIL ... == 0060`: an owner-unreadable 0060 destination became 0644, without and with FORCE | The fallback for "metadata not copied" was the new-file default, whatever the reason the copy did not happen. A destination that exists but cannot be opened was treated like no destination. | ok: the observed mode is kept; the default applies only to a name with no destination |
| 2 MF-D | `t_r3_verify`, `t_r3_recover`, `t_r3_held_retry_proof` | recover of a symlink: `FAIL ... == FILE_ERR_NOTREG` (it was IO, OS_E_LOOP). The other assertions were already green; their red is the mutants (X6, X7, X9, X13). | The NOFOLLOW open's ELOOP fell into the generic I/O path, so a foreign symlink read as "retry later". The rest was implemented but untested. | ok: content mismatch, re-stat mismatch, a name that moves between its two looks, symlink (CHANGED for verify, NOTREG for recover), and a HELD retry that syncs nothing until proven, then settles VERIFIED |
| 4a N-B | `t_r3_rename_not_performed` | `FAIL out.pub == FILE_PUB_PRE_RENAME`: EIO or EISDIR with nothing renamed gave RENAME_UNCERTAIN, HELD, and a temp left forever | The classification trusted the errno class: every error other than EXIST/NOTSUP was "uncertain". The kernel's own evidence was never looked at. | ok: classified by observation (§4); PRE_RENAME, temp removed, async IDLE, `file_check` clean |
| 4b N-C | `t_r3_source_conflict` | `FAIL calls_dir_sync == syncs + 1`: a FOREIGN pair skipped the barrier, and a retry then gave VERIFIED | The source verdict was a return path, not state: CONFLICT returned early, and custody kept no memory of why. | ok: the barrier runs; the verdict lives in custody (`file_source_pair`); retries stay CONFLICT; the validator stays false |
| 4c N-D | `t_r3_relative_own_path` | `FAIL file_baseline(..) == installed`: a relative self-save read as an external change | Own-path adoption compared spellings (`strcmp(save_dest, f->path)`), but `f->path` is canonical and the destination was not. | ok: the prepare job canonicalizes the directory |
| 5 umask | `t_r3_creation_mask` | `FAIL run_child("mask_fallback", ...)`: the constructor read the live mask, and nothing could make it fall back | The capture was a set-and-restore at constructor time, which no open failure can affect, and it rested on "no thread before main". | ok: `/proc/self/status`, read once; 077 when unreadable; under umask 027 the value is 027 |

Item 3 is the mutant table below. The round 2 tests `t_r2_uncertain_rename` and `t_r2_abandon_while_publishing` used
"EIO with nothing renamed" as their uncertain case, which round 3 reclassifies as before-rename. They now use a true
uncertain case: the temp is moved to a third name and EIO is returned, so neither name proves anything. Their
evidence assertion follows the temp to that name.

### Mutant table (item 3)

`build/r3/mutate3.py` re-expresses each surviving attack mutant of `build/review-probe/mutate2.py` against the round 3
code. Each one is applied, the san suite is rebuilt, the named test runs, and the mutant is reverted. `cmp` against
the saved base passed afterwards.

| # | Mutation | Killed by |
|---|---|---|
| X1 | the rebaseline accepts any pair difference | `t_r3_source_conflict` (grow case) |
| X2 | the rebaseline ignores the falling link count | `t_r3_source_conflict` (relink case) |
| X3 | the rebaseline does not require before == baseline | `t_r3_rebaseline_needs_baseline` |
| X4 | decode ignores the epoch | `t_r3_decode_identity` |
| X5 | decode ignores the generation | `t_r3_decode_identity` |
| X6 | the witness check skips the content CRC | `t_r3_verify` |
| X7 | the witness check skips the stable re-stat | `t_r3_verify` |
| X8 | `temp_unlink_own` drops the identity check | `t_r3_unlink_identity` |
| X9 | the HELD retry skips the installation proof | `t_r3_held_retry_proof` |
| X13 | the witness check skips the path stability recheck | `t_r3_verify` |
| X14 | VERIFIED adoption for a save to another path | `t_r3_saveas_keeps_baseline` |
| X15 | close readiness ignores the reload slot | `t_r3_close_waits_reload` |
| X16 | the installation proof ignores the `fd_stat` mtime (N2) | `t_r3_installed_touched` |
| X18 | the null-ctx validator accepts any backing | `t_r3_null_ctx_validator` |

14 of 14 were killed (P, a count of the rows).

### Sizes

Changed lines against round 2 (`diff`, + added / − removed):

| File | Added | Removed |
|---|---|---|
| `src/file/file.c` | 122 | 42 |
| `src/file/file.h` | 12 | 0 |
| `src/os/os.h` | 6 | 7 |
| `src/os/os_posix.c` | 45 | 7 |
| `tests/file_test.c` | 596 | 17 |
| `Makefile` | 1 | 1 |
| `tools/system-symbols.txt` | 0 | 1 |

Comment-free, non-blank lines (`build/r2/ncloc.py`):

| File | Before round 3 | After |
|---|---|---|
| `file.c` | 1,873 | 1,932 |
| `file.h` | 286 | 292 |
| `os.h` | 244 | 244 |
| `os_posix.c` | 1,301 | 1,332 |

## 12. Outside scope, reported only

- **MF-A and MF-C**: see §10 (v2-hy8).
- **Custody hand-over to savectl** (review N-H): the async object keeps custody in `f->save_temp` with no take
  call. That belongs to savectl's bead.
- **Publish backpressure spins.** `publish_or_continue` re-queues through `work_continue` on every invocation while the
  mailbox is full, so a bulk worker loops (CPU, not a sleep) until the UI drains. Round 1 behaved the same. It needs a
  wake-on-drain primitive in work.
- **Chown limitation:** bead v2-jh7 (Tobias). **Publish-by-exchange (N1):** a later bead, by ruling.
- **file_crc32c** rebuilds its 1 KiB table per call (N9 nit). There is no trace.
- **The reviewer's suggested separate "source drift" bit** in the outcome (N-C) is not added. Round 3 keeps CONFLICT
  for both a foreign destination and a FOREIGN pair, as the advisor scoped it. Savectl's CONFLICT_CANCEL therefore
  still conflates the two.
- **Worklog and HANDOFF** were not written. Under Law 3 the worker does not touch session state; the coordinator does.

## 13. Lane results

Round 3 stamp: BAT0 = Not charging, load1 1.30 at the check start and 1.92 at the tsan start (M), 2026-11-25
05:14–05:17 CET. Logs are in `build/r3/lane-*.log`.
- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189`: exit 0, `check: ok`.
  - check-layers: 6 isolated layer links;
  - check-san: 33 binaries, `file_test: ok (56 tests)`, `os_test: ok`;
  - check-law2: 9 release suites, `law2_file_test: ok`;
  - contract: PASS 64.
- `nice make -j2 check-tsan`: exit 0. 13 threaded suites passed, `file_test: ok (56 tests)`.
- `nice make -j2 fuzz-smoke`: exit 0. 16 fuzzers replayed their seeds.

The first `check` attempt failed at check-symbols. The release build's fortify turned the `/proc/self/status`
`read` into `__read_chk`, which is not granted. The read now goes through `os_file_read` / `os_file_close`, and all
three lanes were rerun from the start.

Round 2 (for the record):

Stamp: BAT0 = Not charging, load1 0.09 at the start and 0.73 at the tsan start (M), 2026-11-25 04:04–04:09 CET. Logs
are in `build/r2/lane-*.log`.
- `nice make -j2 check CONTRACT_CHECK_DISPLAYS=:180-:189`: exit 0, `check: ok`.
  - check-layers: 6 isolated layer links;
  - check-san: 33 binaries, `file_test: ok (41 tests)`, `os_test: ok`;
  - check-law2: 9 release suites, `law2_file_test: ok`;
  - contract: PASS 64.
- `nice make -j2 check-tsan`: exit 0. 13 threaded suites passed, `file_test: ok (41 tests)`.
- `nice make -j2 fuzz-smoke`: exit 0. 16 fuzzers replayed their seeds, `file_fuzz` included.
