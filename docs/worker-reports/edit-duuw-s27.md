# edit-duuw (session 27): idle spin and stopped journal after an open burst

## Root cause (confirmed)

The cause is the one in the bead text. A test reproduces it headless, with no gdb.

1. `editor_step` set `e->journal_waiting` when `journal_pump` returned 0. Only
   `editor_route_work` cleared it, when `journal_receive` accepted the pump's
   completion. `journal_flush` drains the journal mailbox with its own drain
   callback. `protected_record` (buffers.c) calls it on `JOURNAL_BUSY`, and so
   do the other UI-thread flush sites. A flush that runs while a pump is in
   flight takes the completion, and the flag stays true for the rest of the
   process. From then on the editor never pumps again, so durable_sequence
   stops. New File, Open and Save As, and the save, retain and publish gates
   that test the flag, never run.
2. `runnable()` counted a pending file command as runnable with only three
   of the service's seven gates (`!dirty`, `!backend->active`). As a result
   `wait_timeout` returned 0 and the UI thread spun.

Red-run state (same shape as the coordinator's gdb dump): `file_command=150`
(NEW_FILE), `op_count=0`, `accepted=724 > durable=681`, `pending_bytes=9882`.

## Fix

- **Ownership (derived, the brief's preferred option):** the journal now
  answers whether a job is in flight. A new query, `bool
  journal_in_flight(const journal *j)`, is in `src/journal/journal.h` with its
  comment and in `journal.c` (`j && j->active`). The field
  `editor.journal_waiting` is deleted. `editor_journal_waiting(e)`
  (session.c, declared in private.h) returns `e->journal &&
  (editor_save_leased(e) || journal_in_flight(e->journal))`. While a lease
  is held the UI does not read the journal, and the journal counts as busy.
  Every old reader now calls this helper: pump admission and the
  wait_timeout journal deadline (editor.c), the file-command gate
  (commands.c), the retain/publish tick (retain.c), and save start and
  finish (session.c). The flag writes in `editor_route_work`, `editor_step`
  and `editor_close` are gone. The reasoning is in `docs/decisions/edit-duuw.md`.
- **One gate predicate:** `editor_file_command_ready(e)` (commands.c) is
  the whole gate of `editor_file_command_service`, and `runnable()` now
  calls it. A gated command can no longer make `wait_timeout` return 0.
- No new allocation anywhere. The query is one load and is not on the
  typing path.

## Files touched

- `src/journal/journal.h`, `src/journal/journal.c`: `journal_in_flight`
- `src/editor/private.h`: field removed; `editor_journal_waiting`,
  `editor_file_command_ready` declared
- `src/editor/editor.c`: route_work, runnable, wait_timeout, step, close
- `src/editor/commands.c`: `editor_file_command_ready`, service uses it
- `src/editor/session.c`: `editor_journal_waiting`, save gates
- `src/editor/retain.c`: publish gate
- `tests/editor_session_test.c`: `duuw_burst`, `duuw_gated`, selector
  `--edit-duuw`, and both in the default run
- `tests/journal_test.c`: `in_flight_test` (`--in-flight`, and in the default run)
- `tests/editor_test.c`: settle-timeout diagnostic reads the helper
- `docs/decisions/edit-duuw.md`, this report

## Tests

`duuw_burst`: it opens small files through `editor_add_buffer` with the IPC
drain's own admission (no activation busy, no mapped open). Each open
happens right after a step, so it lands between that step's pump and the
next drain. It sums the exact TABS record bytes (48 + 8n for n tabs) until
it crosses `JOURNAL_DEFAULT_SYNC_BYTES`, which took 121 opens (122 tabs,
65,824 bytes). It then asserts four things: Ctrl+N adds a tab, an idle
`editor_step(e, 0)` returns `EDITOR_OK`, durable_sequence reaches
accepted_sequence through blocking steps of one journal tick, and a window
close returns `EDITOR_CLOSED`.

`duuw_gated` is narrower and covers the runnable half on its own. A held job
occupies the journal pool's one worker, so a typed key's pump stays queued
and in flight. While it waits, Ctrl+N must leave the step at `EDITOR_OK`
with the command still pending. Once the worker is released, the tab
appears.

`journal_test --in-flight`: pump sets in-flight, flush clears it and makes
the sequences equal, and the owner's later drain finds nothing (the flush
took the completion). I wrote this test together with the query, so its
only possible red is a compile failure; I did not record a red run for it.

### Red runs

Original code, `build/san/tests/editor_session_test --edit-duuw` (burst case):

    edit-duuw: 121 opens, 122 tabs, 65824 TABS bytes
    edit-duuw: Ctrl+N pending: file_command=150 op_count=0 accepted=724 durable=681 pending_bytes=9882
    editor_session_test:2719: FAIL trace_now_ns() < limit
    exit=1

Journal fix in place but `runnable()` set back to the old clause
`(e->file_command != KEYS_ACTION_NONE && !e->dirty && !e->backend->active)`
(gated case; the idle step stayed `EDITOR_MORE` until the deadline):

    editor_session_test:2765: FAIL trace_now_ns() < limit
    exit=1

### Green run

    edit-duuw: gated Ctrl+N: step=0 file_command=150 in_flight=1 tabs=1
    edit-duuw: 121 opens, 122 tabs, 65824 TABS bytes
    edit-duuw: a Ctrl+N gated by an in-flight journal job sleeps, then runs on its completion
    edit-duuw: after an open burst Ctrl+N completes, the idle step sleeps, the journal stays durable and close finishes
    exit=0

## Manual check (build/sublimite, Xvfb :99, Not charging, load avg 8.30)

I created a private temp dir with `mktemp -d` (`/tmp/tmp.A4E49YxG2Y`) and
set XDG_DATA_HOME, XDG_RUNTIME_DIR and XDG_CONFIG_HOME inside it, with
`EDIT_REFUSE_REAL_DISPLAY=1`. I started the server with no file and ran 200
sequential clients, one small file each (all 200 returned 0). Then I ran
`xdotool windowfocus --sync` and `key ctrl+n`.

- `top -b -n1 -p PID` 3 s after Ctrl+N: `S 0,0 %CPU`, TIME+ 0:00.45; 2 s
  later still 0,0 % and TIME+ unchanged; `/proc/PID/status` State S
  (sleeping).
- Screenshot `/tmp/tmp.A4E49YxG2Y/shot.png`: the tab strip ends with
  `small-189.txt` ... `small-200.txt`, then the active tab `untitled` in
  bright text. The empty document shows line number 1 and the caret.
- SIGTERM: the process exited within the 15 s poll with status 0. Its log
  reports the kept session journal (`unsaved session journal: ...`), as on
  any hot exit.

## Verification

- `make -j4 all`: exit 0 (gcc, -Werror).
- `ASAN_OPTIONS=detect_leaks=1`, run through `tools/with_display_lock.sh`
  on :99: `build/san/tests/journal_test` exit 0,
  `build/san/tests/editor_session_test` (full default run) exit 0,
  `build/san/tests/editor_test` exit 0 ("editor_test: all passed").
- Release `build/tests/editor_test` (malloc guard active) exit 0; the
  guarded paths report `mallocs=0`, and the G1 typing test is in this suite.
- `make -j4 check`, launched with DISPLAY=:99: exit 0 (Charging, load
  avg 6.01 at start). Summary line: `check: 117 test binaries passed in
  147.6 s (parallel, 12 jobs, 6 displays)`. The runner (tools/check_parallel.py)
  starts its own private, locked Xvfbs above :99; that is how it is built,
  not something I chose. One FLAKY: `editor_large_test` failed in the
  parallel run (`editor_large_test:724: FAIL !e->buffer->pending_open &&
  editor_large_status_get(e).mapped`, in wrapped_byte_jump) and passed when
  rerun alone. I then ran it alone three more times with detect_leaks=1 on
  :99, and all three exited 0. I judge it unrelated to this change: that case
  opens no journal, so `editor_journal_waiting` is false, and it sets no file
  command, which is the only thing the new runnable clause changes.

## runnable() audit (same bug class: a clause true while its service refuses)

Fixed: the file_command clause.

Reported, not fixed. Each of these can only spin for a while: the condition
it waits on has its own wake, so none can stick forever the way the file
command did. Making them stricter without a wake audit risks a missed wake.

1. Pointer scrolling: `runnable` requires `!backend->active &&
   !scroll_waiting && !wheel_pending && !clip_busy`.
   `editor_pointer_tick` (input.c:1510) also refuses on `queue_count`,
   `view_busy`, `action != NONE`, `layout_busy` and `full_pending`. A
   drag-scroll while input is queued but not drainable (`queue_pick` returns
   SIZE_MAX during a save lease or while journal ops are pending) makes the
   loop spin until the lease or the ops end.
2. Save As adoption: `editor_save_as_runnable` is `save_as && !writer busy`.
   `editor_save_as_tick` (commands.c:239,244) also refuses on dirty,
   backend active, op_count, activation busy, and a busy save controller of
   the same buffer. The loop spins until those clear.
3. Dirty frame: `runnable` has `dirty && !backend->active && !clip_busy`.
   `submit` (editor.c:430) also refuses on op_count, scroll_waiting,
   REPLAY_WORK, GOTO, `!paint_ready`, full_pending, layout_busy, view_busy,
   extra_rows, and on `JOURNAL_BUSY` from `editor_journal_metadata` or
   `editor_journal_paint`. Most of those are runnable clauses of their own.
   op_count and journal BUSY are not: the loop spins until the op completes
   or the pump completion arrives.
4. The lineidx clause omits the service's `b == e->buffer && action ==
   REPLAY_WORK` skip. REPLAY_WORK is runnable anyway, so it adds no spin.
5. close_ready and source_choice match their services exactly.

## Outside scope (reported, not fixed)

- `journal_receive` also returns true for retention messages
  (`retention_receive`). The old `editor_route_work` would have cleared
  `journal_waiting` on one, but it returns early on
  `JOURNAL_RETAIN_MESSAGE`, so this did not bite. With the derived state
  the question no longer applies.
- The headless fixture's tab strip holds 128 tabs: `editor_add_buffer`
  returned `EDITOR_ERR_ARG` at open 128 (M). `build/sublimite` accepted 200
  opens in the manual check, so its strip is sized differently. I did not look into
  this.
