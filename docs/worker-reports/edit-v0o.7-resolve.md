# edit-v0o.7 rebase resolver report

Resolved the single conflict in `src/editor/editor.c` on 2026-10-11.
No `git` or `bd` command was run; rebase bookkeeping remains with the
coordinator.

## Resolution

The `editor_step` slice loop retains both sides:

```c
if (!view_busy(&e->v) && e->action != EDITOR_ACTION_REPLAY_WORK) {
    rc = editor_preferences_apply(e); if (rc) return fail(e, rc);
}
if (e->action == EDITOR_ACTION_CLIP || e->action == EDITOR_ACTION_REPLAY_WORK || view_busy(&e->v)) rc = editor_continue_action(e);
```

This preserves the config branch's idle-view/replay guard and config application
before input/layout/paint dispatch, while retaining main's clipboard continuation
alongside replay and view work. The surrounding dispatch code is unchanged.
The marker scan of `src/editor/editor.c` finds no conflict markers.
Only this conflict and this report were edited.

## Builds

Both commands exited successfully:

```sh
make all
make build/san/tests/config_test build/san/tests/config_editor_test build/san/tests/config_worker_test build/san/tests/editor_test
```

Release compilation used GCC with
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`.
Sanitizer compilation used Clang with the same language/warning flags and
`-fsanitize=address,undefined -fno-omit-frame-pointer`.
Complete build output is in `build/config-resolve-verification/make-all.log`
and `make-san.log`.

## Sanitizer results

Every execution below used `ASAN_OPTIONS=detect_leaks=0`.
Exit statuses are observed process results, not performance bounds.

| Binary / arguments | Exit status | Result |
| --- | --- | --- |
| `build/san/tests/config_test` | 0 | TOML types/mutations, reload/inotify/shared watches, settings and keymap passed. |
| `build/san/tests/config_editor_test` | 0 | Next frame generation, theme, wrap, tabs, font contract and keymap passed. |
| `build/san/tests/config_worker_test` | 0 | Worker cancellation and transactional settings passed. |
| `build/san/tests/editor_test --ck4-save` | 1 | Saved-content assertion failed; details below. |
| `build/san/tests/editor_test --clipboard` | 0 | Round trip, bulk paste/cut/undo, deep middle click, PRIMARY and no-tabs cases passed. |
| `build/san/tests/editor_test --minimap-flash` | 0 | Null typing probe passed, including its stale-frame requirement. |
| `build/san/tests/editor_test` via display lock on `:99` | 1 | Display lock timed out before binary execution. |
| `build/san/tests/editor_test` via display lock on `:10099` | 1 | Binary executed; stopped at `ck4_recovery(1)` during `editor_open`. |

The full-suite commands were:

```sh
ASAN_OPTIONS=detect_leaks=0 EDIT_DISPLAY_LOCK_TIMEOUT=10 DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test
ASAN_OPTIONS=detect_leaks=0 EDIT_DISPLAY_LOCK_TIMEOUT=0 DISPLAY=:10099 sh tools/with_display_lock.sh build/san/tests/editor_test
```

The shared `:99` lock was occupied. The second run selected an unused display
number to execute the headless prefix without competing with that owner; no
X server was started there. It passed the independent minimap pixel reference,
minimap typing probe, selection chords, Ctrl+Y redo, panel notice, clipboard,
journal credit/metadata/target and FIFO checks, then stopped before native
backend tests. This is not a full editor-suite pass or native-display validation.

### Ctrl+S failure

```text
editor_test:2102: FAIL !memcmp(saved, "xa", 2)
editor_test:2121: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
```

The current `src/editor/input.c` dispatcher has no `KEYS_ACTION_SAVE` handler;
that action falls through its unimplemented-action path. The default Ctrl+S
binding still maps to `KEYS_ACTION_SAVE`. The fixture does not enable config
preferences, so `editor_preferences_apply` returns without applying settings.
These observations explain why the save probe does not save; no pre-resolution
baseline or main-branch comparison was established. The input dispatcher was
not changed as part of this single-hunk resolution.

### Full-suite failure

```text
editor_test:2084: FAIL editor_open(&e, &cfg, &backend) == 0
editor_test:2540: FAIL ck4_recovery(1) == 0
editor_test:2121: FAIL WIFEXITED(status) && WEXITSTATUS(status) == 0
```

The failing recovery fixture uses the null backend. Its open failure has not
been attributed to config or to display availability. Subsequent suite cases
were not reached. No ASan/UBSan error was printed in the executed test logs.

Complete test logs are under `build/config-resolve-verification/`:
`config-test.log`, `config-editor-test.log`, `config-worker-test.log`,
`editor-test-ck4-save.log`, `editor-test-clipboard.log`,
`editor-test-minimap-flash.log`, `editor-test-all.log`, and
`editor-test-all-isolated-display.log`.

The conflict is resolved and both builds pass. Editor acceptance remains
blocked by the save and full-suite recovery failures recorded above.
