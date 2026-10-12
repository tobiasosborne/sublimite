# edit-pgr worker report

Implemented action commit framing, with the campaign's torn-undo case now in default mode. The original campaign source was copied byte-identically before the red run. The source worker later fixed only its GCC-shadowing local identifiers; a lexical comparison verified comments, literals and behavior unchanged, and the refreshed copy again differs from that provided source only by the default-mode call. The existing --probe cases remain unchanged.

Power was read from `/sys/class/power_supply/BAT0/status`: `Not charging`, interpreted as [AC] under HANDOFF. Numbers in the following pasted logs are (M)[AC] observations from the named runs, including source locations, fixture progress, process IDs and test return codes. Fixture arithmetic and the wire byte cost are (P), derived in `docs/decisions/edit-pgr.md`. Existing command names, version labels and bead identifiers are identifiers rather than performance claims. No new budgets, deadlines or acceptance counts were selected.

Files touched:

- `src/journal/journal.h`, `src/journal/journal.c`: append-compatible begin/commit markers; common byte-image and bounded streaming validation; uncommitted trailing groups are never delivered and are truncated to their pre-group prefix.
- `src/editor/input.c`, `src/editor/private.h`: bracket multi-record staged actions and continued clipboard/find replacements; retain pending commits in the existing reserved staging array.
- `src/editor/checkpoint.c`: reset group state after a successful complete checkpoint.
- `src/editor/session.c`: defer publication and checkpoint admission across an open group or continuing logical replacement.
- `tests/dataloss_test.c`: exact imported campaign fixture, then promote torn undo into default mode.
- `tests/journal_group_test.c`: undo, redo, selection replacement and large paste crashes after DELETE, before COMMIT, and after complete action; every byte cut; corrupt COMMIT; PAD/batch crossing; legacy records; single-keystroke byte/write cost.
- `tests/editor_test.c`: adapt the existing append-error suffix assertion to retained BEGIN plus both unaccepted data operations.
- `fuzz/journal_fuzz.c`: group cutoff/commit-corruption oracle and structured input coverage for the new record types.
- `docs/decisions/edit-pgr.md`, this report. Diagnostic logs and temporary verification artifacts are under `build/`.

Red run, before implementation:

```text
$ build/san/tests/dataloss_test --probe
dataloss_test: changed_base kind=0 tabs=2 notice=[file changed on disk since this session; its unsaved edits were set aside] aside=[build/dataloss-yUmvbz/session-oyOVv4.set-aside-1791814288570587979]
dataloss_test: reload_vs_keystroke turns=0 text=[xbase] stale=1 modified=1
dataloss_test: reload_vs_keystroke turns=1 text=[xbase] stale=1 modified=1
dataloss_test: two_process_save disk=[Abase] A(stale=0 modified=0) B(err=5 stale=0 modified=1 text=[Bbase])
dataloss_test: changed_base kind=1 tabs=2 notice=[file changed on disk since this session; its unsaved edits were set aside] aside=[build/dataloss-olhgco/session-HCH2tW.set-aside-1791814299163952231]
dataloss_test: torn_undo_group: killed at insert progress 16384/262144, editor showed 262144 bytes, recovered 16384 bytes
dataloss_test:465: FAIL len == 1 || len == GROUP_BYTES
dataloss_test: stranded_orphan launch 0: tabs=1 seen_crashed=0 seen_survivor=1
dataloss_test: stranded_orphan launch 1: tabs=1 seen_crashed=0 seen_survivor=1
dataloss_test: stranded_orphan launch 2: tabs=1 seen_crashed=0 seen_survivor=1
dataloss_test: stranded_orphan leftover session-tMT1a2 541 bytes
dataloss_test: stranded_orphan leftover session-PbHkmA 8192 bytes
dataloss_test:588: FAIL seen_a
dataloss_test --probe: changed_base(deleted)=ok torn_undo_group=FAIL stranded_orphan=FAIL
dataloss_test:609: FAIL !deleted && !torn && !stranded
```

Independent boundary red run, also before implementation:

```text
$ build/san/tests/journal_group_test
journal_group_test: boundary=0 redo=0 recovered=0 expected=1
journal_group_test:82 FAIL actual==want
journal_group_test:91 FAIL crash_case(AFTER_DELETE,false)==0
```

The unmodified default LeakSanitizer scan also reported that it cannot operate under ptrace in this sandbox. Subsequent verification uses `ASAN_OPTIONS=detect_leaks=0`; AddressSanitizer and UndefinedBehaviorSanitizer remain enabled. Leak checks require the coordinator's environment.

(P) BEGIN and COMMIT each cost the existing JOURNAL_HEADER, derived from magic32 + length32 + type32 + CRC32 + sequence64 + buffer64 = 32 bytes; the pair adds 64 bytes per grouped action before existing PAD rounding. A single-record keystroke adds zero bytes and zero write/sync calls. No typing-path allocation, blocking flush or new fsync was introduced. The commit append occurs after the last data append; the existing worker sync policy and protected admission credit are retained.

Green runs, using the required sanitizer flags and the declared leak-scan exception. These are completed runs; the official kill fixture failure is recorded separately below.

```text
$ build/san/tests/editor_session_test
editor_session_test: three launches leave one nonempty 0600 journal; changed base opens and sets the source aside
editor_session_test: headless save/repeated/new-target, cutoff edits, shared targets, inactive failure, interrupted-save startup, Saving acknowledgement, allocation guard passed
exit status: 0

$ build/san/tests/dataloss_test
dataloss_test: changed_base kind=0 tabs=2 notice=[file changed on disk since this session; its unsaved edits were set aside] aside=[build/dataloss-b9UST1/session-CGVTvA.set-aside-1791814937965900370]
dataloss_test: reload_vs_keystroke turns=0 text=[xbase] stale=1 modified=1
dataloss_test: reload_vs_keystroke turns=1 text=[xbase] stale=1 modified=1
dataloss_test: two_process_save disk=[Abase] A(stale=0 modified=0) B(err=5 stale=0 modified=1 text=[Bbase])
dataloss_test: torn_undo_group: killed at insert progress 16384/262144, editor showed 262144 bytes, recovered 1 bytes
dataloss_test: kill-after-append, kill at every save step, shorter base, reload/keep during held save, Ctrl+S under prompt, mixed hot exit with closed tab, reload vs keystroke, two-process save passed
exit status: 0

$ build/san/tests/journal_group_test
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
exit status: 0

$ build/tests/journal_test
journal_test: malloc_guard=active append_allocations=0
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
exit status: 0

$ build/tests/editor_test
editor_test: null 10000 keys mallocs=0 guard=active
exit status: 0

$ build/tests/editor_test --clipboard-bulk
clipboard bulk: worker queue, cross-slice CRLF, replacement/undo, journal, mallocs=0 passed
exit status: 0

$ ASAN_OPTIONS=detect_leaks=0 build/fuzz/journal_fuzz <fixed inputs: every uint8_t selector plus model-capacity payload>
***
*** NOTE: fuzzing was not performed, you have only
***       executed the target code on a fixed set of inputs.
***
exit status: 0

$ ASAN_OPTIONS=detect_leaks=0 build/edit-pgr-journal-kill-diagnostic
journal_kill_test: ok trials=50 script_edits=100000 issued=2662383 page_cache=2662338 replayed=2662361 acknowledged=2604014 (page_cache<=replayed<=issued; piece_tree==independent_byte_model_at_replayed; session_values_verified=1; trial0=kill_before_pump)
exit status: 0
```


Outside scope / unresolved inherited fixtures:

- The campaign's `stranded_orphan` probe still fails; it is already outside edit-pgr and remains --probe-only. The pasted red run contains its independent failure. No journal-retirement work was attempted.
- Initial release verification rejected the original copied campaign under `-Wshadow -Werror` because its local `journal` identifiers shadowed the typedef. The source worker fixed those identifiers in the provided file while this session was running. After verifying it was an identifier-only update, this worktree refreshed that source and retained only the default-mode promotion. No compiler warnings were disabled and no byte-identity exception is needed.
- Official `journal_kill_test` rejects the current WINDOW payload before replaying edits: it demands the legacy payload while `journal_set_window` emits the payload with JOURNAL_WINDOW_CHOSEN. The suite source is unchanged. A build-only diagnostic copy changes that expectation to JOURNAL_WINDOW_BYTES to isolate replay behavior; this does not count as an official suite pass.
- Full `make check` cannot pass inside this sandbox: leak scanning is denied; with leak scanning disabled, cli_test cannot create its IPC server. Actual native X startup is also denied. Coordinator must rerun display suites on Xvfb :99, as requested; the display number is the user-specified infrastructure identifier.

The remaining broad sanitizer suites were run serially. (M)[AC], derived by counting their recorded command/exit pairs: 62 completed, 56 passed. The other results are:

| Suite | Result and remaining work |
| --- | --- |
| editor_close_test | Headless case passes; native case fails opening the display. Its explicit `--headless` rerun passes. |
| editor_large_test | `mapped_startup_ipc` cannot create the server inside the sandbox. Other cases ran. |
| file_kill_test | Its viewport and queued children use the real raster/X platform through file_bench; display setup fails. |
| ipc_test | Socket creation returns Operation not permitted. |
| refproto_test | Cannot connect to the requested display. |
| scroll_test | Source-failure assertion in `scroll_resolve` fails before the journal/editor recovery cases. Scroll code and its fixture are unchanged; report for coordinator triage, do not fix here. |

Official `journal_kill_test` output:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/journal_kill_test
journal_kill_test: FAIL trial=0 target=1 issued=1 page_cache=1 ack=0 replay=0 rc=6 error=0
exit status: 1
```

Its legacy WINDOW expectation is (P) two uint32_t fields = 8 bytes; the writer's current payload is those fields plus flags32 = 12 bytes. The temporary diagnostic modifies only that payload-size expectation and passes the original seeded process-kill workload. This is evidence about parser replay, not a replacement for fixing or rerunning the official suite through the coordinator.

Actual native-display failure:

```text
$ ASAN_OPTIONS=detect_leaks=0 DISPLAY=:99 build/san/tests/x11_live_test
FAIL tests/x11_live_test.c:118: §22 colormap protocol error returned -1
FAIL tests/x11_live_test.c:118: §22 window protocol error returned -1
FAIL tests/x11_live_test.c:118: §22 present protocol error returned -1
x11_live_test: private display connection failed
§22 startup errors: FAILED
```

Full make-check failure with leak scanning disabled and immediate lock admission:

```text
$ ASAN_OPTIONS=detect_leaks=0 EDIT_DISPLAY_LOCK_TIMEOUT=0 make check
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
make: *** [Makefile:131: check] Error 1
```

(P) Zero lock timeout selects immediate admission rather than introducing a wait duration. The first make-check attempt with default leak scanning failed at base_test because LeakSanitizer's ptrace scan is denied. A later default-lock attempt was interrupted while waiting for the shared display lock; no other worktree or display owner was interrupted.

`make all` and `make -k -j4 all` initially rejected the original imported campaign's local identifiers under GCC's required C11 warning flags. After refreshing its source worker's identifier-only update, `make all` passes, including that release test. Compiler worker count in the keep-going command is only a build setting; it is not a workload budget or acceptance count.

`make all` is now green. The copy matches the updated provided source byte-for-byte apart from its default-mode promotion. The earlier rename question is resolved by refreshing that authorized source. Full `make check` is **not green**: display/IPC restrictions, official WINDOW fixture mismatch and scroll source-error fixture failure remain for the coordinator. No out-of-bead fixture was changed.

Journal benchmark, (M)[AC], existing workload and existing tracked bounds; timing misses are reported, with no new performance limit:

```text
$ build/bench/journal_bench --track
TRACK journal_paste bytes=1000000 requests=100 p50_ms=2.054 p99_ms=13.362 component_ceiling_ms=5/15_(G) within_gate=1 allocations=0 guard=active content_verified=1 endpoint=journal_enqueue G9_full_frame_ms=5/15_(G)_unmeasured (M)[AC] load1=26.01
TRACK journal_append payload=1 edits=100000 p50_ns=3779 p99_ns=27864 gate_p99_ns=20000_(G) within_gate=0 ui_page_cache_write=1 timer_ms=5 final_flush=1 bulk_jobs=60 bulk=index_scan/find_scan/save_write_standins (M)[AC] load1=25.45
TRACK journal_append payload=1024 edits=100000 p50_ns=3916 p99_ns=38212 gate_p99_ns=20000_(G) within_gate=0 ui_page_cache_write=1 timer_ms=5 final_flush=1 bulk_jobs=60 bulk=index_scan/find_scan/save_write_standins (M)[AC] load1=25.01
journal_bench: correctness=ok mode=TRACK gate_misses=2 status=0 (complete G1/G9 endpoints require editor harness)
exit status: 0
```

The implementation is contained in edit-pgr. No `bd` or `git` command was run, no other worktree was modified, and HANDOFF.md, CLAUDE.md, PLAN.md, PRD.md and docs/worklog were not edited. Changes in session.c are limited to publication/admission guards; recovery.c and the other workers' retirement/command code remain untouched.

Final fresh-binary green run, after the campaign-source refresh and explicit editor test rebuild. (M)[AC], from the serial commands in `build/edit-pgr-last-green.log`; `ASAN_OPTIONS=detect_leaks=0` applies to the sanitizer binaries:

```text
$ make all
make: Nothing to be done for 'all'.
exit status: 0

$ build/san/tests/journal_test
journal_test: malloc_guard=ASan-inactive (release run required) append_allocations=0
journal_test: ok (roundtrip, corruption, torn pages, straddles, base conflict, rotation, allocator, back-pressure)
exit status: 0

$ build/san/tests/editor_session_test
editor_session_test: three launches leave one nonempty 0600 journal; changed base opens and sets the source aside
editor_session_test: headless save/repeated/new-target, cutoff edits, shared targets, inactive failure, interrupted-save startup, Saving acknowledgement, allocation guard passed
exit status: 0

$ build/san/tests/dataloss_test
dataloss_test: changed_base kind=0 tabs=2 notice=[file changed on disk since this session; its unsaved edits were set aside] aside=[build/dataloss-r6bMaA/session-LeYdg5.set-aside-1791816056558724675]
dataloss_test: reload_vs_keystroke turns=0 text=[xbase] stale=1 modified=1
dataloss_test: reload_vs_keystroke turns=1 text=[xbase] stale=1 modified=1
dataloss_test: two_process_save disk=[Abase] A(stale=0 modified=0) B(err=5 stale=0 modified=1 text=[Bbase])
dataloss_test: torn_undo_group: killed at insert progress 16384/262144, editor showed 262144 bytes, recovered 1 bytes
dataloss_test: kill-after-append, kill at every save step, shorter base, reload/keep during held save, Ctrl+S under prompt, mixed hot exit with closed tab, reload vs keystroke, two-process save passed
exit status: 0

$ build/san/tests/journal_group_test
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
exit status: 0

$ build/san/tests/editor_test --headless
editor_test: scrolled undo/redo viewport repair passed
editor_test: mutation error is negative and stops the loop passed
editor_test: null script and journal byte model passed
editor_test: null 10000 keys mallocs=0 guard=ASan-inert
editor_test: idle blink timeout and unfocused timer disarm passed
editor_test: queue depth one and coalesced edits passed
editor_test: headless passed
exit status: 0
```

What remains: coordinator reruns display suites on the requested Xvfb and leak checks outside this sandbox; triage the unchanged official `journal_kill_test` WINDOW-size expectation and `scroll_test` source-failure assertion. The `stranded_orphan` probe remains an independent campaign finding. The implementation and regression coverage for edit-pgr are complete; the full aggregate check is not claimed green.

Final fuzzer build and fixed-input execution pass after updating the callback-error oracle to discard partial restore state. The inputs cover every uint8_t selector, the existing model capacity, and a CRC-valid committed group whose oversized INSERT fails its visitor after DELETE. (P) Its payload exceeds the existing model capacity by strlen("overflow"); no duration or random-run count was chosen. This is deterministic input execution, not a claim of a randomized fuzz campaign.

```text
$ make build/fuzz/journal_fuzz
exit status: 0
$ ASAN_OPTIONS=detect_leaks=0 build/fuzz/journal_fuzz <the recorded fixed inputs>
***
*** NOTE: fuzzing was not performed, you have only
***       executed the target code on a fixed set of inputs.
***
exit status: 0
```
