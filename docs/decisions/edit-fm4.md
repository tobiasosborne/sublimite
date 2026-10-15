# edit-fm4: GB-scale orphan journals never touched on the startup path

Bead: edit-fm4 (P1 trial blocker). Worker session s7, 2026-10-15/16.

## What was happening (reproduced headless)

A discovered orphan session journal that is a checkpoint of a 1 GiB buffer
(1086427136 B (M), the same size as the profile's `session-1aAWBt.set-aside-*`
and `session-BawHhJ.set-aside-*`) went through, before the first frame:

1. `journal_retire_directory` scanned every journal artifact end to end
   (`journal_scan_file`), set-asides and `.sublimite-journal-*` temporaries
   included: several GB read per launch on the fixture profile.
2. `restore_source`: strict replay stops at the changed BASE, then
   `replay_tolerant` reads the whole 1 GiB journal into one malloc.
3. `install_session` opens the disk text with COPY acquisition
   (`copy_threshold = UINT64_MAX`) and `editor_checkpoint` writes the full text
   (DELETE base + INSERT in 4 KiB records) into the new launch's journal: a new
   1 GiB file.
4. The orphan is renamed `*.set-aside-*`. The new 1 GiB journal is the next
   launch's orphan, so every attempt adds one more 1 GiB set-aside.

Headless red run (null backend, the test below): `editor_open` did not return
within 300 s (M) [Charging, load 6.6]; the directory held the renamed orphan and
a fresh 1086427136-byte journal. On the real display the coordinator saw
`error -103` after 10.06 s (M).

## The 10 s limit

There is no 10 s discovery timeout in `src/` (searched: recovery.c, open.c,
journal/, main.c). The nearest 10 s constant is `EDITOR_IDLE_NS` (blink idle,
editor.c:570), not on the startup path. -103 is `EDITOR_ERR_IO`, which
`open_editor` returns for any positive journal code (open.c:269) and which
`restore_source` returns for failed installs. So "the timeout" is the wall time
of the synchronous restore until some failure, not a timer. Point 3 of the
brief therefore holds by construction: nothing large runs before the first
frame, so there is nothing for a timeout to cut short.

## Decision

One bound, `JOURNAL_STARTUP_SCAN_BYTES` (journal.h):

    (G) G4a warm p50 = 25 ms   (perf/01-perf-target.md §0.2, row G4a:
        "exec -> CPU back buffer complete + map requested + input accepted | 25 / 40")
    (M)[AC] page-cache sequential read = 4.2 GB/s  (perf/00-hardware-profile.md, dd 1 GiB, 1 MiB blocks)
    bound = 0.025 s x 4.2e9 B/s = 105,000,000 bytes

One read pass of that size alone spends the whole warm-startup p50. It is a
tracked aspiration, not an enforced limit: the restore cost model (journal read
+ BASE read + install + checkpoint, at least three passes) is (E).

1. **Retirement** (`retire.c`): an artifact larger than the bound is not
   scanned. It is pinned (`clean = false`) and marks the inventory uncertain,
   because its BASE references are unknown, so no retained `.sublimite-base-*`
   original is retired in that launch. Conservative: litter can stay; nothing
   is deleted on a guess.
2. **Discovery** (`recovery.c`): before replaying a discovered `session-*`, its
   cost is the journal size plus the sizes of every BASE it names. The journal
   size is checked first (fstat, no read); only a journal within the bound is
   scanned for BASE sizes, so the cost scan is bounded too. A source over the
   bound is **deferred**: not replayed, not copied, not renamed, lease
   released, still discoverable under its own name byte for byte. The status
   line shows "large session journal kept for recovery; not replayed at
   startup"; `e->recovery_deferred` counts them. A failed launch therefore
   leaves the profile as it found it (point 4), and repeated launches do not
   fill the disk.
3. Small sources keep today's behaviour exactly (edit-btj set-aside by
   rename on a changed BASE, edit-rzj capacity deferral, edit-rfq retirement).
   Explicitly named journals (`recover_existing`) keep the strict contract and
   are not size-gated.

## Not done (point 2 of the brief)

Deferred orphans are kept and listed in the status notice, but they are not
yet presented as recoverable closed tabs, and they are not restored lazily.
Today the content is recoverable with `tools/journal_replay` (manual replay)
and on every later launch it is still deferred, never lost. Doing point 2
needs, as a follow-up bead:

- a placeholder closed tab per deferred source (bounded by the closed-tab
  pool), excluded from `editor_checkpoint` and `editor_journal_metadata` so it
  is not recorded as an empty tab, holding the source's lease;
- restore on reopen or on the work pool, with install that keeps the
  large-file mapped path instead of forcing COPY acquisition;
- a checkpoint that does not rewrite the full text of an unmodified buffer
  (`editor_checkpoint` writes DELETE + INSERT of every buffer's whole text even
  when it equals BASE). That is where every GB orphan came from; without it a
  lazy restore would still write 1 GiB on the UI thread.
