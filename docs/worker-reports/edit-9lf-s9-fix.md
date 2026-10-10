# edit-9lf session 9 clean-build fix

## Cause and contract decision

Read both decision records (`docs/decisions/edit-9lf.md` and
`docs/decisions/edit-ck4.md`), both workers' session/rebase reports, and the
`review_source_change`, `review_stale_controls`, and `ck4_recovery` expectations.

The expectations are compatible. §13 freezes source-dependent actions only in
the stale owner and leaves tab controls available. ck4 Keep accepts the disk
BASE only while retained original bytes remain valid, checkpoints the complete
session before resuming, and marks history dirty. Its mapped Keep fixture uses
rename-over, leaving the old mapped inode intact. `review_source_change` instead
modifies the mapped inode itself: it must become stale, refuse Keep, and accept
Reload. Reload must preserve the logical tab. None of these assertions changed.

The reported failure precedes Keep/Reload. The source-change fixture creates
`source\n` and immediately writes `change\n` (both seven bytes), optionally
truncating and restoring length before detection. On this filesystem those
operations sometimes share exactly the same mtime **and** ctime. Size, inode,
and all other identity fields also match. An inotify notification requests an
identity check; it does not itself prove a different generation. A build-only
diagnostic captured `file_id_diff == 0`, and even synchronous `file_check`
returned unchanged. This was already intermittently reported before the rebase
in `edit-9lf-s9.md`; it is not a per-tab-state or checkpoint-resolution regression.

## Minimal change

Only `tests/editor_test.c` changes: capture the initial stat and use `futimens`
after writing to advance mtime by one second. Both watch-overwrite and
focus-truncate modes now present a distinct identity, without a sleep or a
longer detection deadline. Production code, source-stale assertions, unsafe
mapped Keep refusal, and Reload assertions remain unchanged.

Git was read-only; no bd, HANDOFF.md, or docs/worklog edits. Build logs and
build-only diagnostic source/binary are retained under `/tmp/edit-9lf-fix/`.

## Failing runs (pasted)

Coordinator-provided tail, saved before removing build:

```text
288-editor_p4_test: null brace selection LIMIT scenario=0 and synchronous retry preserve bytes/history/cursor passed
289-editor_p4_test: null brace selection LIMIT scenario=1 and synchronous retry preserve bytes/history/cursor passed
290-editor_p4_test: null brace selection LIMIT scenario=2 and synchronous retry preserve bytes/history/cursor passed
291-editor_p4_test: null brace giant selection shortening succeeds atomically mallocs=0 guard=ASan-inert
292-editor_p4_test: null disconnected wait sweep, retained buffers, association reuse and allocation guard passed
293-editor_p4_test: IPC allocation deferred until pending key submission passed
294-editor_p4_test: null Enter/brace/selection/CRLF atomic undo passed
295-editor_p4_test: null full input queue journal groups preserve every key passed
296-editor_p4_test: null bracket/whitespace/chord paint passed
297-editor_p4_test: null missing-path/MRU/reopen/reorder/wrap/modified passed
298-editor_p4_test: null visual wrap motion/affinity/tab restore passed
299-editor_p4_test: null wrapped tab gutter/resize geometry passed
300-editor_p4_test: null minimap same-frame/click/drag passed
301-editor_p4_test: null IPC poll/path positions/binary stdin/all-tab wait/rollback/new-path passed
302-editor_p4_test: null IPC columns visible/bounded at certified clusters passed
303-editor_p4_test: null interrupted index recovery/stale indicator passed (stale frames=2)
304-editor_p4_test: null 100 tabs 10000 mixed keys all modules mallocs=0 guard=ASan-inert
305-editor_p4_test: raster Enter/brace/selection/CRLF atomic undo passed
306-editor_p4_test: raster full input queue journal groups preserve every key passed
307-editor_p4_test: raster bracket/whitespace/chord paint passed
308-editor_p4_test: raster missing-path/MRU/reopen/reorder/wrap/modified passed
309-editor_p4_test: raster visual wrap motion/affinity/tab restore passed
310-editor_p4_test: raster wrapped tab gutter/resize geometry passed
311-editor_p4_test: raster minimap same-frame/click/drag passed
312-editor_p4_test: raster IPC poll/path positions/binary stdin/all-tab wait/rollback/new-path passed
313-editor_p4_test: raster IPC columns visible/bounded at certified clusters passed
314-editor_p4_test: raster interrupted index recovery/stale indicator passed (stale frames=2)
315-editor_p4_test: raster 100 tabs 10000 mixed keys all modules mallocs=0 guard=ASan-inert
316-editor_p4_test: all passed
317-== build/san/tests/editor_test
318:editor_test:749: FAIL editor_get_stats(e).source_stale
```

Unmodified sanitizer binary repeated in isolation with leaks off:

```text
$ for i in $(seq 1 100); do
    DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 EDITOR_REVIEW_ONLY=16 build/san/tests/editor_test > /tmp/edit-9lf-fix/watch-probe.log 2>&1 || {
        cat /tmp/edit-9lf-fix/watch-probe.log
        printf 'ITERATION=%s\n' "$i"
        exit 1
    }
done
editor_test:749: FAIL editor_get_stats(e).source_stale
ITERATION=95
exit 1
```

Build-only diagnostic (temporary added lines shift the failure line number):

```text
watch failure mode=1 pending=1 changed=0 kind=0 open=0
diff=0 original mtime=1791662879451165225 ctime=1791662879451165225 size=7 observed mtime=1791662879451165225 ctime=1791662879451165225 size=7
synchronous check changed=0
editor_test:762: FAIL editor_get_stats(e).source_stale
ITERATION=18
exit 1
```

The first full-binary diagnostic also encountered launcher/native-open failures
under sandboxed local IPC/X11 access; its log is `before-editor.log`. Final
validation uses approved local IPC/X11 access, a private mode-0700
XDG_RUNTIME_DIR, and display :99.

## Passing runs (pasted)

Clean build (no incremental objects retained):

```text
$ rm -rf build
$ make all > /tmp/edit-9lf-fix/make-all.log 2>&1
gcc -std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion -D_GNU_SOURCE -pthread -O2 -g -msse2 -Isrc -include tests/display_guard.h -MMD -MP -c bench/zygote_bench.c -o build/rel/bench/zygote_bench.o
gcc -O2 -g -msse2 -pthread build/rel/bench/zygote_bench.o build/libedit.a -lm -ldl -lxcb -lxcb-present -lxcb-sync -lxcb-xfixes -lxcb -lxcb-shm -lxcb-present -lxcb-xkb -lxkbcommon -lxkbcommon-x11 -o build/bench/zygote_bench
MAKE_ALL_EXIT=0
```

Corrected release regression repeated 100 times:

```text
$ for i in $(seq 1 100); do DISPLAY=:99 EDIT_DISPLAY=:99 EDITOR_REVIEW_ONLY=16 build/tests/editor_test > /tmp/edit-9lf-fix/after-focused.log 2>&1 || exit 1; done
review 16: mapped watch overwrite, focus truncate, edit suspension and reload/keep passed
FOCUSED_RELEASE_RUNS=100 EXIT=0
```

Corrected ASan/UBSan regression repeated 100 times:

```text
$ for i in $(seq 1 100); do DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 EDITOR_REVIEW_ONLY=16 build/san/tests/editor_test > /tmp/edit-9lf-fix/after-focused-san.log 2>&1 || exit 1; done
review 16: mapped watch overwrite, focus truncate, edit suspension and reload/keep passed
FOCUSED_SANITIZER_RUNS=100 EXIT=0 LEAKS=OFF
```

Requested full check on the same clean build, with private runtime storage:

```text
$ XDG_RUNTIME_DIR=/tmp/edit-9lf-fix/runtime DISPLAY=:99 EDIT_DISPLAY=:99 ASAN_OPTIONS=detect_leaks=0 make check > /tmp/edit-9lf-fix/make-check.log 2>&1
P4-3 section 4: mode=COPY choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
P4-3 section 4: mode=COPY choice=Reload child abrupt exit; selected/unrelated/closed recovery exact
P4-3 section 4: mode=mapped choice=Keep child abrupt exit; selected/unrelated/closed recovery exact
P4-3 section 4: mode=mapped choice=Reload child abrupt exit; selected/unrelated/closed recovery exact
P4-3 13: suspended source preserves tab keys and tab pointer controls passed
review 16: mapped watch overwrite, focus truncate, edit suspension and reload/keep passed
editor_test: all passed
check: 63 test binaries passed
== tools/test_replay_cli.sh
ok:   --speed=inf rc=2 replay: --speed must be a finite number > 0
ok:   --speed=nan rc=2 replay: --speed must be a finite number > 0
ok:   --speed=0 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=-1 rc=2 replay: --speed must be a finite number > 0
ok:   --speed=2 rc=0
test_replay_cli: all passed
== tools/test_display_lock.sh contract
waiting for display :99 (held by 1064711)
true: display lock timeout: display :99 (held by 1064711)
keyinject: --pairs must be nonzero
waiting for display :99 (held by 1064711)
refwin_test: display lock timeout: display :99 (held by 1064711)
== build/san/tests/x11_identity_test
waiting for display :99 (held by 1064711)
x11_identity_test: display lock timeout: display :99 (held by 1064711)
make[1]: *** [Makefile:105: check] Error 1
waiting for display :99 (held by 1064711)
sh: display lock timeout: display :99 (held by 1064711)
waiting for display :99 (held by 1064711)
sh: display lock timeout: display :99 (held by 1064711)
waiting for display :99 (held by 1064711)
sh: display lock timeout: display :99 (held by 1064711)
waiting for display :99 (held by 1064711)
sh: display lock timeout: display :99 (held by 1064711)
display lock contract: owner PID, alias/timeout, make/scripts, nested inheritance, exit status PASS
MAKE_CHECK_EXIT=0
```

**Leaks were off.** All 63 ASan/UBSan test binaries, replay CLI contract, and
display-lock contract passed. The display-lock contract intentionally runs
children and a nested make while holding the lock; their timeouts and nested
`make[1]` error above are expected negative-test evidence. The outer `make check`
exited zero. Full logs: `/tmp/edit-9lf-fix/make-all.log` and
`/tmp/edit-9lf-fix/make-check.log`.

Final `git diff --check`: exit 0. No production changes and no assertion removals.

