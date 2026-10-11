# edit-ret worker report

Clean buffers now reload external disk changes without submitting the
`Source changed: R reload / K keep` prompt. Dirty buffers retain that prompt.
The shared Reload implementation preserves logical tab identity, selection,
cursor and scroll byte positions, clamps positions past the replacement text,
and starts fresh undo history. Inactive open tabs refresh without switching
the active tab. The file module and its shared inotify service are unchanged.

No `git` or `bd` commands were run. This worker stops after this bead.

## Evidence convention

Verification results are (M)[AC]: `/sys/class/power_supply/BAT0/status` read
`Not charging` before verification. Exit statuses are process results from the
commands below, with success defined by the executable/build's normal exit
status. Source coordinates in captured failures refer to the source compiled
for that run. Sanitizer flags come from the existing Makefile.

The clean regression matrix has eight cases (G: regression coverage), derived
from two cleanliness histories (open/saved), two original-storage modes
(copied/mapped), and two tab states (active/inactive): 2 × 2 × 2 = 8. Each case
appends the fixture's `EXTERNAL_APPEND` line and then truncates to `z`.
Offsets derive from the fixture's newline-delimited text; its watchdog reuses
the existing editor `settle` watchdog, rather than imposing a new timing gate.

## Red run

Before implementation, built the sanitizer editor test and ran:

```sh
make build/san/tests/editor_test
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --clean-source-change
```

The first case edited the buffer, saved with Ctrl+S, waited for save completion,
verified the clean indicator, appended externally, and stepped the editor.
It exited with failure (M)[AC], with this captured assertion:

```text
editor_test:1171: FAIL !e->buffer->source_stale && !probe.shown
```

Evidence: `build/edit-ret-evidence/red.log`, `red-build.log`, and
`red-rebuild.log`. Leak checking was disabled for this intentional assertion
failure, which exits before fixture cleanup.

## Green runs

`make all` passed (M)[AC], including the release editor, tools, tests, and
bench binaries. The final repeat reports that all targets are up to date.
Evidence: `build/edit-ret-evidence/make-all.log`.

All of the following passed (M)[AC] with AddressSanitizer,
UndefinedBehaviorSanitizer, and LeakSanitizer enabled:

| Command, with `ASAN_OPTIONS=detect_leaks=1` | Evidence under `build/edit-ret-evidence/` |
| --- | --- |
| `build/san/tests/editor_test --headless` | `editor-headless-leaks.log` |
| `EDITOR_REVIEW_ONLY=p4-16 build/san/tests/editor_test --headless` | `explicit-reload.log` |
| `build/san/tests/editor_p4_test --headless` | `editor-p4-headless-unsandboxed.log` |
| `build/san/tests/editor_large_test` | `editor-large-green.log` |
| `build/san/tests/editor_session_test` | `editor-session-leaks.log` |
| `build/san/tests/editor_hot_exit_test` | `editor-hot-exit-leaks.log` |
| `build/san/tests/editor_scroll_test` | `editor-scroll-leaks.log` |
| `build/san/tests/config_editor_test` | `config-editor-leaks.log` |
| `build/san/tests/editor_close_test --headless` | `editor-close-headless-leaks.log` |
| `build/san/tests/file_test` | `file-rerun.log` |
| `build/san/tests/savectl_test` | `savectl-leaks.log` |

The full headless editor suite includes the new clean regression and the
existing dirty mapped-source prompt/unsafe-Keep/Reload regression. Focused
runs also passed:

```sh
ASAN_OPTIONS=detect_leaks=0 build/san/tests/editor_test --clean-source-change
ASAN_OPTIONS=detect_leaks=0 EDITOR_REVIEW_ONLY=16 build/san/tests/editor_test --headless
```

Their logs are `green-clean.log` and `green-dirty.log`. The clean regression
checks bytes and rendered appended text, unchanged view positions on append,
clamping on truncation, empty undo/redo history, and absence of a prompt in
every submitted frame. Its save cases explicitly drain self-save watch events
and confirm they neither replace the buffer nor reset its retained undo.

Release allocation verification also passed (M)[AC]:

```sh
build/tests/editor_test --headless
build/tests/editor_p4_test --headless
```

Both report `mallocs=0 guard=active` (M)[AC], derived from the existing counting
allocator across ingress through submission. The key count is the existing
fixture's 10000 (G: CLAUDE law 2), not a new workload size. Evidence:
`editor-release-headless.log` and `editor-p4-release-headless.log`.

The initial sandbox run could not initialize P4's local Unix-socket server,
and LeakSanitizer reported that it cannot run under ptrace. The same headless
suites were subsequently run using approved sandbox escalation; local IPC and
LeakSanitizer passed. No X display was used.

Making the large-file Keep fixture actually dirty initially invalidated its
index and broke its warm-job setup assertion. The fixture now waits for its
index to complete before arranging the held warm/index jobs; the full suite
passes. This preserves its Keep/cancellation coverage under the new behavior.

## Files changed

- `src/editor/buffers.c`: shared replacement helper; saved-history cleanliness
  check; automatic refresh admission; inactive tab state and checkpoint rollback.
- `tests/editor_test.c`: clean save/open external-change regression and prompt
  frame oracle; actual unsaved edits in dirty prompt and explicit Reload cases.
- `tests/editor_large_test.c`: actual unsaved edits in Keep fixtures, updated
  snapshot/index lengths, and completed-index setup before held warm work.
- `tests/editor_p4_test.c`: headless selector for its existing null-backend cases.
- `tests/editor_close_test.c`: headless selector for its existing injected-close case.
- `docs/decisions/edit-ret.md`: cleanliness, shared Reload, admission, and
  checkpoint decisions.
- `docs/worker-reports/edit-ret.md`, `docs/worklog/2026-10-11.md`, and
  `HANDOFF.md`: worker evidence and handoff.

## Out-of-scope findings and limits

- The unchanged file suite initially failed its shared-watcher peer-change
  assertion in `t_astra_watch_many`; its subsequent leak-enabled rerun passed.
  Both logs are retained (`file.log`, `file-rerun.log`). No file-service changes
  were made and the transient failure was not diagnosed further.
- Reload still uses the existing retained-generation policy. In
  `editor_retire_buffers`, a retired buffer with a file remains retained until
  editor shutdown; automatic refresh does not redesign physical retirement or
  spare-slot capacity. Acquisition/checkpoint failure keeps the existing
  recovery prompt and old resources.
- Native display suites, display benchmarks, and the Astra/Sublime drive are
  the coordinator's. `make check` was not invoked because it includes those
  display suites; the headless editor coverage above was run explicitly.
