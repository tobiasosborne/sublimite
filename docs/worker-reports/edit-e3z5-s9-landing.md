# edit-e3z5 landing report (s9)

Lander: Opus subagent, 2026-10-20.

- Branch: wt/edit-e3z5-s9.
- Verified commit: **f9d1185**, rebased onto main 623f6ba.
- main had moved on to cf416ea by the end of the run. On the coordinator's
  instruction this tree was **not** rebased again, so a check against
  cf416ea is still owed before fast-forwarding.

Box stamp for the final run:

| Time | BAT0 status | load average |
|---|---|---|
| start, 05:36:15 | Full | 6.04 / 7.57 / 8.43 |
| end, 05:42:00 | Full | 6.42 / 7.53 / 8.13 |

`df -h /` was 389G used / 67G free before and 391G used / 65G free after.

## Rebases and conflicts

The worker tree branched at 69647e4 (Session 17). `build/briefs/land9.sh e3z5 "<msg>"`
committed it as 180b893, reported REBASE CONFLICT and aborted (exit 3). The
rebase was then done by hand. Main moved several times during verification,
so the tree was rebased five times in all.

1. **Onto f2c8e5d (uzrb, ze59, 2mhd).** Three files conflicted.
   - `src/editor/checkpoint.c`: kept ze59's `editor_checkpoint_published`. The
     e3z5 comment edit was later reverted, so the file is identical to main.
   - `src/editor/commands.c`: Save As now runs on a worker (ze59), so the
     e3z5 peer-reload outcome logic moved into `save_as_adopt`. The command's
     own checkpoint is ze59's worker publication (`publish_owed`). A
     JOURNAL_BUSY result there already leaves it owed, so the e3z5
     `busy_checkpoint` setting was dropped.
   - `tests/dataloss_test.c`: the ze59 and e3z5 test bodies conflicted as
     both-added; both were kept. The `e3z5_save_as` test was adapted to the
     async path:
     - wait until `e->save_as` is clear and `busy_refusals` is set;
     - check `publish_owed` instead of `busy_checkpoint`;
     - inject Return without `settle`, because an owed publication counts as
       pending until the guard is released.
2. **Onto d63fbc9 (ut0m, 0rge).** Clean.
3. **Onto 5c1e112 (a7fo, 14bi, ci6i, 6yom).** Clean.
4. **Onto 1d7d29e (6090, 9fzc, 3620).** Three files conflicted.
   - Background: 9fzc replaced `editor_reload_source` with
     `editor_reload_saved_peer`, which defers the peer checkpoint to the
     worker publication. The Save As peer reload therefore no longer
     checkpoints on the UI thread and cannot be refused.
   - `src/editor/commands.c`: took main's loop. The e3z5 "refused peer
     becomes clean+stale" branch was dropped as unreachable. A comment notes
     that a refused publication stays owed.
   - `src/editor/buffers.c`: the static `reload_source` takes both the e3z5
     `editor_reload_outcome *` and 9fzc's `defer_checkpoint`. 3620's
     `keep_failed_notice` clear is kept next to the e3z5 notice clear.
   - `src/editor/private.h`: kept `editor_reload_saved_peer` and the outcome
     enum. The exported `editor_reload_source` was dropped (it no longer has
     a caller).
   - `tests/dataloss_test.c`: the peer case now expects the peer to already
     hold the written bytes, clean and not stale. Its checkpoint is the owed
     publication.
5. **Onto 8da8b8b (vp4m, y315), then 0b915c3 (dxas).** Clean.
6. **Onto 623f6ba (7cj8).** The `tests/dataloss_test.c` selector ladder
   conflicted. Both sides' selectors were kept (`--detached-rewrite`,
   `--retain-window`, `--reload-held`, `--edit-e3z5`).

The design notes for these resolutions are in docs/decisions/edit-e3z5.md,
section "Rebase onto edit-ze59 and edit-9fzc".

**Effect on the bead.** After ze59 and 9fzc, the Save As half of e3z5 reduces
to two things:

- the test that proves Save As never reports "save as failed" while the
  guard is held and lands afterwards;
- comments.

The Reload half is fully in effect: the outcome is reported, REFUSED or
DEFERRED re-arms the choice, and NO_SLOT or FAILED shows a notice and keeps
the prompt. The commit headline, given by the brief, still says "peer tab
clean+stale" and "editor_reload_source". The coordinator may want to reword
it.

## Commands and exit codes

### Final run (f9d1185 on 623f6ba)

| Command | rc |
|---|---|
| `touch src/*/*.c src/*/*.h tests/*.c; make -j3 all` (gcc, -Werror) | 0 |
| `make -j3 build/san/tests/{dataloss,editor_session,editor_checkpoint,journal,editor}_test build/tests/editor_test` | 0 |
| `build/san/tests/dataloss_test` | 0 |
| `build/san/tests/editor_session_test` | 0 |
| `build/san/tests/editor_checkpoint_test` | 0 |
| `build/san/tests/journal_test` | 0 |
| `build/san/tests/editor_test` | 0 |
| `EDITOR_ALLOC_ONLY=null build/tests/editor_test` | 0, `null 10000 keys mallocs=0 guard=active` |
| `EDITOR_ALLOC_ONLY=raster build/tests/editor_test` | 0, `raster 10000 keys mallocs=0 guard=active` |

Every sanitizer suite ran as
`XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 EDIT_DISPLAY=:95 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/<name>`,
outside the sandbox. The malloc-guard runs used the same wrapper.

e3z5 lines from the final dataloss_test run:

```
dataloss_test: edit-e3z5 save_as(peer) with guard held: notice=(none) refusals=1 owed=1 path_moved=1 owner_modified=0 peer_stale=0 peer_modified=0
dataloss_test: edit-e3z5 save_as(peer) landed after the guard was released
dataloss_test: edit-e3z5 save_as(alone) with guard held: notice=(none) refusals=1 owed=1 path_moved=1 owner_modified=0 peer_stale=-1 peer_modified=-1
dataloss_test: edit-e3z5 save_as(alone) landed after the guard was released
dataloss_test: edit-e3z5 reload with no free slot: notice=reload needs a free tab slot: close a tab, then press R stale=1 choice=0
dataloss_test: edit-e3z5 reload after a slot freed: notice=(none) stale=0
```

### Earlier runs (superseded by the rebases above)

- **a0954af on d63fbc9, then 5c1e112.**
  - `make all`: 0.
  - All five sanitizer suites: 0.
  - Malloc guard null and raster: 0, mallocs=0.
  - One attempt was killed while it waited for the :95 lock (shell exit 144),
    which is not a test failure. It was rerun with private log paths, and all
    suites returned 0.
- **0e713e0 on 8da8b8b.**
  - `make all`: 0.
  - All five sanitizer suites: 0.
  - Malloc guard null and raster: 0, mallocs=0.
- **b670d4c on 0b915c3.**
  - `make all`: 0.
  - dataloss_test: 0.
  - editor_session_test: 0.
- **Interim checks during the conflict resolutions.**
  `build/san/tests/dataloss_test --edit-e3z5` returned 0 after the ze59
  resolution and again after the 9fzc resolution.

No suite failed in any run. No GB fixture was involved beyond the existing
sparse 100 MB ze59 case. cli_test was not run; the worker's xcb_connect
failures on :99 are not this tree's regression.

## Unresolved

- main moved to cf416ea after the final run, and this tree has not been
  rebased onto it.
- The commit headline is stale with respect to the merged design (see
  "Effect on the bead" above).
