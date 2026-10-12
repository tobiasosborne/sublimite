# edit-1vo worker report

Implemented recovery on the next save request. A retained failed token is
reconciled through a complete CURRENT checkpoint before the requested buffer's
fresh save starts. Cancellation preserves current text independently of the
retained original; verified replacement recovery finishes against the captured
replacement BASE. Recovery failure retains ownership for another request.
Requests from another tab resolve the same session slot. Save reaches recovery
even when a verified self-replacement has raised the source-change input gate.

Files touched:

- `src/editor/session.c`: local request/recovery state, CURRENT capture, token
  reconciliation, and restart of the requested buffer's save. Replay untouched.
- `src/editor/input.c`: Save is a control action at the source-change gate.
- `src/savectl/savectl.h` and `src/savectl/savectl.c`: worker-prepared recovery
  request API, context retirement, preparation-error preservation, and draining
  accepted batches before recovery finish rotation.
- `tests/editor_session_test.c`: injected failures before/after rename;
  same-tab and other-tab retry, token retirement, subsequent saving, dirty-state
  checks, and a release allocation guard around retry admission.
- `docs/decisions/edit-1vo.md`: design choices.
- `docs/worker-reports/edit-1vo-s6.md`: this report.

No `bd` or `git` commands were run. All source edits are in this worktree.
Protected handoff, plan, requirements, rules, and worklog files were not edited.

## Red and green

(M)[AC] Evidence below is verbatim command output. Diagnostic source positions
come from `__LINE__`; exit statuses come from the shell. Fixture quantities in
suite output derive from its launch/journal assertions. Power was `Not charging`
from `/sys/class/power_supply/BAT0/status`, the repository's AC stamp.
In sanitizer commands, `detect_leaks=0` disables LeakSanitizer; ASan and UBSan
remain enabled. An attempted default run reported that LeakSanitizer could not
inspect the sandbox's processes. The failing run's positions precede subsequent
fixture additions.

The regression first asserted a retained PREPARED token and unchanged original
after injected rename failure. Retrying Ctrl+S then failed the disk-content
assertion, verifying the review mechanism before implementation.

```text
$ make build/san/tests/editor_session_test
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_session_test
editor_session_test:69: FAIL match
editor_session_test:238: FAIL disk_is(other_tab ? other : f.path, other_tab ? "y" : after_rename ? "xbase" : "xybase") == 0
editor_session_test:499: FAIL recoverable_save_retry(false, false) == 0
```

Final green runs after implementation and allocation-guard additions:

```text
$ ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_session_test
editor_session_test: three launches leave one nonempty 0600 journal; changed base opens and sets the source aside
editor_session_test: headless save/repeated/new-target, recoverable failure retry, cutoff edits, shared targets, inactive failure, interrupted-save startup, Saving acknowledgement, allocation guard passed
$ build/tests/editor_session_test
editor_session_test: three launches leave one nonempty 0600 journal; changed base opens and sets the source aside
editor_session_test: headless save/repeated/new-target, recoverable failure retry, cutoff edits, shared targets, inactive failure, interrupted-save startup, Saving acknowledgement, allocation guard passed
```

Both final runs exited successfully. The release run uses the active allocator
interposer; retry admission met the no-heap-allocation typing-path contract.
Sanitizer allocation guards are inert, as documented by the existing suites.

## Build and broader verification

`make all` passed, including the final incremental rebuild. (M)[AC] Compiler
identities from `--version`: GCC `13.3.0`, Clang `18.1.3`; (P) C11 and the warning
flags are the supplied build contract. The Makefile retained
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion` and its ASan/UBSan flags.
Build parallelism derived from `nproc`; test binaries ran serially.

`make check` built all sanitizer binaries and passed `base_test`, then failed
in `cli_test` at local IPC setup after waiting for the shared display lock.
The remaining binaries were run serially, using existing headless modes for
editor suites. `editor_test --headless`, `editor_session_test`, `savectl_test`,
`savectl_reload_test`, and `journal_test` passed.

The sandbox denies local Unix socket binding: an isolated bind probe returned
`Operation not permitted`. Headless `editor_p4_test` and `editor_large_test`
passed when rerun with local IPC enabled through an automatically approved
test-only sandbox escalation. `ipc_test` then reached the unrelated launcher
fixture failure described below.

Full verification is **not entirely green**. Socket-independent suites passed
except `journal_kill_test`. Its failure and the IPC fixture failure remain
outside this bead. Detailed serial results are in
`build/edit-1vo-serial-summary.log`, with individual logs under
`build/edit-1vo-suites/`. Headless rerun evidence is in
`build/edit-1vo-p4-ipc.log`, `build/edit-1vo-large-ipc.log`, and
`build/edit-1vo-ipc.log`. Final TDD logs are `build/edit-1vo-red.log`,
`build/edit-1vo-final-san.log`, and `build/edit-1vo-final-release.log`.

Display-dependent tests failed inside this sandbox for lack of an accessible X
socket. Failures include the raster/view portions of `file_kill_test`,
`raster_test`, reference-window suites, and native X input/clipboard suites.
Some optional native suites skipped successfully. The coordinator must rerun
full display suites on Xvfb `:99` (P: display identifier supplied in the brief).
`refwin_test` also encountered the shared lock's immediate-admission refusal.

Release benchmarks were run serially with the AC/load stamps emitted by their
existing harnesses. `savectl_bench --guard --probe --track` passed its controller
smoke run; its probe population does not establish qualified throughput or
saving-frame latency. The initially launched full savectl population run was
interrupted; remaining populations are unmeasured. Logs are
`build/edit-1vo-savectl-guard.log` and `build/edit-1vo-savectl-bench.log`.
The null editor/font benchmark passed its structural and allocation checks:

```text
build/bench/editor_bench --track --no-idle --no-p4 --font-baseline='DejaVu Sans Mono' --file=build/edit-1vo-bench-source
```

Its corpus is the existing regression marker `base` followed by newline, a
deliberate fixture choice (E); sampling counts come from the unchanged benchmark
defaults. Null typing exceeded the existing tracked latency aspirations on the
shared loaded box; no new target or budget was introduced. Full measured output,
endpoint definitions, power/load stamps, and allocator evidence are in
`build/edit-1vo-editor-bench.log`.

## Remaining work and findings outside scope

- `journal_kill_test` rejects the writer's flagged WINDOW payload because its
  replay callback requires the legacy payload shape. `journal_set_window()`
  emits `JOURNAL_WINDOW_BYTES`; the failure is a callback refusal, not a save
  controller failure. Left unchanged for the coordinator.
- With IPC enabled, `ipc_test` fails the main-launcher fixture's `accepted`
  assertion; its reported tab count exceeds the fixture expectation. This
  occurs in launcher setup without a save request. Left unchanged; evidence is
  in `build/edit-1vo-ipc.log`.
- Existing editor-wide journal I/O errors can still become sticky through
  `editor.c` journal-error handling. This bead restores retry for retained
  controller failures while the editor loop remains usable; it does not change
  that separate global error policy.
- Existing eviction of the saving owner's published leaf remains the separate
  lifetime issue described in the editor review. No tab-retention changes were
  made here.

The implementation fits this bead. The remaining coordinator work is resolving
the unrelated suite failures and running full display validation.
