# edit-2bl worker report

Scope completed: repair the kill campaign's WINDOW visitor and exercise action
group framing across process kills. All edits are local to this worktree.
No `bd` or `git` commands were run.

Files touched:

- `tests/journal_kill_test.c`: accept legacy/current WINDOW payloads, verify
  current chosen flag and session buffer ID; retain a legacy-record parser
  fixture; add deterministic BEGIN/DELETE/pre-COMMIT/post-COMMIT SIGKILL cases
  with PAD and a batch transition inside the action.
- `docs/decisions/edit-2bl.md`: test-only design and fixture derivations.
- `docs/worker-reports/edit-2bl-s6.md`: this report.

The production writer, decoder, editor and input/scroll/find modules needed no
changes. The legacy fixture exercises the kill test's own visitor; existing
`journal_test` covers legacy wire replay and flag defaults, while
`window_size_test` covers keeping the DPI default for legacy records.

Evidence below is (M)[AC]: BAT0 reported `Not charging` before verification.
Generated replay/issued/page-cache/ack totals are observed test instrumentation,
not budgets. Campaign trial/script counts, seeds, fixture dimensions and
watchdogs reuse the unchanged pre-existing test; this bead introduces no new
trial count or duration. New group cases derive from the action's wire
boundaries. Return/error values and sequences derive from the journal/POSIX
APIs; `rc=6` denotes `JOURNAL_CALLBACK`.

Red: existing campaign, before the WINDOW visitor repair. The unchanged test
reproduced the same failure; after adding the legacy/group cases it still
failed at that WINDOW callback:

```console
$ make build/san/tests/journal_kill_test
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/journal_kill_test
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
journal_kill_test: legacy WINDOW parser passed
journal_kill_test: BEGIN/DELETE/pre-COMMIT/post-COMMIT kills with PAD passed
exit=1
```

Green: required targeted sanitizer suites after the visitor repair. Each run
uses `ASAN_OPTIONS=detect_leaks=0`; display variables are set to the coordinator's
safe display for the editor-backed group suite. The compiler uses the repository's
C11 warning flags and ASan/UBSan configuration.

```console
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/journal_kill_test
journal_kill_test: legacy WINDOW parser passed
journal_kill_test: BEGIN/DELETE/pre-COMMIT/post-COMMIT kills with PAD passed
journal_kill_test: ok trials=50 script_edits=100000 issued=2660916 page_cache=2660869 replayed=2660886 acknowledged=2607996 (page_cache<=replayed<=issued; piece_tree==independent_byte_model_at_replayed; session_values_verified=1; trial0=kill_before_pump)
exit=0
```

```console
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/journal_test
journal_test: full VIEW roundtrip/rotation, legacy defaults, invalid lengths/flags passed
journal_test: empty placeholder cleanup preserves nonempty/replacement inodes; mode 0600 and legacy empty close passed
P4-3 section 5: abrupt child exit, beyond-prefix rewrite/restored mtime rejected before callbacks
journal_test: insert credit counts UI cache bytes, bounds blocked-worker loss including PAD, resumes after durable fence
journal_test: insert continuations bound one record/step, protect every prefix, retain IO progress, allocate zero bytes
journal_test: CRC32C wire/BASE vectors match independent oracle (tails, unaligned replay, maximum record)
journal_test: overlapping saves preserve both targets/cutoffs/deltas in both finish orders and publication/retirement failures
journal_test: session values exact after replay/rotation/power image; CRC-valid semantic mutants rejected
journal_test: writeback loss requires a fresh checkpoint (released unsynced batch included)
journal_test: retained identity has a full metadata barrier (empty BASE mtime survives)
journal_test: retention completion crosses the routed work mailbox
journal_test: PAD IO outranks submission BUSY and retries only on worker
journal_test: valid 4095-byte BASE retains in bounded recovery directory
journal_test: page cache ok (before pump, blocked worker, PAD gap, completion preserves UI prefix)
journal_test: append failures ok (EAGAIN/ENOSPC/EIO/EINTR/short, single attempt, retained worker retry, sticky IO, allocations=0)
journal_test: append gaps ok (PAD failure surfaced, split INSERT suffix retained, retry blocks later offsets)
journal_test: invalid setup options create no journal
journal_test: 4095-byte journal path rotates via retained directory fd
journal_test: journal rotation survives parent directory rename
journal_test: exact scripted corruption/truncation prefix (first page, 64 KiB, PAD, byte contents)
journal_test: FULL flush reports suspension; complete checkpoint clears it
journal_test: split 1 MB paste accepted, replay bytes exact, allocations=0
journal_test: atomic 1 MiB paste accepted, replay bytes exact, allocations=0
journal_test: untitled checkpoint >64 MiB restores exact content and resumes appends
journal_test: canonical base and journal paths survive cwd changes
journal_test: session IDs rejected by writer
journal_test: session IDs rejected by CRC-repaired parser
journal_test: unreadable/short/missing bases are conflicts; replay leaves log intact
journal_test: 255-byte journal basename rotates with independent temporary name
journal_test: retry ok (write failures retain progress; sync EIO requires fresh checkpoint)
journal_test: barriers ok (durable image, creation, 64KiB straddle, sync/rename/dir failures)
journal_test: save ok (file_save_begin, retained generation, post-save edits, two buffers, marker/rename/dir failures)
journal_test: file mailbox ok (copy, mmap, empty, open failure, unread close, independent save wait)
journal_test: worker returns with undrained completion
journal_test: cadence parameter ok (byte boundary, timer, force, rotation)
journal_test: save transaction accepts a 250-byte base filename
journal_test: save retains a named base shared by two buffers
journal_test: retained recovery survives old-fd truncate/overwrite (replacement, acknowledged post-save edits, restart before finish)
journal_test: retained recovery survives hard-link alias overwrite (replacement, acknowledged post-save edits, restart before finish)
journal_test: retained worker copy ok (short writes, exact multi-chunk bytes, mutation rejection, data/directory failures before publication)
journal_test: malloc_guard=ASan-inactive (release run required) append_allocations=0
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
exit=0
```

```console
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/journal_group_test
journal_group_test: boundary=0 action=0 recovered=1 expected=1
journal_group_test: boundary=1 action=0 recovered=1 expected=1
journal_group_test: boundary=2 action=0 recovered=16388 expected=16388
journal_group_test: boundary=0 action=1 recovered=16388 expected=16388
journal_group_test: boundary=1 action=1 recovered=16388 expected=16388
journal_group_test: boundary=2 action=1 recovered=1 expected=1
journal_group_test: boundary=0 action=2 recovered=16388 expected=16388
journal_group_test: boundary=1 action=2 recovered=16388 expected=16388
journal_group_test: boundary=2 action=2 recovered=1 expected=1
journal_group_test: boundary=0 action=3 recovered=1 expected=1
journal_group_test: boundary=1 action=3 recovered=1 expected=1
journal_group_test: boundary=2 action=3 recovered=16388 expected=16388
journal_group_test: legacy, PAD, all byte cuts, corrupt commit, keystroke wire cost passed
journal_group_test: undo/redo/replacement/paste boundary crashes passed
exit=0
```

Release build: GCC with the repository's C11 warning flags passed. The
initial release build compiled all targets; the final plain invocation confirms
the tree is up to date. Compiler versions were observed with `gcc --version`
and `clang --version` and match the requested release/sanitizer toolchains.

```console
$ make all
make: Nothing to be done for 'all'.
exit=0
```

Full sanitizer check built all test binaries, then stopped at the CLI suite.
(M)[AC], tail of `build/edit-2bl-s6/make-check.log`:

```console
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 make check
== build/san/tests/base_test
0.0.1-dev
arena: ok
malloc guard: SKIP (AddressSanitizer owns malloc)
pool: ok
malloc guard exempt: SKIP (sanitizer owns malloc)
cpu: avx2=1 popcnt=1
base_test: PASS
== build/san/tests/cli_test
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
cli_test: §36 forced startup timeout correctly failed
cli_test:391: FAIL ipc_server_init_display(&decoy, NULL, getenv("DISPLAY")) == IPC_OK
make: *** [Makefile:133: check] Error 1
exit=2
```

For the non-display run, `TEST_SAN` was overridden with the manifest generated
from `tests/*_test.c`, excluding exactly the Makefile's `DISPLAY_TEST_SAN`
patterns. The scoped `make check` passed base/config cases, then stopped at
`dataloss_test` when a forked editor returned `EDITOR_ERR_IO`; the process
ended with SIGPIPE. Every subsequent manifest suite was run serially and its
exit recorded. The initial failure and all continuation results are retained
in `build/edit-2bl-s6/make-check-headless.log` and
`build/edit-2bl-s6/headless-remaining.log`. (M)[AC] continuation transcript:

```console
build/san/tests/file_kill_test exit=1
file_kill_test:198: FAIL rc == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
file_kill_test:324: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
SELF_CHECK section=26 PASS
SELF_CHECK section=27 PASS
SELF_CHECK section=28 PASS
file_cancelled_open: PASS queued/pending-prefix/pending-map getters and filtered delivery
file_save_alloc: PASS pooled fresh ack guard=ASan-inert (release required)
file_kill_test: FAIL visibility/cancellation; power-loss barriers unverified

build/san/tests/file_test exit=0
build/san/tests/find_bench_test exit=0
build/san/tests/find_bestof_test exit=0
build/san/tests/find_p1r_test exit=0
build/san/tests/find_simd_filter_verify_test exit=0
build/san/tests/find_test exit=0
build/san/tests/find_timeout_test exit=0
build/san/tests/findui_test exit=0
build/san/tests/font_selection_test exit=0
build/san/tests/font_size_test exit=0
build/san/tests/font_test exit=0
build/san/tests/harness_test exit=0
build/san/tests/hl_parse_test exit=0
build/san/tests/hl_test exit=0
build/san/tests/hl_worker_test exit=0
build/san/tests/indent_test exit=0
build/san/tests/ipc_test exit=1
init rc=4 errno=1 (Operation not permitted)
FAIL tests/ipc_test.c:130: init_rc==IPC_OK

build/san/tests/journal_group_test exit=0
build/san/tests/journal_kill_test exit=0
build/san/tests/journal_retire_test exit=0
build/san/tests/journal_test exit=0
build/san/tests/keys_test exit=0
build/san/tests/layout_test exit=0
build/san/tests/lineidx_test exit=0
build/san/tests/minimap_test exit=0
build/san/tests/piece_bounds_test exit=0
build/san/tests/piece_mem_test exit=0
build/san/tests/piece_mt_test exit=0
build/san/tests/piece_test exit=0
build/san/tests/prewake_test exit=0
build/san/tests/raster_scroll_test exit=0
build/san/tests/render_test exit=0
build/san/tests/replay_test exit=0
build/san/tests/runtime_test exit=0
build/san/tests/savectl_reload_test exit=0
build/san/tests/savectl_test exit=0
build/san/tests/scan_test exit=0
build/san/tests/scroll_display_test exit=0
build/san/tests/scroll_test exit=0
build/san/tests/startup_test exit=0
build/san/tests/tabs_test exit=0
build/san/tests/trace_pairs_test exit=0
build/san/tests/trace_test exit=0
build/san/tests/undo_test exit=0
build/san/tests/unicode_render_test exit=0
build/san/tests/utf8_tables_test exit=0
build/san/tests/utf8_test exit=0
build/san/tests/view_test exit=0
build/san/tests/wayland_bench_test exit=0
build/san/tests/wayland_gl_test exit=0
build/san/tests/wayland_test exit=0
build/san/tests/window_size_test exit=0
build/san/tests/work_foreground_test exit=0
build/san/tests/work_test exit=0
build/san/tests/wrap_test exit=0
```

The isolated serial rerun of `dataloss_test` passed; no source change was made
between runs. Its transient startup failure is outside this bead and remains
reported for the coordinator. (M)[AC]:

```console
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 build/san/tests/dataloss_test
dataloss_test: changed_base kind=0 tabs=2 notice=[file changed on disk since this session; its unsaved edits were set aside] aside=[build/dataloss-zNSTyA/session-hGfgey.set-aside-1791822510596279184]
dataloss_test: reload_vs_keystroke turns=0 text=[xbase] stale=1 modified=1
dataloss_test: reload_vs_keystroke turns=1 text=[xbase] stale=1 modified=1
dataloss_test: two_process_save disk=[Abase] A(stale=0 modified=0) B(err=5 stale=0 modified=1 text=[Bbase])
dataloss_test: torn_undo_group: killed at insert progress 32768/262144, editor showed 262144 bytes, recovered 1 bytes
dataloss_test: kill-after-append, kill at every save step, shorter base, reload/keep during held save, Ctrl+S under prompt, mixed hot exit with closed tab, reload vs keystroke, two-process save passed
exit=0
```

Both shell contracts from `make check` were run separately because the full
recipe had stopped before reaching them. (M)[AC]:

```console
$ sh tools/test_replay_cli.sh build
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
exit=0
$ sh tools/test_display_lock.sh contract build/san/tests/refwin_test
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
exit=0
```

What remains / findings outside the bead (reported, not fixed):

- Display suites cannot connect to the X socket inside this sandbox. The
  coordinator must rerun them on Xvfb `:99`. A direct locked `x11_live_test`
  run confirms the connection failure (transcript below).
- `file_kill_test` is outside the Makefile's display classification but its
  viewport and queued-child fixtures call `view_init` from
  `bench/file_bench.c`, which calls `plat_init` and maps a real window. The
  observed failures are viewport initialization and those queued child exits;
  include this suite in the coordinator's Xvfb rerun. Its cancellation and
  save-allocation checks passed in this sandbox.
- `ipc_test` reports `IPC_IO` with `Operation not permitted`. Creating an
  AF_UNIX socket succeeded, but a filesystem bind to a temporary fixture
  inside this worktree independently failed with `EPERM`. CLI startup also
  stopped at `ipc_server_init_display`, whose implementation binds a Unix
  endpoint. These require a rerun where local socket binding is permitted.
- LeakSanitizer failed even on the initial unchanged red run with
  `LeakSanitizer does not work under ptrace`. Subsequent verification used
  `ASAN_OPTIONS=detect_leaks=0`, retaining ASan and UBSan. Leak checking remains
  for the coordinator outside this sandbox.

(M)[AC] direct display evidence; protocol/error values are observed outcomes:

```console
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 EDIT_DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/x11_live_test
FAIL tests/x11_live_test.c:118: §22 colormap protocol error returned -1
FAIL tests/x11_live_test.c:118: §22 window protocol error returned -1
FAIL tests/x11_live_test.c:118: §22 present protocol error returned -1
x11_live_test: private display connection failed
§22 startup errors: FAILED
exit=1
```

There is no remaining implementation work in edit-2bl. The required journal
suites passed, including legacy WINDOW handling and grouped action kill safety.
No unrelated code, protected handoff/worklog/planning files, or Makefile was
edited. Generated build logs and manifests are under `build/edit-2bl-s6/`.
